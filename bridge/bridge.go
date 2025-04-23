//go:build cgo
// +build cgo

package main

/*
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

typedef uint64_t godbc_handle_t;
typedef char* godbc_string_t;

static inline void* mallocWrapper(size_t size) {
    return malloc(size);
}

static inline char* alloc_string(const char* str) {
    size_t len = strlen(str);
    char* result = (char*)malloc(len + 1);
    if (result) {
        strcpy(result, str);
    }
    return result;
}

static inline void free_string(char* str) {
    free(str);
}
*/
import "C"

import (
	"context"
	"database/sql"
	"fmt"
	"strings"
	"sync"
	"time"
	"unsafe"

	_ "github.com/microsoft/go-mssqldb"
)

// Handle represents a database resource handle
type Handle uint64

// Pool represents a connection pool
type Pool struct {
	db                *sql.DB
	minConns          int
	maxConns          int
	connTimeout       time.Duration
	retryDelay        time.Duration
	retryAttempts     int
	networkRetryDelay time.Duration
	mu                sync.RWMutex
	activeConns       int
	verboseLogging    bool
	connections       []*sql.Conn
	minExecTime       time.Duration
	maxExecTime       time.Duration
	minConnTime       time.Duration
	maxConnTime       time.Duration
	minCloseTime      time.Duration
	maxCloseTime      time.Duration
	metricsMu         sync.RWMutex
}

var (
	mu       sync.RWMutex
	handles         = make(map[Handle]interface{})
	nextID   Handle = 1
	connPool        = make(map[string]*Pool)
)

// preallocateConnections preallocates the minimum number of connections
func (p *Pool) preallocateConnections() error {
	p.mu.Lock()
	defer p.mu.Unlock()

	p.connections = make([]*sql.Conn, 0, p.minConns)
	for i := 0; i < p.minConns; i++ {
		conn, err := p.db.Conn(context.Background())
		if err != nil {
			// Close all connections we've created so far
			for _, c := range p.connections {
				c.Close()
			}
			return fmt.Errorf("failed to preallocate connection: %v", err)
		}
		// Test the connection
		err = conn.PingContext(context.Background())
		if err != nil {
			conn.Close()
			// Close all connections we've created so far
			for _, c := range p.connections {
				c.Close()
			}
			return fmt.Errorf("failed to ping connection: %v", err)
		}
		p.connections = append(p.connections, conn)
	}
	return nil
}

// getConnectionWithRetry attempts to get a connection with retries
func (p *Pool) getConnectionWithRetry() (*sql.Conn, error) {
	p.mu.Lock()
	defer p.mu.Unlock()

	startTime := time.Now()
	defer func() {
		connTime := time.Since(startTime)
		p.updateConnectionTiming(connTime)
		if p.verboseLogging {
			fmt.Printf("Connection time: %v\n", connTime)
		}
	}()

	// First try to get an existing connection
	if len(p.connections) > 0 {
		conn := p.connections[len(p.connections)-1]
		p.connections = p.connections[:len(p.connections)-1]

		// Test the connection
		err := conn.PingContext(context.Background())
		if err == nil {
			// Create a new connection to maintain the minimum if needed
			if len(p.connections) < p.minConns {
				go func() {
					p.mu.Lock()
					defer p.mu.Unlock()
					for i := 0; i < p.retryAttempts; i++ {
						newConn, err := p.db.Conn(context.Background())
						if err == nil {
							err = newConn.PingContext(context.Background())
							if err == nil {
								p.connections = append(p.connections, newConn)
								return
							}
							newConn.Close()
						}
						if i < p.retryAttempts-1 {
							time.Sleep(p.retryDelay)
						}
					}
				}()
			}
			return conn, nil
		}

		// If connection is dead, close it and create a new one
		conn.Close()
	}

	// Create a new connection
	var lastErr error
	for attempt := 0; attempt < p.retryAttempts; attempt++ {
		if attempt > 0 {
			if isNetworkError(lastErr) {
				if p.verboseLogging {
					fmt.Printf("Network error detected, waiting %v before retry: %v\n", p.networkRetryDelay, lastErr)
				}
				time.Sleep(p.networkRetryDelay)
			} else {
				if p.verboseLogging {
					fmt.Printf("Retry attempt %d/%d after connection failure: %v\n", attempt, p.retryAttempts, lastErr)
				}
				time.Sleep(p.retryDelay)
			}
		}

		conn, err := p.db.Conn(context.Background())
		if err == nil {
			err = conn.PingContext(context.Background())
			if err == nil {
				return conn, nil
			}
			conn.Close()
			lastErr = err
			continue
		}
		lastErr = err
	}
	return nil, fmt.Errorf("failed to get connection after %d attempts: %v", p.retryAttempts, lastErr)
}

// updateConnectionTiming updates min and max connection times
func (p *Pool) updateConnectionTiming(d time.Duration) {
	p.metricsMu.Lock()
	defer p.metricsMu.Unlock()

	if p.minConnTime == 0 || d < p.minConnTime {
		p.minConnTime = d
	}
	if d > p.maxConnTime {
		p.maxConnTime = d
	}
}

// updateExecutionTiming updates min and max execution times
func (p *Pool) updateExecutionTiming(d time.Duration) {
	p.metricsMu.Lock()
	defer p.metricsMu.Unlock()

	if p.minExecTime == 0 || d < p.minExecTime {
		p.minExecTime = d
	}
	if d > p.maxExecTime {
		p.maxExecTime = d
	}
}

// updateCloseTiming updates min and max close times
func (p *Pool) updateCloseTiming(d time.Duration) {
	p.metricsMu.Lock()
	defer p.metricsMu.Unlock()

	if p.minCloseTime == 0 || d < p.minCloseTime {
		p.minCloseTime = d
	}
	if d > p.maxCloseTime {
		p.maxCloseTime = d
	}
}

// returnConnection returns a connection to the pool
func (p *Pool) returnConnection(conn *sql.Conn) {
	p.mu.Lock()
	defer p.mu.Unlock()

	startTime := time.Now()
	defer func() {
		closeTime := time.Since(startTime)
		p.updateCloseTiming(closeTime)
		if p.verboseLogging {
			fmt.Printf("Connection close time: %v\n", closeTime)
		}
	}()

	// Test the connection before returning it to the pool
	err := conn.PingContext(context.Background())
	if err != nil {
		conn.Close()
		// Create a new connection to maintain the minimum
		for i := 0; i < p.retryAttempts; i++ {
			newConn, err := p.db.Conn(context.Background())
			if err == nil {
				err = newConn.PingContext(context.Background())
				if err == nil {
					p.connections = append(p.connections, newConn)
					break
				}
				newConn.Close()
			}
			if i < p.retryAttempts-1 {
				time.Sleep(p.retryDelay)
			}
		}
		return
	}

	// Keep the connection if we're below maxConns
	if len(p.connections) < p.maxConns {
		p.connections = append(p.connections, conn)
	} else {
		conn.Close()
	}

	// If we're still below minConns, create new connections
	for len(p.connections) < p.minConns {
		for i := 0; i < p.retryAttempts; i++ {
			newConn, err := p.db.Conn(context.Background())
			if err == nil {
				err = newConn.PingContext(context.Background())
				if err == nil {
					p.connections = append(p.connections, newConn)
					break
				}
				newConn.Close()
			}
			if i < p.retryAttempts-1 {
				time.Sleep(p.retryDelay)
			}
		}
	}
}

// isNetworkError checks if the error is related to network connectivity
func isNetworkError(err error) bool {
	if err == nil {
		return false
	}
	errStr := err.Error()
	networkErrors := []string{
		"connection refused",
		"connection reset",
		"network is unreachable",
		"no route to host",
		"i/o timeout",
		"connection timed out",
		"tcp connection rejected",
		"dial tcp",
	}
	for _, netErr := range networkErrors {
		if strings.Contains(strings.ToLower(errStr), netErr) {
			return true
		}
	}
	return false
}

// isSQLError checks if the error is related to SQL syntax or query execution
func isSQLError(err error) bool {
	if err == nil {
		return false
	}
	errStr := strings.ToLower(err.Error())
	sqlErrors := []string{
		"incorrect syntax",
		"invalid object name",
		"column name",
		"constraint violation",
		"duplicate key",
		"foreign key",
		"primary key",
		"deadlock",
		"timeout expired",
		"permission denied",
		"invalid column name",
		"invalid parameter",
	}
	for _, sqlErr := range sqlErrors {
		if strings.Contains(errStr, sqlErr) {
			return true
		}
	}
	return false
}

//export GodbcConnect
func GodbcConnect(connStr *C.char, minConns, maxConns C.int, connTimeoutMs, retryDelayMs, retryAttempts C.int, networkRetryDelaySecs C.int, verboseLogging C.int, errPtr **C.char) C.godbc_handle_t {
	// Convert C string to Go string
	connString := C.GoString(connStr)

	// Parse the connection string to get the key-value pairs
	params := make(map[string]string)
	for _, pair := range strings.Split(connString, ";") {
		parts := strings.SplitN(pair, "=", 2)
		if len(parts) == 2 {
			params[strings.TrimSpace(parts[0])] = strings.TrimSpace(parts[1])
		}
	}

	// Convert connection timeout to time.Duration (now supporting larger values)
	connTimeout := time.Duration(connTimeoutMs) * time.Millisecond

	// Convert retry delay to time.Duration (now supporting larger values)
	retryDelay := time.Duration(retryDelayMs) * time.Millisecond

	// Convert network retry delay to time.Duration (now supporting larger values)
	networkRetryDelay := time.Duration(networkRetryDelaySecs) * time.Second

	db, err := sql.Open("sqlserver", connString)
	if err != nil {
		setError(err, errPtr)
		return 0
	}

	pool := &Pool{
		db:                db,
		minConns:          int(minConns),
		maxConns:          int(maxConns),
		connTimeout:       connTimeout,
		retryDelay:        retryDelay,
		retryAttempts:     int(retryAttempts),
		networkRetryDelay: networkRetryDelay,
		verboseLogging:    verboseLogging != 0,
	}

	// Set connection pool settings
	db.SetMaxOpenConns(int(maxConns))
	db.SetMaxIdleConns(int(minConns))
	db.SetConnMaxLifetime(connTimeout)

	// Preallocate minimum connections
	err = pool.preallocateConnections()
	if err != nil {
		db.Close()
		setError(err, errPtr)
		return 0
	}

	return C.godbc_handle_t(storeHandle(pool))
}

//export GodbcClose
func GodbcClose(h C.godbc_handle_t, errPtr **C.char) {
	mu.Lock()
	defer mu.Unlock()

	handle := Handle(h)
	if obj, exists := handles[handle]; exists {
		switch v := obj.(type) {
		case *Pool:
			// Only close the pool if this is the last handle
			var hasOtherHandles bool
			for h2, obj2 := range handles {
				if h2 != handle {
					if pool2, ok := obj2.(*Pool); ok && pool2 == v {
						hasOtherHandles = true
						break
					}
				}
			}
			if !hasOtherHandles {
				v.mu.Lock()
				for _, conn := range v.connections {
					conn.Close()
				}
				v.connections = nil
				v.db.Close()
				v.mu.Unlock()

				// Remove from connPool
				for connStr, pool := range connPool {
					if pool == v {
						delete(connPool, connStr)
						break
					}
				}
			}
			delete(handles, handle)
		case *sql.Conn:
			// Find the pool this connection belongs to
			var foundPool *Pool
			for _, pool := range connPool {
				pool.mu.Lock()
				for _, conn := range pool.connections {
					if conn == v {
						foundPool = pool
						break
					}
				}
				pool.mu.Unlock()
				if foundPool != nil {
					break
				}
			}
			if foundPool != nil {
				foundPool.returnConnection(v)
			} else {
				v.Close()
			}
			delete(handles, handle)
		case *sql.Tx:
			v.Rollback()
			delete(handles, handle)
		case *sql.Stmt:
			v.Close()
			delete(handles, handle)
		default:
			*errPtr = C.CString("invalid handle type")
			return
		}
	} else {
		*errPtr = C.CString("invalid handle")
		return
	}
}

//export GodbcExecute
func GodbcExecute(h C.godbc_handle_t, query *C.char, errPtr **C.char) {
	obj, err := getHandle(Handle(h))
	if err != nil {
		setError(err, errPtr)
		return
	}

	pool, ok := obj.(*Pool)
	if !ok {
		setError(fmt.Errorf("invalid handle type"), errPtr)
		return
	}

	startTime := time.Now()
	defer func() {
		execTime := time.Since(startTime)
		pool.updateExecutionTiming(execTime)
		if pool.verboseLogging {
			fmt.Printf("Query execution time: %v\n", execTime)
		}
	}()

	conn, err := pool.getConnectionWithRetry()
	if err != nil {
		setError(err, errPtr)
		return
	}

	ctx := context.Background()
	_, err = conn.ExecContext(ctx, C.GoString(query))
	if err != nil {
		conn.Close()
		setError(err, errPtr)
		return
	}

	pool.returnConnection(conn)
}

//export GodbcQuery
func GodbcQuery(h C.godbc_handle_t, query *C.char, errPtr **C.char) C.godbc_handle_t {
	mu.Lock()
	defer mu.Unlock()

	handle := Handle(h)
	obj, exists := handles[handle]
	if !exists {
		*errPtr = C.CString("invalid handle")
		return 0
	}

	pool, ok := obj.(*Pool)
	if !ok {
		*errPtr = C.CString("Invalid connection handle")
		return 0
	}

	var lastErr error
	var rows *sql.Rows

	for attempt := 0; attempt < pool.retryAttempts; attempt++ {
		conn, err := pool.getConnectionWithRetry()
		if err != nil {
			lastErr = err
			if !isNetworkError(err) {
				break
			}
			if attempt < pool.retryAttempts-1 {
				time.Sleep(pool.retryDelay)
			}
			continue
		}

		rows, err = conn.QueryContext(context.Background(), C.GoString(query))
		if err == nil {
			// Store both the connection and rows to ensure proper cleanup
			h := nextID
			nextID++
			handles[h] = &queryResult{
				rows: rows,
				conn: conn,
				pool: pool,
			}
			return C.godbc_handle_t(h)
		}

		pool.returnConnection(conn)
		lastErr = err
		// Don't retry if it's a SQL error
		if isSQLError(err) {
			break
		}
		// Only retry network errors
		if !isNetworkError(err) {
			break
		}
		if attempt < pool.retryAttempts-1 {
			time.Sleep(pool.retryDelay)
		}
	}

	*errPtr = C.CString(lastErr.Error())
	return 0
}

//export GodbcQueryWithParams
func GodbcQueryWithParams(h C.godbc_handle_t, query *C.char, params **C.char, paramCount C.int, errPtr **C.char) C.godbc_handle_t {
	obj, err := getHandle(Handle(h))
	if err != nil {
		*errPtr = C.CString(err.Error())
		return 0
	}

	pool, ok := obj.(*Pool)
	if !ok {
		*errPtr = C.CString("Invalid connection handle")
		return 0
	}

	goParams := make([]interface{}, paramCount)
	for i := 0; i < int(paramCount); i++ {
		param := C.GoString(*(**C.char)(unsafe.Pointer(uintptr(unsafe.Pointer(params)) + uintptr(i)*unsafe.Sizeof(*params))))
		goParams[i] = param
	}

	var lastErr error
	var rows *sql.Rows
	for attempt := 0; attempt < pool.retryAttempts; attempt++ {
		conn, err := pool.getConnectionWithRetry()
		if err != nil {
			lastErr = err
			if !isNetworkError(err) {
				break
			}
			if attempt < pool.retryAttempts-1 {
				time.Sleep(pool.retryDelay)
			}
			continue
		}

		rows, err = conn.QueryContext(context.Background(), C.GoString(query), goParams...)
		if err == nil {
			// Store both the connection and rows to ensure proper cleanup
			h := nextID
			nextID++
			handles[h] = &queryResult{
				rows: rows,
				conn: conn,
				pool: pool,
			}
			return C.godbc_handle_t(h)
		}
		conn.Close()
		lastErr = err
		// Don't retry if it's a SQL error
		if isSQLError(err) {
			break
		}
		// Only retry network errors
		if !isNetworkError(err) {
			break
		}
		if attempt < pool.retryAttempts-1 {
			time.Sleep(pool.retryDelay)
		}
	}

	*errPtr = C.CString(lastErr.Error())
	return 0
}

// queryResult holds both the rows and the connection to ensure proper cleanup
type queryResult struct {
	rows *sql.Rows
	conn *sql.Conn
	pool *Pool
}

//export GodbcNext
func GodbcNext(h C.godbc_handle_t, errPtr **C.char) C.int {
	obj, err := getHandle(Handle(h))
	if err != nil {
		*errPtr = C.CString(err.Error())
		return -1
	}

	qr, ok := obj.(*queryResult)
	if !ok {
		*errPtr = C.CString("Invalid result set handle")
		return -1
	}

	if qr.rows.Next() {
		return 1
	}

	if err := qr.rows.Err(); err != nil {
		*errPtr = C.CString(err.Error())
		return -1
	}

	return 0
}

//export GodbcScan
func GodbcScan(h C.godbc_handle_t, values ***C.char, count C.int, errPtr **C.char) *C.char {
	obj, err := getHandle(Handle(h))
	if err != nil {
		*errPtr = C.CString(err.Error())
		return nil
	}

	qr, ok := obj.(*queryResult)
	if !ok {
		*errPtr = C.CString("Invalid result set handle")
		return nil
	}

	rawValues := make([]interface{}, count)
	scanValues := make([]interface{}, count)
	for i := range rawValues {
		scanValues[i] = &rawValues[i]
	}

	if err := qr.rows.Scan(scanValues...); err != nil {
		*errPtr = C.CString(err.Error())
		return nil
	}

	cValues := C.mallocWrapper(C.size_t(count) * C.size_t(unsafe.Sizeof(uintptr(0))))
	cValuesSlice := (*[1 << 30]*C.char)(cValues)

	for i, v := range rawValues {
		var str string
		switch v := v.(type) {
		case nil:
			str = "NULL"
		case []byte:
			str = string(v)
		default:
			str = fmt.Sprint(v)
		}
		cValuesSlice[i] = C.CString(str)
	}

	*values = (**C.char)(cValues)
	return nil
}

//export GodbcCloseRows
func GodbcCloseRows(h C.godbc_handle_t, errPtr **C.char) *C.char {
	mu.Lock()
	defer mu.Unlock()

	handle := Handle(h)
	obj, exists := handles[handle]
	if !exists {
		*errPtr = C.CString("invalid handle")
		return nil
	}

	qr, ok := obj.(*queryResult)
	if !ok {
		*errPtr = C.CString("Invalid result set handle")
		return nil
	}

	// Remove the handle before closing to prevent race conditions
	delete(handles, handle)

	// Close rows first
	if err := qr.rows.Close(); err != nil {
		// Even if rows.Close() fails, try to return the connection
		qr.pool.returnConnection(qr.conn)
		*errPtr = C.CString(err.Error())
		return nil
	}

	// Then return the connection to the pool
	qr.pool.returnConnection(qr.conn)
	return nil
}

//export GodbcBeginTransaction
func GodbcBeginTransaction(h C.godbc_handle_t, errPtr **C.char) C.godbc_handle_t {
	obj, err := getHandle(Handle(h))
	if err != nil {
		*errPtr = C.CString(err.Error())
		return 0
	}

	pool, ok := obj.(*Pool)
	if !ok {
		*errPtr = C.CString("Invalid connection handle")
		return 0
	}

	conn, err := pool.getConnectionWithRetry()
	if err != nil {
		*errPtr = C.CString(err.Error())
		return 0
	}

	tx, err := conn.BeginTx(context.Background(), nil)
	if err != nil {
		conn.Close()
		*errPtr = C.CString(err.Error())
		return 0
	}

	mu.Lock()
	handle := Handle(nextID)
	nextID++
	handles[handle] = tx
	mu.Unlock()

	return C.godbc_handle_t(handle)
}

//export GodbcExecuteInTransaction
func GodbcExecuteInTransaction(h C.godbc_handle_t, query *C.char, errPtr **C.char) *C.char {
	obj, ok := handles[Handle(h)]
	if !ok {
		*errPtr = C.CString("invalid handle")
		return nil
	}

	tx, ok := obj.(*sql.Tx)
	if !ok {
		*errPtr = C.CString("invalid transaction handle")
		return nil
	}

	_, err := tx.ExecContext(context.Background(), C.GoString(query))
	if err != nil {
		*errPtr = C.CString(err.Error())
		return nil
	}

	return nil
}

//export GodbcExecuteInTransactionWithParams
func GodbcExecuteInTransactionWithParams(h C.godbc_handle_t, query *C.char, params **C.char, paramCount C.int, errPtr **C.char) *C.char {
	obj, ok := handles[Handle(h)]
	if !ok {
		*errPtr = C.CString("invalid handle")
		return nil
	}

	tx, ok := obj.(*sql.Tx)
	if !ok {
		*errPtr = C.CString("invalid transaction handle")
		return nil
	}

	goParams := make([]interface{}, paramCount)
	for i := 0; i < int(paramCount); i++ {
		param := C.GoString(*(**C.char)(unsafe.Pointer(uintptr(unsafe.Pointer(params)) + uintptr(i)*unsafe.Sizeof(*params))))
		goParams[i] = param
	}

	_, err := tx.ExecContext(context.Background(), C.GoString(query), goParams...)
	if err != nil {
		*errPtr = C.CString(err.Error())
		return nil
	}

	return nil
}

//export GodbcCommit
func GodbcCommit(h C.godbc_handle_t, errPtr **C.char) *C.char {
	obj, err := getHandle(Handle(h))
	if err != nil {
		*errPtr = C.CString(err.Error())
		return nil
	}

	tx, ok := obj.(*sql.Tx)
	if !ok {
		*errPtr = C.CString("Invalid transaction handle")
		return nil
	}

	if err := tx.Commit(); err != nil {
		*errPtr = C.CString(err.Error())
		return nil
	}

	removeHandle(Handle(h))
	return nil
}

//export GodbcRollback
func GodbcRollback(h C.godbc_handle_t, errPtr **C.char) *C.char {
	obj, err := getHandle(Handle(h))
	if err != nil {
		*errPtr = C.CString(err.Error())
		return nil
	}

	tx, ok := obj.(*sql.Tx)
	if !ok {
		*errPtr = C.CString("Invalid transaction handle")
		return nil
	}

	if err := tx.Rollback(); err != nil {
		*errPtr = C.CString(err.Error())
		return nil
	}

	removeHandle(Handle(h))
	return nil
}

//export GodbcPrepare
func GodbcPrepare(h C.godbc_handle_t, query *C.char, errPtr **C.char) C.godbc_handle_t {
	obj, err := getHandle(Handle(h))
	if err != nil {
		*errPtr = C.CString(err.Error())
		return 0
	}

	pool, ok := obj.(*Pool)
	if !ok {
		*errPtr = C.CString("Invalid connection handle")
		return 0
	}

	conn, err := pool.getConnectionWithRetry()
	if err != nil {
		*errPtr = C.CString(err.Error())
		return 0
	}

	stmt, err := conn.PrepareContext(context.Background(), C.GoString(query))
	if err != nil {
		conn.Close()
		*errPtr = C.CString(err.Error())
		return 0
	}

	return storeHandle(stmt)
}

//export GodbcExecutePrepared
func GodbcExecutePrepared(h C.godbc_handle_t, params **C.char, paramCount C.int, errPtr **C.char) *C.char {
	obj, err := getHandle(Handle(h))
	if err != nil {
		*errPtr = C.CString(err.Error())
		return nil
	}

	stmt, ok := obj.(*sql.Stmt)
	if !ok {
		*errPtr = C.CString("Invalid prepared statement handle")
		return nil
	}

	args := make([]interface{}, paramCount)
	for i := 0; i < int(paramCount); i++ {
		paramPtr := (**C.char)(unsafe.Pointer(uintptr(unsafe.Pointer(params)) + uintptr(i)*unsafe.Sizeof(*params)))
		args[i] = C.GoString(*paramPtr)
	}

	_, err = stmt.ExecContext(context.Background(), args...)
	if err != nil {
		*errPtr = C.CString(err.Error())
		return nil
	}

	return nil
}

//export GodbcClosePrepared
func GodbcClosePrepared(h C.godbc_handle_t, errPtr **C.char) *C.char {
	obj, err := getHandle(Handle(h))
	if err != nil {
		*errPtr = C.CString(err.Error())
		return nil
	}

	stmt, ok := obj.(*sql.Stmt)
	if !ok {
		*errPtr = C.CString("Invalid prepared statement handle")
		return nil
	}

	removeHandle(Handle(h))
	if err := stmt.Close(); err != nil {
		*errPtr = C.CString(err.Error())
		return nil
	}

	return nil
}

//export GodbcExecuteWithParams
func GodbcExecuteWithParams(h C.godbc_handle_t, query *C.char, params **C.char, paramCount C.int, errPtr **C.char) {
	mu.Lock()
	defer mu.Unlock()

	handle := Handle(h)
	if obj, exists := handles[handle]; exists {
		goParams := make([]interface{}, paramCount)
		for i := 0; i < int(paramCount); i++ {
			param := C.GoString(*(**C.char)(unsafe.Pointer(uintptr(unsafe.Pointer(params)) + uintptr(i)*unsafe.Sizeof(*params))))
			goParams[i] = param
		}

		switch v := obj.(type) {
		case *Pool:
			conn, err := v.getConnectionWithRetry()
			if err != nil {
				*errPtr = C.CString(err.Error())
				return
			}
			defer v.returnConnection(conn)
			_, err = conn.ExecContext(context.Background(), C.GoString(query), goParams...)
			if err != nil {
				*errPtr = C.CString(err.Error())
			}
		case *sql.Conn:
			_, err := v.ExecContext(context.Background(), C.GoString(query), goParams...)
			if err != nil {
				*errPtr = C.CString(err.Error())
			}
		case *sql.Tx:
			_, err := v.ExecContext(context.Background(), C.GoString(query), goParams...)
			if err != nil {
				*errPtr = C.CString(err.Error())
			}
		case *sql.Stmt:
			_, err := v.ExecContext(context.Background(), goParams...)
			if err != nil {
				*errPtr = C.CString(err.Error())
			}
		default:
			*errPtr = C.CString("invalid handle type")
		}
	} else {
		*errPtr = C.CString("invalid handle")
	}
}

//export GodbcReturnConnection
func GodbcReturnConnection(h C.godbc_handle_t, errPtr **C.char) {
	mu.Lock()
	defer mu.Unlock()

	handle := Handle(h)
	if obj, exists := handles[handle]; exists {
		switch v := obj.(type) {
		case *Pool:
			// Don't actually close the pool, just remove the handle
			delete(handles, handle)
		case *sql.Conn:
			// Find the pool this connection belongs to
			var foundPool *Pool
			for _, pool := range connPool {
				pool.mu.Lock()
				for _, conn := range pool.connections {
					if conn == v {
						foundPool = pool
						break
					}
				}
				pool.mu.Unlock()
				if foundPool != nil {
					break
				}
			}
			if foundPool != nil {
				foundPool.returnConnection(v)
			} else {
				v.Close()
			}
			delete(handles, handle)
		default:
			*errPtr = C.CString("invalid handle type")
			return
		}
	} else {
		*errPtr = C.CString("invalid handle")
		return
	}
}

func storeHandle(obj interface{}) C.godbc_handle_t {
	mu.Lock()
	defer mu.Unlock()

	// Check if the object is already stored
	for h, existing := range handles {
		if existing == obj {
			return C.godbc_handle_t(h)
		}
	}

	h := nextID
	nextID++
	handles[h] = obj
	return C.godbc_handle_t(h)
}

func getHandle(h Handle) (interface{}, error) {
	mu.RLock()
	defer mu.RUnlock()

	if obj, ok := handles[h]; ok {
		return obj, nil
	}
	return nil, fmt.Errorf("invalid handle")
}

func removeHandle(h Handle) {
	mu.Lock()
	defer mu.Unlock()
	delete(handles, h)
}

func setError(err error, errPtr **C.char) {
	*errPtr = C.CString(err.Error())
}

func main() {
}

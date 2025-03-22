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

	for i := 0; i < p.minConns; i++ {
		conn, err := p.db.Conn(context.Background())
		if err != nil {
			return fmt.Errorf("failed to preallocate connection: %v", err)
		}
		if err := conn.Close(); err != nil {
			return fmt.Errorf("failed to close preallocated connection: %v", err)
		}
		p.activeConns++
	}
	return nil
}

// getConnectionWithRetry attempts to get a connection with retries
func (p *Pool) getConnectionWithRetry() (*sql.Conn, error) {
	var lastErr error
	for attempt := 0; attempt < p.retryAttempts; attempt++ {
		if attempt > 0 {
			// Check if the error is a network-related error
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
			// Test the connection with a simple query
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
func GodbcConnect(connStr *C.char, minConns, maxConns C.int, connTimeoutMs, retryDelayMs, retryAttempts C.int, networkRetryDelaySecs C.int, verboseLogging C.int, errPtr **C.char) (C.godbc_handle_t, *C.char) {
	goConnStr := C.GoString(connStr)

	mu.Lock()
	if pool, exists := connPool[goConnStr]; exists {
		mu.Unlock()
		return C.godbc_handle_t(pool.db.Stats().OpenConnections), nil
	}
	mu.Unlock()

	db, err := sql.Open("mssql", goConnStr)
	if err != nil {
		*errPtr = C.CString(err.Error())
		return 0, nil
	}

	pool := &Pool{
		db:                db,
		minConns:          int(minConns),
		maxConns:          int(maxConns),
		connTimeout:       time.Duration(connTimeoutMs) * time.Millisecond,
		retryDelay:        time.Duration(retryDelayMs) * time.Millisecond,
		retryAttempts:     int(retryAttempts),
		networkRetryDelay: time.Duration(networkRetryDelaySecs) * time.Second,
		verboseLogging:    verboseLogging != 0,
	}

	db.SetMaxOpenConns(pool.maxConns)
	db.SetMaxIdleConns(pool.minConns)
	db.SetConnMaxLifetime(pool.connTimeout)

	// Preallocate connections with retry
	for i := 0; i < pool.minConns; i++ {
		conn, err := pool.getConnectionWithRetry()
		if err != nil {
			db.Close()
			*errPtr = C.CString(fmt.Sprintf("failed to preallocate connection %d: %v", i+1, err))
			return 0, nil
		}
		conn.Close()
	}

	mu.Lock()
	h := nextID
	nextID++
	handles[h] = pool
	connPool[goConnStr] = pool
	mu.Unlock()

	return C.godbc_handle_t(h), nil
}

//export GodbcClose
func GodbcClose(h C.godbc_handle_t, errPtr **C.char) {
	mu.Lock()
	defer mu.Unlock()

	if pool, ok := handles[Handle(h)].(*Pool); ok {
		if err := pool.db.Close(); err != nil {
			*errPtr = C.CString(err.Error())
			return
		}
		delete(handles, Handle(h))
		*errPtr = nil
	} else {
		*errPtr = C.CString("invalid handle")
	}
}

//export GodbcExecute
func GodbcExecute(h C.godbc_handle_t, query *C.char, errPtr **C.char) *C.char {
	obj, err := getHandle(Handle(h))
	if err != nil {
		*errPtr = C.CString(err.Error())
		return nil
	}

	switch v := obj.(type) {
	case *Pool:
		// Get a connection from the pool
		conn, err := v.getConnectionWithRetry()
		if err != nil {
			*errPtr = C.CString(err.Error())
			return nil
		}
		defer conn.Close()

		// Execute the query directly without a transaction
		if _, err := conn.ExecContext(context.Background(), C.GoString(query)); err != nil {
			*errPtr = C.CString(err.Error())
			return nil
		}
		return nil

	case *sql.Tx:
		// Execute within an existing transaction
		if _, err := v.ExecContext(context.Background(), C.GoString(query)); err != nil {
			*errPtr = C.CString(err.Error())
			return nil
		}
		return nil

	case *sql.Conn:
		// Execute directly on the connection
		if _, err := v.ExecContext(context.Background(), C.GoString(query)); err != nil {
			*errPtr = C.CString(err.Error())
			return nil
		}
		return nil

	default:
		*errPtr = C.CString("invalid handle type")
		return nil
	}
}

//export GodbcQuery
func GodbcQuery(h C.godbc_handle_t, query *C.char, errPtr **C.char) C.godbc_handle_t {
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

	// Remove the handle before closing to prevent race conditions
	removeHandle(Handle(h))

	// Close rows first
	if err := qr.rows.Close(); err != nil {
		// Even if rows.Close() fails, try to close the connection
		qr.conn.Close()
		*errPtr = C.CString(err.Error())
		return nil
	}

	// Then close the connection
	if err := qr.conn.Close(); err != nil {
		*errPtr = C.CString(err.Error())
		return nil
	}

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

func main() {
}

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
	"database/sql"
	"fmt"
	"sync"
	"time"
	"unsafe"

	_ "github.com/microsoft/go-mssqldb"
)

// Handle represents a database resource handle
type Handle uint64

var (
	mu       sync.RWMutex
	handles         = make(map[Handle]interface{})
	nextID   Handle = 1
	connPool        = make(map[string]*sql.DB)
)

//export GodbcConnect
func GodbcConnect(connStr *C.char, minConns, maxConns C.int, connTimeoutMs, retryDelayMs, retryAttempts C.int) (C.godbc_handle_t, *C.char) {
	goConnStr := C.GoString(connStr)
	db, err := sql.Open("mssql", goConnStr)
	if err != nil {
		errStr := C.CString(err.Error())
		return 0, errStr
	}

	db.SetMaxOpenConns(int(maxConns))
	db.SetMaxIdleConns(int(minConns))
	db.SetConnMaxLifetime(time.Duration(connTimeoutMs) * time.Millisecond)

	mu.Lock()
	h := nextID
	nextID++
	handles[h] = db
	connPool[goConnStr] = db
	mu.Unlock()

	return C.godbc_handle_t(h), nil
}

//export GodbcClose
func GodbcClose(h C.godbc_handle_t, error **C.char) {
	mu.Lock()
	defer mu.Unlock()

	if db, ok := handles[Handle(h)].(*sql.DB); ok {
		if err := db.Close(); err != nil {
			*error = C.CString(err.Error())
			return
		}
		delete(handles, Handle(h))
		*error = nil
	} else {
		*error = C.CString("invalid handle")
	}
}

//export GodbcExecute
func GodbcExecute(h C.godbc_handle_t, query *C.char, error **C.char) *C.char {
	obj, err := getHandle(Handle(h))
	if err != nil {
		*error = C.CString(err.Error())
		return nil
	}

	db, ok := obj.(*sql.DB)
	if !ok {
		*error = C.CString("Invalid connection handle")
		return nil
	}

	_, err = db.Exec(C.GoString(query))
	if err != nil {
		*error = C.CString(err.Error())
		return nil
	}

	return nil
}

//export GodbcQuery
func GodbcQuery(h C.godbc_handle_t, query *C.char, error **C.char) C.godbc_handle_t {
	obj, err := getHandle(Handle(h))
	if err != nil {
		*error = C.CString(err.Error())
		return 0
	}

	db, ok := obj.(*sql.DB)
	if !ok {
		*error = C.CString("Invalid connection handle")
		return 0
	}

	rows, err := db.Query(C.GoString(query))
	if err != nil {
		*error = C.CString(err.Error())
		return 0
	}

	return storeHandle(rows)
}

//export GodbcNext
func GodbcNext(h C.godbc_handle_t, error **C.char) C.int {
	obj, err := getHandle(Handle(h))
	if err != nil {
		*error = C.CString(err.Error())
		return -1
	}

	rows, ok := obj.(*sql.Rows)
	if !ok {
		*error = C.CString("Invalid result set handle")
		return -1
	}

	if rows.Next() {
		return 1
	}

	if err := rows.Err(); err != nil {
		*error = C.CString(err.Error())
		return -1
	}

	return 0
}

//export GodbcScan
func GodbcScan(h C.godbc_handle_t, values ***C.char, count C.int, error **C.char) *C.char {
	obj, err := getHandle(Handle(h))
	if err != nil {
		*error = C.CString(err.Error())
		return nil
	}

	rows, ok := obj.(*sql.Rows)
	if !ok {
		*error = C.CString("Invalid result set handle")
		return nil
	}

	rawValues := make([]interface{}, count)
	scanValues := make([]interface{}, count)
	for i := range rawValues {
		scanValues[i] = &rawValues[i]
	}

	if err := rows.Scan(scanValues...); err != nil {
		*error = C.CString(err.Error())
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
func GodbcCloseRows(h C.godbc_handle_t, error **C.char) *C.char {
	obj, err := getHandle(Handle(h))
	if err != nil {
		*error = C.CString(err.Error())
		return nil
	}

	rows, ok := obj.(*sql.Rows)
	if !ok {
		*error = C.CString("Invalid result set handle")
		return nil
	}

	removeHandle(Handle(h))
	if err := rows.Close(); err != nil {
		*error = C.CString(err.Error())
		return nil
	}

	return nil
}

//export GodbcBeginTransaction
func GodbcBeginTransaction(h C.godbc_handle_t, error **C.char) C.godbc_handle_t {
	obj, err := getHandle(Handle(h))
	if err != nil {
		*error = C.CString(err.Error())
		return 0
	}

	db, ok := obj.(*sql.DB)
	if !ok {
		*error = C.CString("Invalid connection handle")
		return 0
	}

	tx, err := db.Begin()
	if err != nil {
		*error = C.CString(err.Error())
		return 0
	}

	return storeHandle(tx)
}

//export GodbcExecuteInTransaction
func GodbcExecuteInTransaction(h C.godbc_handle_t, query *C.char, error **C.char) *C.char {
	obj, err := getHandle(Handle(h))
	if err != nil {
		*error = C.CString(err.Error())
		return nil
	}

	tx, ok := obj.(*sql.Tx)
	if !ok {
		*error = C.CString("Invalid transaction handle")
		return nil
	}

	_, err = tx.Exec(C.GoString(query))
	if err != nil {
		*error = C.CString(err.Error())
		return nil
	}

	return nil
}

//export GodbcCommit
func GodbcCommit(h C.godbc_handle_t, error **C.char) *C.char {
	obj, err := getHandle(Handle(h))
	if err != nil {
		*error = C.CString(err.Error())
		return nil
	}

	tx, ok := obj.(*sql.Tx)
	if !ok {
		*error = C.CString("Invalid transaction handle")
		return nil
	}

	removeHandle(Handle(h))
	if err := tx.Commit(); err != nil {
		*error = C.CString(err.Error())
		return nil
	}

	return nil
}

//export GodbcRollback
func GodbcRollback(h C.godbc_handle_t, error **C.char) *C.char {
	obj, err := getHandle(Handle(h))
	if err != nil {
		*error = C.CString(err.Error())
		return nil
	}

	tx, ok := obj.(*sql.Tx)
	if !ok {
		*error = C.CString("Invalid transaction handle")
		return nil
	}

	removeHandle(Handle(h))
	if err := tx.Rollback(); err != nil {
		*error = C.CString(err.Error())
		return nil
	}

	return nil
}

//export GodbcPrepare
func GodbcPrepare(h C.godbc_handle_t, query *C.char, error **C.char) C.godbc_handle_t {
	obj, err := getHandle(Handle(h))
	if err != nil {
		*error = C.CString(err.Error())
		return 0
	}

	db, ok := obj.(*sql.DB)
	if !ok {
		*error = C.CString("Invalid connection handle")
		return 0
	}

	stmt, err := db.Prepare(C.GoString(query))
	if err != nil {
		*error = C.CString(err.Error())
		return 0
	}

	return storeHandle(stmt)
}

//export GodbcExecutePrepared
func GodbcExecutePrepared(h C.godbc_handle_t, params **C.char, paramCount C.int, error **C.char) *C.char {
	obj, err := getHandle(Handle(h))
	if err != nil {
		*error = C.CString(err.Error())
		return nil
	}

	stmt, ok := obj.(*sql.Stmt)
	if !ok {
		*error = C.CString("Invalid statement handle")
		return nil
	}

	paramsSlice := (*[1 << 30]*C.char)(unsafe.Pointer(params))
	goParams := make([]interface{}, paramCount)
	for i := 0; i < int(paramCount); i++ {
		goParams[i] = C.GoString(paramsSlice[i])
	}

	_, err = stmt.Exec(goParams...)
	if err != nil {
		*error = C.CString(err.Error())
		return nil
	}

	return nil
}

//export GodbcClosePrepared
func GodbcClosePrepared(h C.godbc_handle_t, error **C.char) *C.char {
	obj, err := getHandle(Handle(h))
	if err != nil {
		*error = C.CString(err.Error())
		return nil
	}

	stmt, ok := obj.(*sql.Stmt)
	if !ok {
		*error = C.CString("Invalid statement handle")
		return nil
	}

	removeHandle(Handle(h))
	if err := stmt.Close(); err != nil {
		*error = C.CString(err.Error())
		return nil
	}

	return nil
}

func storeHandle(obj interface{}) C.godbc_handle_t {
	mu.Lock()
	defer mu.Unlock()
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

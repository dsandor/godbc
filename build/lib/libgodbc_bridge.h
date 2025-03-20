/* Code generated by cmd/cgo; DO NOT EDIT. */

/* package command-line-arguments */


#line 1 "cgo-builtin-export-prolog"

#include <stddef.h>

#ifndef GO_CGO_EXPORT_PROLOGUE_H
#define GO_CGO_EXPORT_PROLOGUE_H

#ifndef GO_CGO_GOSTRING_TYPEDEF
typedef struct { const char *p; ptrdiff_t n; } _GoString_;
#endif

#endif

/* Start of preamble from import "C" comments.  */


#line 6 "bridge.go"

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

#line 1 "cgo-generated-wrapper"


/* End of preamble from import "C" comments.  */


/* Start of boilerplate cgo prologue.  */
#line 1 "cgo-gcc-export-header-prolog"

#ifndef GO_CGO_PROLOGUE_H
#define GO_CGO_PROLOGUE_H

typedef signed char GoInt8;
typedef unsigned char GoUint8;
typedef short GoInt16;
typedef unsigned short GoUint16;
typedef int GoInt32;
typedef unsigned int GoUint32;
typedef long long GoInt64;
typedef unsigned long long GoUint64;
typedef GoInt64 GoInt;
typedef GoUint64 GoUint;
typedef size_t GoUintptr;
typedef float GoFloat32;
typedef double GoFloat64;
#ifdef _MSC_VER
#include <complex.h>
typedef _Fcomplex GoComplex64;
typedef _Dcomplex GoComplex128;
#else
typedef float _Complex GoComplex64;
typedef double _Complex GoComplex128;
#endif

/*
  static assertion to make sure the file is being used on architecture
  at least with matching size of GoInt.
*/
typedef char _check_for_64_bit_pointer_matching_GoInt[sizeof(void*)==64/8 ? 1:-1];

#ifndef GO_CGO_GOSTRING_TYPEDEF
typedef _GoString_ GoString;
#endif
typedef void *GoMap;
typedef void *GoChan;
typedef struct { void *t; void *v; } GoInterface;
typedef struct { void *data; GoInt len; GoInt cap; } GoSlice;

#endif

/* End of boilerplate cgo prologue.  */

#ifdef __cplusplus
extern "C" {
#endif


/* Return type for GodbcConnect */
struct GodbcConnect_return {
	godbc_handle_t r0;
	char* r1;
};
extern struct GodbcConnect_return GodbcConnect(char* connStr, int minConns, int maxConns, int connTimeoutMs, int retryDelayMs, int retryAttempts);
extern void GodbcClose(godbc_handle_t h, char** error);
extern char* GodbcExecute(godbc_handle_t h, char* query, char** error);
extern godbc_handle_t GodbcQuery(godbc_handle_t h, char* query, char** error);
extern int GodbcNext(godbc_handle_t h, char** error);
extern char* GodbcScan(godbc_handle_t h, char*** values, int count, char** error);
extern char* GodbcCloseRows(godbc_handle_t h, char** error);
extern godbc_handle_t GodbcBeginTransaction(godbc_handle_t h, char** error);
extern char* GodbcExecuteInTransaction(godbc_handle_t h, char* query, char** error);
extern char* GodbcCommit(godbc_handle_t h, char** error);
extern char* GodbcRollback(godbc_handle_t h, char** error);
extern godbc_handle_t GodbcPrepare(godbc_handle_t h, char* query, char** error);
extern char* GodbcExecutePrepared(godbc_handle_t h, char** params, int paramCount, char** error);
extern char* GodbcClosePrepared(godbc_handle_t h, char** error);

#ifdef __cplusplus
}
#endif

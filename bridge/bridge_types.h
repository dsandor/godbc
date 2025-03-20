#ifndef BRIDGE_TYPES_H
#define BRIDGE_TYPES_H

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

// Basic types
typedef uint64_t GodbcHandle;
typedef int GoInt;
typedef char* GoString;
typedef char* char_ptr;
typedef const char* const_char_ptr;

// String handling functions
static inline char* CString(const char* str) {
    if (str == NULL) return NULL;
    size_t len = strlen(str);
    char* result = (char*)malloc(len + 1);
    if (result != NULL) {
        strcpy(result, str);
    }
    return result;
}

static inline void FreeCString(char* str) {
    if (str != NULL) {
        free(str);
    }
}

static inline const char* GoString_str(const char* str) {
    return str;
}

// Memory management
static inline void* mallocWrapper(size_t size) {
    return malloc(size);
}

static inline void freeWrapper(void* ptr) {
    if (ptr != NULL) {
        free(ptr);
    }
}

// Function declarations
GodbcHandle GodbcConnect(const char* connStr, int minConns, int maxConns, 
                        int connTimeoutMs, int retryDelayMs, int retryAttempts, 
                        char** error);
void GodbcClose(GodbcHandle h, char** error);
void GodbcExecute(GodbcHandle h, const char* query, char** error);
GodbcHandle GodbcQuery(GodbcHandle h, const char* query, char** error);
int GodbcNext(GodbcHandle h, char** error);
void GodbcScan(GodbcHandle h, char*** values, int count, char** error);
void GodbcCloseRows(GodbcHandle h, char** error);
GodbcHandle GodbcBeginTransaction(GodbcHandle h, char** error);
void GodbcExecuteInTransaction(GodbcHandle h, const char* query, char** error);
void GodbcCommit(GodbcHandle h, char** error);
void GodbcRollback(GodbcHandle h, char** error);
GodbcHandle GodbcPrepare(GodbcHandle h, const char* query, char** error);
void GodbcExecutePrepared(GodbcHandle h, char** params, int paramCount, char** error);
void GodbcClosePrepared(GodbcHandle h, char** error);

#endif /* BRIDGE_TYPES_H */

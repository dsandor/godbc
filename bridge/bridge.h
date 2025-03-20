#ifndef GODBC_BRIDGE_H
#define GODBC_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef unsigned long long godbc_handle;

// Error handling
typedef struct {
    char* message;
} godbc_error;

typedef struct {
    godbc_handle handle;
    godbc_error* error;
} godbc_result;

// Connection functions
godbc_handle GodbcConnect(const char* connStr, int minConns, int maxConns, 
                         int connTimeoutMs, int retryDelayMs, int retryAttempts, int networkRetryDelaySecs, int verboseLogging, char** errPtr);
char* GodbcClose(godbc_handle handle, char** errPtr);

// Query execution
char* GodbcExecute(godbc_handle handle, const char* query, char** errPtr);
godbc_handle GodbcQuery(godbc_handle handle, const char* query, char** errPtr);
int GodbcNext(godbc_handle handle, char** errPtr);
char* GodbcScan(godbc_handle handle, char*** values, int count, char** errPtr);
char* GodbcCloseRows(godbc_handle handle, char** errPtr);

// Transaction management
godbc_handle GodbcBeginTransaction(godbc_handle handle, char** errPtr);
char* GodbcExecuteInTransaction(godbc_handle handle, const char* query, char** errPtr);
char* GodbcExecuteInTransactionWithParams(godbc_handle handle, const char* query, char** params, int paramCount, char** errPtr);
char* GodbcCommit(godbc_handle handle, char** errPtr);
char* GodbcRollback(godbc_handle handle, char** errPtr);

// Prepared statements
godbc_handle GodbcPrepare(godbc_handle handle, const char* query, char** errPtr);
char* GodbcExecutePrepared(godbc_handle handle, char** params, int paramCount, char** errPtr);
char* GodbcClosePrepared(godbc_handle handle, char** errPtr);

#ifdef __cplusplus
}
#endif

#endif // GODBC_BRIDGE_H

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
                         int connTimeoutMs, int retryDelayMs, int retryAttempts,
                         char** error);
char* GodbcClose(godbc_handle handle, char** error);

// Query execution
char* GodbcExecute(godbc_handle handle, const char* query, char** error);
godbc_handle GodbcQuery(godbc_handle handle, const char* query, char** error);
int GodbcNext(godbc_handle handle, char** error);
char* GodbcScan(godbc_handle handle, char*** values, int count, char** error);
char* GodbcCloseRows(godbc_handle handle, char** error);

// Transaction management
godbc_handle GodbcBeginTransaction(godbc_handle handle, char** error);
char* GodbcExecuteInTransaction(godbc_handle handle, const char* query, char** error);
char* GodbcCommit(godbc_handle handle, char** error);
char* GodbcRollback(godbc_handle handle, char** error);

// Prepared statements
godbc_handle GodbcPrepare(godbc_handle handle, const char* query, char** error);
char* GodbcExecutePrepared(godbc_handle handle, char** params, int paramCount, char** error);
char* GodbcClosePrepared(godbc_handle handle, char** error);

#ifdef __cplusplus
}
#endif

#endif // GODBC_BRIDGE_H

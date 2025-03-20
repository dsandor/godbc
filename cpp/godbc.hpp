#pragma once

#include <string>
#include <memory>
#include <stdexcept>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <sstream>

extern "C" {
#include "bridge/bridge.h"
}

namespace godbc {

class Error : public std::runtime_error {
public:
    explicit Error(const char* message) : std::runtime_error(message) {}
    explicit Error(const std::string& message) : std::runtime_error(message) {}
};

class ResultSet {
private:
    godbc_handle handle_;

public:
    explicit ResultSet(godbc_handle handle) : handle_(handle) {}
    ~ResultSet() noexcept(false) {
        if (handle_) {
            char* errPtr = nullptr;
            GodbcCloseRows(handle_, &errPtr);
            if (errPtr) {
                std::string err(errPtr);
                free(errPtr);
                throw Error(err);
            }
        }
    }

    bool next() const {
        char* errPtr = nullptr;
        int result = GodbcNext(handle_, &errPtr);
        if (errPtr) {
            std::string err(errPtr);
            free(errPtr);
            throw Error(err);
        }
        return result == 1;
    }

    void scan(std::vector<std::string>& values) {
        char** row = nullptr;
        char* errPtr = nullptr;
        size_t numColumns = values.size();
        GodbcScan(handle_, &row, static_cast<int>(numColumns), &errPtr);
        if (errPtr) {
            std::string err(errPtr);
            free(errPtr);
            throw Error(err);
        }

        values.clear();
        values.reserve(numColumns);
        for (size_t i = 0; i < numColumns; ++i) {
            values.emplace_back(row[i]);
            free(row[i]);
        }
        free(row);
    }
};

class PreparedStatement {
private:
    godbc_handle handle_;

    template<typename T>
    static std::string toString(const T& value) {
        std::ostringstream ss;
        ss << value;
        return ss.str();
    }

    // Specialization for string types to add quotes and escape single quotes
    template<>
    std::string toString<std::string>(const std::string& value) {
        std::string escaped = value;
        size_t pos = 0;
        while ((pos = escaped.find('\'', pos)) != std::string::npos) {
            escaped.replace(pos, 1, "''");
            pos += 2;
        }
        return "'" + escaped + "'";
    }

    template<>
    std::string toString<const char*>(const char* const& value) {
        return toString(std::string(value));
    }

public:
    explicit PreparedStatement(godbc_handle handle) : handle_(handle) {}
    ~PreparedStatement() noexcept(false) {
        if (handle_) {
            char* errPtr = nullptr;
            GodbcClosePrepared(handle_, &errPtr);
            if (errPtr) {
                std::string err(errPtr);
                free(errPtr);
                throw Error(err);
            }
        }
    }

    template<typename... Args>
    void execute(const Args&... args) {
        std::vector<std::string> params;
        params.reserve(sizeof...(args));
        (params.push_back(toString(args)), ...);

        std::vector<char*> cParams;
        cParams.reserve(params.size());
        for (const auto& p : params) {
            cParams.push_back(const_cast<char*>(p.c_str()));
        }

        char* errPtr = nullptr;
        GodbcExecutePrepared(handle_, cParams.data(), static_cast<int>(cParams.size()), &errPtr);
        if (errPtr) {
            std::string err(errPtr);
            free(errPtr);
            throw Error(err);
        }
    }
};

class Transaction {
private:
    godbc_handle handle_;
    bool committed_;
    bool rolledBack_;

    template<typename T>
    static std::string toString(const T& value) {
        std::ostringstream ss;
        ss << value;
        return ss.str();
    }

    // Specialization for string types to add quotes and escape single quotes
    template<>
    std::string toString<std::string>(const std::string& value) {
        std::string escaped = value;
        size_t pos = 0;
        while ((pos = escaped.find('\'', pos)) != std::string::npos) {
            escaped.replace(pos, 1, "''");
            pos += 2;
        }
        return "'" + escaped + "'";
    }

    template<>
    std::string toString<const char*>(const char* const& value) {
        return toString(std::string(value));
    }

public:
    explicit Transaction(godbc_handle handle) : handle_(handle), committed_(false), rolledBack_(false) {}
    ~Transaction() noexcept(false) {
        if (!committed_ && !rolledBack_) {
            rollback();
        }
    }

    void execute(const std::string& query) {
        char* errPtr = nullptr;
        GodbcExecuteInTransaction(handle_, query.c_str(), &errPtr);
        if (errPtr) {
            std::string err(errPtr);
            free(errPtr);
            throw Error(err);
        }
    }

    template<typename... Args>
    void execute(const std::string& query, const Args&... args) {
        std::vector<std::string> params;
        params.reserve(sizeof...(args));
        (params.push_back(toString(args)), ...);

        std::vector<char*> cParams;
        cParams.reserve(params.size());
        for (const auto& p : params) {
            cParams.push_back(const_cast<char*>(p.c_str()));
        }

        char* errPtr = nullptr;
        GodbcExecuteInTransactionWithParams(handle_, query.c_str(), cParams.data(), static_cast<int>(cParams.size()), &errPtr);
        if (errPtr) {
            std::string err(errPtr);
            free(errPtr);
            throw Error(err);
        }
    }

    void commit() {
        char* errPtr = nullptr;
        GodbcCommit(handle_, &errPtr);
        if (errPtr) {
            std::string err(errPtr);
            free(errPtr);
            throw Error(err);
        }
        committed_ = true;
    }

    void rollback() {
        if (!rolledBack_) {
            char* errPtr = nullptr;
            GodbcRollback(handle_, &errPtr);
            if (errPtr) {
                std::string err(errPtr);
                free(errPtr);
                throw Error(err);
            }
            rolledBack_ = true;
        }
    }
};

class Connection {
private:
    godbc_handle handle_;

    template<typename T>
    static std::string toString(const T& value) {
        std::ostringstream ss;
        ss << value;
        return ss.str();
    }

    // Specialization for string types to add quotes and escape single quotes
    template<>
    std::string toString<std::string>(const std::string& value) {
        std::string escaped = value;
        size_t pos = 0;
        while ((pos = escaped.find('\'', pos)) != std::string::npos) {
            escaped.replace(pos, 1, "''");
            pos += 2;
        }
        return "'" + escaped + "'";
    }

    template<>
    std::string toString<const char*>(const char* const& value) {
        return toString(std::string(value));
    }

public:
    explicit Connection(godbc_handle handle) : handle_(handle) {}
    ~Connection() noexcept(false) {
        if (handle_) {
            char* errPtr = nullptr;
            GodbcClose(handle_, &errPtr);
            if (errPtr) {
                std::string err(errPtr);
                free(errPtr);
                throw Error(err);
            }
        }
    }

    void execute(const std::string& query) const {
        char* errPtr = nullptr;
        GodbcExecute(handle_, query.c_str(), &errPtr);
        if (errPtr) {
            std::string err(errPtr);
            free(errPtr);
            throw Error(err);
        }
    }

    template<typename... Args>
    void execute(const std::string& query, const Args&... args) const {
        std::vector<std::string> params;
        params.reserve(sizeof...(args));
        (params.push_back(toString(args)), ...);

        std::string finalQuery = query;
        for (const auto& param : params) {
            size_t pos = finalQuery.find('?');
            if (pos != std::string::npos) {
                finalQuery.replace(pos, 1, param);
            }
        }

        execute(finalQuery);
    }

    ResultSet query(const std::string& query) const {
        char* errPtr = nullptr;
        godbc_handle handle = GodbcQuery(handle_, query.c_str(), &errPtr);
        if (errPtr) {
            std::string err(errPtr);
            free(errPtr);
            throw Error(err);
        }
        return ResultSet(handle);
    }

    template<typename... Args>
    ResultSet query(const std::string& query, const Args&... args) const {
        std::vector<std::string> params;
        params.reserve(sizeof...(args));
        (params.push_back(toString(args)), ...);

        std::string finalQuery = query;
        for (const auto& param : params) {
            size_t pos = finalQuery.find('?');
            if (pos != std::string::npos) {
                finalQuery.replace(pos, 1, param);
            }
        }

        return this->query(finalQuery);
    }

    Transaction beginTransaction() const {
        char* errPtr = nullptr;
        godbc_handle txHandle = GodbcBeginTransaction(handle_, &errPtr);
        if (errPtr) {
            std::string err(errPtr);
            free(errPtr);
            throw Error(err);
        }
        return Transaction(txHandle);
    }

    PreparedStatement prepare(const std::string& query) const {
        char* errPtr = nullptr;
        godbc_handle stmtHandle = GodbcPrepare(handle_, query.c_str(), &errPtr);
        if (errPtr) {
            std::string err(errPtr);
            free(errPtr);
            throw Error(err);
        }
        return PreparedStatement(stmtHandle);
    }
};

class ConnectionPool {
private:
    std::string connStr_;
    int minConns_;
    int maxConns_;
    int connTimeoutMs_;
    int retryDelayMs_;
    int retryAttempts_;
    int networkRetryDelaySecs_;
    bool verboseLogging_;
    godbc_handle handle_;

public:
    ConnectionPool(const std::string& connStr, int minConns = 1, int maxConns = 10,
                  int connTimeoutMs = 30000, int retryDelayMs = 1000, int retryAttempts = 3,
                  int networkRetryDelaySecs = 1, bool verboseLogging = false)
        : connStr_(connStr), minConns_(minConns), maxConns_(maxConns),
          connTimeoutMs_(connTimeoutMs), retryDelayMs_(retryDelayMs),
          retryAttempts_(retryAttempts), networkRetryDelaySecs_(networkRetryDelaySecs),
          verboseLogging_(verboseLogging), handle_(0) {
        char* err = nullptr;
        handle_ = GodbcConnect(connStr_.c_str(), minConns_, maxConns_,
                             connTimeoutMs_, retryDelayMs_, retryAttempts_,
                             networkRetryDelaySecs_, verboseLogging_ ? 1 : 0, &err);
        if (err) {
            std::string error(err);
            free(err);
            throw std::runtime_error("Failed to connect: " + error);
        }
    }

    ~ConnectionPool() {
        if (handle_) {
            char* err = nullptr;
            GodbcClose(handle_, &err);
            if (err) {
                free(err);
            }
        }
    }

    static Connection getConnection(const std::string& connStr, 
                                    int minConns = 1, int maxConns = 10,
                                    int connTimeoutMs = 30000, int retryDelayMs = 1000,
                                    int retryAttempts = 3, int networkRetryDelaySecs = 1,
                                    bool verboseLogging = false) {
        char* err = nullptr;
        godbc_handle handle = GodbcConnect(connStr.c_str(), minConns, maxConns,
                                         connTimeoutMs, retryDelayMs, retryAttempts,
                                         networkRetryDelaySecs, verboseLogging ? 1 : 0, &err);
        if (err) {
            std::string error(err);
            free(err);
            throw std::runtime_error("Failed to connect: " + error);
        }
        return Connection(handle);
    }
};

} // namespace godbc

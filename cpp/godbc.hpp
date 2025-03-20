#ifndef GODBC_HPP
#define GODBC_HPP

#include <string>
#include <memory>
#include <stdexcept>
#include <vector>
#include "../bridge/bridge.h"

namespace godbc {

class Error : public std::runtime_error {
public:
    explicit Error(const char* message) : std::runtime_error(message) {}
    explicit Error(const std::string& message) : std::runtime_error(message) {}
};

class ResultSet;
class Connection;
class Transaction;
class PreparedStatement;

class ConnectionPool {
public:
    static Connection getConnection(const std::string& connStr, 
                                  int minConns = 1, int maxConns = 10,
                                  int connTimeoutMs = 30000, 
                                  int retryDelayMs = 1000, 
                                  int retryAttempts = 3);
};

class PreparedStatement {
public:
    ~PreparedStatement() noexcept(false) {
        if (handle_) {
            char* error = nullptr;
            GodbcClosePrepared(handle_, &error);
            if (error) {
                std::string err(error);
                free(error);
                throw Error(err);
            }
        }
    }

    template<typename... Args>
    void execute(const Args&... args) {
        std::vector<std::string> params;
        params.reserve(sizeof...(args));
        int dummy[] = { (params.push_back(toString(args)), 0)... };
        (void)dummy; // Suppress unused variable warning
        
        std::vector<char*> cParams;
        cParams.reserve(params.size());
        for (const auto& p : params) {
            cParams.push_back(const_cast<char*>(p.c_str()));
        }

        char* error = nullptr;
        GodbcExecutePrepared(handle_, cParams.data(), static_cast<int>(cParams.size()), &error);
        if (error) {
            std::string err(error);
            free(error);
            throw Error(err);
        }
    }

private:
    friend class Connection;
    PreparedStatement(godbc_handle handle) : handle_(handle) {}
    godbc_handle handle_;

    template<typename T>
    static std::string toString(const T& value) {
        return std::to_string(value);
    }

    static std::string toString(const std::string& value) {
        return value;
    }

    static std::string toString(const char* value) {
        return std::string(value);
    }
};

class Transaction {
public:
    ~Transaction() noexcept(false) {
        if (!committed_ && !rolledBack_) {
            rollback();
        }
    }

    void execute(const std::string& query) {
        char* error = nullptr;
        GodbcExecuteInTransaction(handle_, query.c_str(), &error);
        if (error) {
            std::string err(error);
            free(error);
            throw Error(err);
        }
    }

    void commit() {
        char* error = nullptr;
        GodbcCommit(handle_, &error);
        if (error) {
            std::string err(error);
            free(error);
            throw Error(err);
        }
        committed_ = true;
    }

    void rollback() {
        if (!rolledBack_) {
            char* error = nullptr;
            GodbcRollback(handle_, &error);
            if (error) {
                std::string err(error);
                free(error);
                throw Error(err);
            }
            rolledBack_ = true;
        }
    }

private:
    friend class Connection;
    Transaction(godbc_handle handle) : handle_(handle), committed_(false), rolledBack_(false) {}
    godbc_handle handle_;
    bool committed_;
    bool rolledBack_;
};

class Connection {
public:
    ~Connection() noexcept(false) {
        if (handle_) {
            char* error = nullptr;
            GodbcClose(handle_, &error);
            if (error) {
                std::string err(error);
                free(error);
                throw Error(err);
            }
        }
    }

    void execute(const std::string& query) const {
        char* error = nullptr;
        GodbcExecute(handle_, query.c_str(), &error);
        if (error) {
            std::string err(error);
            free(error);
            throw Error(err);
        }
    }

    std::unique_ptr<ResultSet> query(const std::string& query) const;

    Transaction beginTransaction() const {
        char* error = nullptr;
        godbc_handle txHandle = GodbcBeginTransaction(handle_, &error);
        if (error) {
            std::string err(error);
            free(error);
            throw Error(err);
        }
        return Transaction(txHandle);
    }

    PreparedStatement prepare(const std::string& query) const {
        char* error = nullptr;
        godbc_handle stmtHandle = GodbcPrepare(handle_, query.c_str(), &error);
        if (error) {
            std::string err(error);
            free(error);
            throw Error(err);
        }
        return PreparedStatement(stmtHandle);
    }

private:
    friend class ConnectionPool;
    Connection(godbc_handle handle) : handle_(handle) {}
    godbc_handle handle_;
    friend class ResultSet;
};

Connection ConnectionPool::getConnection(const std::string& connStr, 
                                      int minConns, int maxConns,
                                      int connTimeoutMs, 
                                      int retryDelayMs, 
                                      int retryAttempts) {
    char* error = nullptr;
    godbc_handle handle = GodbcConnect(connStr.c_str(), minConns, maxConns, 
                                     connTimeoutMs, retryDelayMs, retryAttempts,
                                     &error);
    if (error) {
        std::string err(error);
        free(error);
        throw Error(err);
    }
    return Connection(handle);
}

class ResultSet {
public:
    friend class Connection;

    ~ResultSet() noexcept(false) {
        if (handle_) {
            char* error = nullptr;
            GodbcCloseRows(handle_, &error);
            if (error) {
                std::string err(error);
                free(error);
                throw Error(err);
            }
        }
    }

    bool next() const {
        char* error = nullptr;
        int result = GodbcNext(handle_, &error);
        if (error) {
            std::string err(error);
            free(error);
            throw Error(err);
        }
        return result == 1;
    }

    std::vector<std::string> getRow(int columnCount) const {
        char** values = nullptr;
        char* error = nullptr;
        GodbcScan(handle_, &values, columnCount, &error);
        if (error) {
            std::string err(error);
            free(error);
            throw Error(err);
        }

        std::vector<std::string> result;
        result.reserve(columnCount);
        for (int i = 0; i < columnCount; ++i) {
            result.emplace_back(values[i]);
            free(values[i]);
        }
        free(values);
        return result;
    }

private:
    ResultSet(godbc_handle handle) : handle_(handle) {}
    godbc_handle handle_;
};

std::unique_ptr<ResultSet> Connection::query(const std::string& query) const {
    char* error = nullptr;
    godbc_handle handle = GodbcQuery(handle_, query.c_str(), &error);
    if (error) {
        std::string err(error);
        free(error);
        throw Error(err);
    }
    return std::unique_ptr<ResultSet>(new ResultSet(handle));
}

} // namespace godbc

#endif // GODBC_HPP

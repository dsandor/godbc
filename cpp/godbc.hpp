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
    bool closed_;

public:
    explicit ResultSet(godbc_handle handle) : handle_(handle), closed_(false) {}
    ~ResultSet() noexcept {
        if (!closed_) {
            try {
                close();
            } catch (...) {
                // Ignore errors in destructor
            }
        }
    }

    ResultSet(const ResultSet&) = delete;
    ResultSet& operator=(const ResultSet&) = delete;
    
    ResultSet(ResultSet&& other) noexcept : handle_(other.handle_), closed_(other.closed_) {
        other.handle_ = 0;
        other.closed_ = true;
    }
    
    ResultSet& operator=(ResultSet&& other) noexcept {
        if (this != &other) {
            if (!closed_) {
                try {
                    close();
                } catch (...) {
                    // Ignore errors in move assignment
                }
            }
            handle_ = other.handle_;
            closed_ = other.closed_;
            other.handle_ = 0;
            other.closed_ = true;
        }
        return *this;
    }

    void close() {
        if (!closed_ && handle_) {
            char* errPtr = nullptr;
            GodbcCloseRows(handle_, &errPtr);
            if (errPtr) {
                std::string err(errPtr);
                free(errPtr);
                closed_ = true;
                handle_ = 0;
                throw Error(err);
            }
            closed_ = true;
            handle_ = 0;
        }
    }

    bool next() {
        if (closed_) {
            throw Error("Result set is closed");
        }
        char* errPtr = nullptr;
        int result = GodbcNext(handle_, &errPtr);
        if (errPtr) {
            std::string err(errPtr);
            free(errPtr);
            throw Error(err);
        }
        if (result == 0) {
            close();
        }
        return result == 1;
    }

    void scan(std::vector<std::string>& values) {
        if (closed_) {
            throw Error("Result set is closed");
        }
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

// Helper function for string conversion
template<typename T>
std::string toString(const T& value) {
    std::ostringstream ss;
    ss << value;
    return ss.str();
}

// Specialization for string types
template<>
std::string toString<std::string>(const std::string& value) {
    std::string escaped = value;
    // Check if this is a LIKE pattern (contains % or _)
    bool isLikePattern = (value.find('%') != std::string::npos || value.find('_') != std::string::npos);
    
    // Escape single quotes
    size_t pos = 0;
    while ((pos = escaped.find('\'', pos)) != std::string::npos) {
        escaped.replace(pos, 1, "''");
        pos += 2;
    }

    // For LIKE patterns, we need to escape [ and ] characters
    if (isLikePattern) {
        pos = 0;
        while ((pos = escaped.find('[', pos)) != std::string::npos) {
            escaped.replace(pos, 1, "[[]");
            pos += 3;
        }
        pos = 0;
        while ((pos = escaped.find(']', pos)) != std::string::npos) {
            escaped.replace(pos, 1, "[]]");
            pos += 3;
        }
    }

    return "'" + escaped + "'";
}

// Specialization for const char*
template<>
std::string toString<const char*>(const char* const& value) {
    return toString(std::string(value));
}

class PreparedStatement {
private:
    godbc_handle handle_;
    bool closed_;

public:
    explicit PreparedStatement(godbc_handle handle) : handle_(handle), closed_(false) {}
    ~PreparedStatement() noexcept {
        if (!closed_) {
            try {
                close();
            } catch (...) {
                // Ignore errors in destructor
            }
        }
    }

    PreparedStatement(const PreparedStatement&) = delete;
    PreparedStatement& operator=(const PreparedStatement&) = delete;
    
    PreparedStatement(PreparedStatement&& other) noexcept : handle_(other.handle_), closed_(other.closed_) {
        other.handle_ = 0;
        other.closed_ = true;
    }
    
    PreparedStatement& operator=(PreparedStatement&& other) noexcept {
        if (this != &other) {
            if (!closed_) {
                try {
                    close();
                } catch (...) {
                    // Ignore errors in move assignment
                }
            }
            handle_ = other.handle_;
            closed_ = other.closed_;
            other.handle_ = 0;
            other.closed_ = true;
        }
        return *this;
    }

    void close() {
        if (!closed_ && handle_) {
            char* errPtr = nullptr;
            GodbcClosePrepared(handle_, &errPtr);
            if (errPtr) {
                std::string err(errPtr);
                free(errPtr);
                closed_ = true;
                handle_ = 0;
                throw Error(err);
            }
            closed_ = true;
            handle_ = 0;
        }
    }

    template<typename... Args>
    void execute(const Args&... args) {
        if (closed_) {
            throw Error("Prepared statement is closed");
        }
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
    bool closed_;

public:
    explicit Transaction(godbc_handle handle) : handle_(handle), closed_(false) {}
    ~Transaction() noexcept {
        if (!closed_) {
            try {
                close();
            } catch (...) {
                // Ignore errors in destructor
            }
        }
    }

    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    
    Transaction(Transaction&& other) noexcept : handle_(other.handle_), closed_(other.closed_) {
        other.handle_ = 0;
        other.closed_ = true;
    }
    
    Transaction& operator=(Transaction&& other) noexcept {
        if (this != &other) {
            if (!closed_) {
                try {
                    close();
                } catch (...) {
                    // Ignore errors in move assignment
                }
            }
            handle_ = other.handle_;
            closed_ = other.closed_;
            other.handle_ = 0;
            other.closed_ = true;
        }
        return *this;
    }

    void close() {
        if (!closed_ && handle_) {
            char* errPtr = nullptr;
            GodbcRollback(handle_, &errPtr);
            if (errPtr) {
                std::string err(errPtr);
                free(errPtr);
                closed_ = true;
                handle_ = 0;
                throw Error(err);
            }
            closed_ = true;
            handle_ = 0;
        }
    }

    template<typename... Args>
    void execute(const std::string& query, const Args&... args) {
        if (closed_) {
            throw Error("Transaction is closed");
        }
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
        if (closed_) {
            throw Error("Transaction is closed");
        }
        char* errPtr = nullptr;
        GodbcCommit(handle_, &errPtr);
        if (errPtr) {
            std::string err(errPtr);
            free(errPtr);
            closed_ = true;
            handle_ = 0;
            throw Error(err);
        }
        closed_ = true;
        handle_ = 0;
    }

    void rollback() {
        if (closed_) {
            throw Error("Transaction is closed");
        }
        char* errPtr = nullptr;
        GodbcRollback(handle_, &errPtr);
        if (errPtr) {
            std::string err(errPtr);
            free(errPtr);
            closed_ = true;
            handle_ = 0;
            throw Error(err);
        }
        closed_ = true;
        handle_ = 0;
    }
};

class Connection {
private:
    godbc_handle handle_;
    bool closed_;

public:
    explicit Connection(godbc_handle handle) : handle_(handle), closed_(false) {}
    
    Connection(const std::string& connStr, int minConns = 1, int maxConns = 10,
               int connTimeoutMs = 30000, int retryDelayMs = 1000, int retryAttempts = 3,
               int networkRetryDelaySecs = 5, bool verboseLogging = false)
        : handle_(0), closed_(false) {
        char* errPtr = nullptr;
        handle_ = GodbcConnect(connStr.c_str(), minConns, maxConns, connTimeoutMs,
                             retryDelayMs, retryAttempts, networkRetryDelaySecs,
                             verboseLogging ? 1 : 0, &errPtr);
        if (errPtr) {
            std::string err(errPtr);
            free(errPtr);
            throw Error(err);
        }
    }

    ~Connection() noexcept {
        if (!closed_) {
            try {
                close();
            } catch (...) {
                // Ignore errors in destructor
            }
        }
    }

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    
    Connection(Connection&& other) noexcept : handle_(other.handle_), closed_(other.closed_) {
        other.handle_ = 0;
        other.closed_ = true;
    }
    
    Connection& operator=(Connection&& other) noexcept {
        if (this != &other) {
            if (!closed_) {
                try {
                    close();
                } catch (...) {
                    // Ignore errors in move assignment
                }
            }
            handle_ = other.handle_;
            closed_ = other.closed_;
            other.handle_ = 0;
            other.closed_ = true;
        }
        return *this;
    }

    void close() {
        if (!closed_ && handle_) {
            char* errPtr = nullptr;
            GodbcReturnConnection(handle_, &errPtr);
            if (errPtr) {
                std::string err(errPtr);
                free(errPtr);
                closed_ = true;
                handle_ = 0;
                throw Error(err);
            }
            closed_ = true;
            handle_ = 0;
        }
    }

    template<typename... Args>
    void execute(const std::string& query, const Args&... args) const {
        if (closed_) {
            throw Error("Connection is closed");
        }
        std::vector<std::string> params;
        params.reserve(sizeof...(args));
        (params.push_back(toString(args)), ...);

        std::vector<char*> cParams;
        cParams.reserve(params.size());
        for (const auto& p : params) {
            cParams.push_back(const_cast<char*>(p.c_str()));
        }

        char* errPtr = nullptr;
        if (cParams.empty()) {
            GodbcExecute(handle_, query.c_str(), &errPtr);
        } else {
            GodbcExecuteWithParams(handle_, query.c_str(), cParams.data(), static_cast<int>(cParams.size()), &errPtr);
        }
        if (errPtr) {
            std::string err(errPtr);
            free(errPtr);
            throw Error(err);
        }
    }

    template<typename... Args>
    ResultSet query(const std::string& query, const Args&... args) const {
        if (closed_) {
            throw Error("Connection is closed");
        }
        std::vector<std::string> params;
        params.reserve(sizeof...(args));
        (params.push_back(toString(args)), ...);

        std::vector<char*> cParams;
        cParams.reserve(params.size());
        for (const auto& p : params) {
            cParams.push_back(const_cast<char*>(p.c_str()));
        }

        char* errPtr = nullptr;
        godbc_handle result = GodbcQueryWithParams(handle_, query.c_str(), cParams.data(), static_cast<int>(cParams.size()), &errPtr);
        if (errPtr) {
            std::string err(errPtr);
            free(errPtr);
            throw Error(err);
        }
        return ResultSet(result);
    }

    PreparedStatement prepare(const std::string& query) const {
        if (closed_) {
            throw Error("Connection is closed");
        }
        char* errPtr = nullptr;
        godbc_handle handle = GodbcPrepare(handle_, query.c_str(), &errPtr);
        if (errPtr) {
            std::string err(errPtr);
            free(errPtr);
            throw Error(err);
        }
        return PreparedStatement(handle);
    }

    Transaction beginTransaction() const {
        if (closed_) {
            throw Error("Connection is closed");
        }
        char* errPtr = nullptr;
        godbc_handle handle = GodbcBeginTransaction(handle_, &errPtr);
        if (errPtr) {
            std::string err(errPtr);
            free(errPtr);
            throw Error(err);
        }
        return Transaction(handle);
    }
};

class ConnectionPool {
public:
    static Connection getConnection(const std::string& connStr, int minConns = 1, int maxConns = 10,
                                  int connTimeoutMs = 30000, int retryDelayMs = 1000, int retryAttempts = 3,
                                  int networkRetryDelaySecs = 5, bool verboseLogging = false) {
        char* errPtr = nullptr;
        godbc_handle handle = GodbcConnect(connStr.c_str(), minConns, maxConns, connTimeoutMs,
                                         retryDelayMs, retryAttempts, networkRetryDelaySecs,
                                         verboseLogging ? 1 : 0, &errPtr);
        if (errPtr) {
            std::string err(errPtr);
            free(errPtr);
            throw Error(err);
        }
        return Connection(handle);
    }
};

} // namespace godbc

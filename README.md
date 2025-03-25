# GoDBC - A Go-based SQL Server Driver Bridge for C++

GoDBC is a high-performance SQL Server database driver bridge that uses Go's `database/sql` package with the `go-mssqldb` driver under the hood, exposed to C++ applications through a clean, modern C++ interface.

## Features

- Modern C++17 interface with RAII resource management
- Connection pooling with automatic retry mechanisms
- Parameterized queries with proper SQL injection protection
- Transaction support
- Prepared statement support
- Automatic handling of network errors and retries
- Thread-safe connection and resource management

## Requirements

- Go 1.16 or later
- C++17 compatible compiler
- CMake 3.10 or later
- SQL Server 2016 or later

## Installation

### Using CMake

1. Add the GoDBC repository as a subdirectory in your project:

```bash
git clone https://github.com/yourusername/godbc.git
```

2. Add the following to your `CMakeLists.txt`:

```cmake
add_subdirectory(godbc)
target_link_libraries(your_target PRIVATE godbc)
```

### Manual Installation

1. Build the Go bridge:
```bash
cd godbc
go build -buildmode=c-archive -o libgodbc.a bridge/bridge.go
```

2. Include the headers and link against the library:
```cmake
include_directories(godbc/cpp)
target_link_libraries(your_target PRIVATE 
    ${PROJECT_SOURCE_DIR}/godbc/libgodbc.a
    pthread
    dl
)
```

## Usage Examples

### Basic Connection and Query

```cpp
#include <godbc.hpp>
#include <iostream>
#include <vector>

int main() {
    try {
        // Connect to the database
        auto conn = godbc::ConnectionPool::getConnection(
            "server=localhost;user id=sa;password=Password123;database=testdb",
            1,    // Min connections
            10,   // Max connections
            30000 // Connection timeout (ms)
        );

        // Execute a simple query
        auto result = conn.query("SELECT id, name FROM users WHERE age > ?", 25);
        
        std::vector<std::string> row(2);  // 2 columns: id and name
        while (result.next()) {
            result.scan(row);
            std::cout << "ID: " << row[0] << ", Name: " << row[1] << std::endl;
        }
    } catch (const godbc::Error& e) {
        std::cerr << "Database error: " << e.what() << std::endl;
    }
}
```

### Using Prepared Statements

```cpp
#include <godbc.hpp>

void insertUsers(godbc::Connection& conn) {
    auto stmt = conn.prepare(
        "INSERT INTO users (name, age, email) VALUES (?, ?, ?)"
    );
    
    // Execute multiple times with different parameters
    stmt.execute("John Doe", 30, "john@example.com");
    stmt.execute("Jane Smith", 25, "jane@example.com");
    stmt.execute("Bob Wilson", 35, "bob@example.com");
}
```

### Transaction Management

```cpp
#include <godbc.hpp>

void batchUpdateWithTransaction(godbc::Connection& conn) {
    auto tx = conn.beginTransaction();
    try {
        // Insert a new record
        tx.execute("INSERT INTO users (name, age, email) VALUES (?, ?, ?)",
                  "Alice Brown", 28, "alice@example.com");
        
        // Update existing records
        tx.execute("UPDATE users SET age = ? WHERE name = ?", 31, "John Doe");
        
        // Delete a record
        tx.execute("DELETE FROM users WHERE name = ?", "Bob Wilson");
        
        // Commit all changes atomically
        tx.commit();
    } catch (...) {
        // Transaction automatically rolls back if not committed
        throw;
    }
}
```

### Connection Pool Configuration

```cpp
#include <godbc.hpp>

auto conn = godbc::ConnectionPool::getConnection(
    "server=localhost;user id=sa;password=Password123;database=testdb",
    1,      // Min connections
    10,     // Max connections
    30000,  // Connection timeout (ms)
    1000,   // Retry delay (ms)
    3,      // Retry attempts
    1,      // Network retry delay (seconds)
    true    // Verbose logging
);
```

## API Reference

### Connection Class

The `Connection` class represents a database connection and provides methods for executing queries and managing transactions.

#### Methods

- `execute(const std::string& query)`: Execute a query without parameters
- `execute(const std::string& query, const Args&... args)`: Execute a parameterized query
- `query(const std::string& query)`: Execute a query that returns results
- `query(const std::string& query, const Args&... args)`: Execute a parameterized query that returns results
- `beginTransaction()`: Start a new transaction
- `prepare(const std::string& query)`: Create a prepared statement

### PreparedStatement Class

The `PreparedStatement` class represents a pre-compiled SQL statement.

#### Methods

- `execute(const Args&... args)`: Execute the prepared statement with parameters
- `close()`: Explicitly close the prepared statement

### Transaction Class

The `Transaction` class represents a database transaction.

#### Methods

- `execute(const std::string& query)`: Execute a query within the transaction
- `execute(const std::string& query, const Args&... args)`: Execute a parameterized query within the transaction
- `commit()`: Commit the transaction
- `rollback()`: Roll back the transaction

### ResultSet Class

The `ResultSet` class represents the results of a query.

#### Methods

- `next()`: Move to the next row, returns false when no more rows
- `scan(std::vector<std::string>& values)`: Scan the current row into the provided vector
- `close()`: Explicitly close the result set

### Error Handling

All classes throw `godbc::Error` exceptions when errors occur. The error message contains details about what went wrong.

## Tools

### SQL Runner

The `sql_runner` is a command-line tool for benchmarking and testing SQL queries. It supports multi-threaded execution, connection pooling, and detailed performance metrics.

#### Usage

```bash
sql_runner [options]

Options:
  -c <connection_string>  Connection string (required)
  -d <directory>         SQL files directory (default: sql)
  -t <threads>           Number of threads (default: 1)
  -n <iterations>        Number of iterations (default: 1)
  -i                     Run indefinitely
  -s <ms>               Delay between queries in ms (default: 0)
  -r <ms>               Report interval in ms (default: 1000)
  -v                     Verbose output
  -h                     Show this help message
```

#### Example

```bash
# Run 3 iterations with 2 threads, 100ms delay between queries
sql_runner -c "server=localhost;user id=sa;password=Password123;database=testdb" \
          -t 2 -n 3 -s 100 -v
```

#### SQL Files

The tool executes SQL files from the specified directory (default: `sql/`). Each file should contain a single SQL query. Files must have the `.sql` extension.

Example SQL files:
```sql
-- query1.sql
SELECT COUNT(*) FROM users;

-- query2.sql
SELECT TOP 10 * FROM orders ORDER BY order_date DESC;
```

#### Metrics

The tool provides real-time and final metrics including:
- Progress percentage
- Successful and failed queries
- Average query execution time
- Average connection time
- Queries per second (QPS)
- Total execution time

#### Features

- **Multi-threaded Execution**: Run queries concurrently with configurable thread count
- **Connection Pooling**: Efficiently manages database connections
- **Iteration Control**: Run a fixed number of iterations or continuously
- **Query Delay**: Add configurable delays between queries to control load
- **Progress Reporting**: Real-time metrics with configurable reporting interval
- **Verbose Logging**: Detailed execution information with `-v` flag
- **Error Handling**: Robust error reporting and connection retry logic

#### Use Cases

1. **Performance Testing**: Measure query performance under different concurrency levels
2. **Load Testing**: Generate sustained database load with multiple threads
3. **Connection Pool Testing**: Verify connection pool behavior under load
4. **Query Validation**: Test multiple SQL queries for correctness
5. **Monitoring**: Track query performance metrics over time

## Best Practices

1. **Resource Management**
   - Use RAII: Resources are automatically cleaned up when objects go out of scope
   - Explicitly close resources if you're done with them before scope ends

2. **Connection Pooling**
   - Use appropriate pool sizes based on your application's needs
   - Set reasonable timeouts and retry parameters

3. **Parameterized Queries**
   - Always use parameterized queries instead of string concatenation
   - Parameters are properly escaped and protected against SQL injection

4. **Transaction Management**
   - Keep transactions as short as possible
   - Use appropriate isolation levels
   - Handle errors properly with try-catch blocks

5. **Error Handling**
   - Always catch and handle `godbc::Error` exceptions
   - Log errors appropriately in your application
   - Consider retrying operations on network errors

## Thread Safety

- Connection pools are thread-safe
- Individual connections should not be shared across threads
- Prepared statements and result sets should not be shared across threads

## Performance Tips

1. **Connection Pooling**
   - Set appropriate min/max connections
   - Use connection timeouts to prevent hanging connections

2. **Prepared Statements**
   - Reuse prepared statements for repeated queries
   - Close prepared statements when no longer needed

3. **Result Sets**
   - Close result sets explicitly when done
   - Don't keep result sets open longer than necessary

4. **Batch Operations**
   - Use prepared statements for batch inserts/updates
   - Use transactions for multiple operations

## Building and Testing

```bash
mkdir build && cd build
cmake ..
make
./examples/query_examples "server=localhost;user id=sa;password=Password123;database=testdb"
```

## License

[Your License Here]

## Contributing

[Your Contributing Guidelines Here]

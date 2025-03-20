# GODBC

A Go library for database connectivity that provides a simple and efficient interface for Microsoft SQL Server operations, with C++ bindings.

## Features

- Simple and intuitive API for both Go and C++
- Optimized for Microsoft SQL Server
- Connection pooling with configurable settings
- Transaction management
- Prepared statements
- Parameter binding
- Automatic connection retry and timeout handling
- C++ bindings with RAII and exception handling

## Installation

### Go

```bash
go get github.com/dsandor/godbc
```

### C++

Requirements:
- CMake 3.10 or higher
- Go 1.16 or higher
- C++14 compatible compiler

```bash
mkdir build && cd build
cmake ..
make
```

## Usage

### Go

```go
import (
    "context"
    "github.com/dsandor/godbc"
    "github.com/dsandor/godbc/mssql"
    "time"
)

func main() {
    // Configure the MSSQL driver
    config := &mssql.Config{
        MinConnections: 5,
        MaxConnections: 20,
        ConnTimeout:    30 * time.Second,
        RetryAttempts:  3,
        RetryDelay:     time.Second,
    }
    
    // Create a connection
    db, err := godbc.Connect(context.Background(), "mssql", 
        "server=localhost;user id=sa;password=yourpass;database=testdb")
    if err != nil {
        log.Fatal(err)
    }
    defer db.Close()

    // Execute a query
    rows, err := db.Query(context.Background(), "SELECT * FROM users")
    if err != nil {
        log.Fatal(err)
    }
    defer rows.Close()
}
```

### C++

```cpp
#include <godbc/cpp/godbc.hpp>

int main() {
    try {
        // Create a connection with custom pool settings
        godbc::Connection conn(
            "server=localhost;user id=sa;password=yourpass;database=testdb",
            5,    // minConnections
            20,   // maxConnections
            30000,// connTimeout (ms)
            1000, // retryDelay (ms)
            3     // retryAttempts
        );

        // Execute a simple query
        conn.execute("CREATE TABLE IF NOT EXISTS users (id INT, name VARCHAR(100))");
        
        // Query with results
        auto results = conn.query("SELECT id, name FROM users");
        while (results->next()) {
            auto row = results->getRow(2); // 2 columns
            std::cout << "ID: " << row[0] << ", Name: " << row[1] << std::endl;
        }

    } catch (const godbc::Error& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
```

## Configuration Options

- `MinConnections`: Minimum number of connections to keep in the pool
- `MaxConnections`: Maximum number of connections allowed in the pool
- `ConnTimeout`: Connection timeout duration
- `RetryAttempts`: Number of connection retry attempts
- `RetryDelay`: Delay between retry attempts

## Building C++ Projects

1. Include the GODBC library in your CMake project:
```cmake
find_package(godbc REQUIRED)
target_link_libraries(your_target godbc)
```

2. Build your project with the same requirements as GODBC:
```bash
mkdir build && cd build
cmake ..
make
```

## License

MIT License

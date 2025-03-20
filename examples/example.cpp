#include <iostream>
#include <stdexcept>
#include <string>
#include <memory>
#include <vector>
#include "../cpp/godbc.hpp"

void setupDatabases(const godbc::Connection& conn) {
    try {
        // Create test tables
        conn.execute("IF OBJECT_ID('users', 'U') IS NOT NULL DROP TABLE users;");
        conn.execute("CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR(100));");
        
        // Insert some test data
        auto stmt = conn.prepare("INSERT INTO users (id, name) VALUES (?, ?);");
        stmt.execute(1, "Alice");
        stmt.execute(2, "Bob");
        stmt.execute(3, "Charlie");
        
        std::cout << "Test data inserted successfully.\n";
    } catch (const godbc::Error& e) {
        std::cerr << "Database error: " << e.what() << std::endl;
        throw;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        throw;
    }
}

void runQueries(const godbc::Connection& conn) {
    try {
        // Simple query
        auto result = conn.query("SELECT * FROM users ORDER BY id;");
        std::cout << "\nAll users:\n";
        while (result->next()) {
            auto row = result->getRow(2);  // 2 columns: id and name
            std::cout << "ID: " << row[0] << ", Name: " << row[1] << std::endl;
        }
        
        // Transaction example
        auto tx = conn.beginTransaction();
        try {
            tx.execute("UPDATE users SET name = 'Alice Smith' WHERE id = 1;");
            tx.execute("UPDATE users SET name = 'Bob Jones' WHERE id = 2;");
            tx.commit();
            std::cout << "\nTransaction committed successfully.\n";
        } catch (const std::exception& e) {
            tx.rollback();
            throw;
        }
        
        // Prepared statement with parameters
        auto stmt = conn.prepare("SELECT name FROM users WHERE id > ?;");
        stmt.execute(1);  // Get users with id > 1
        
        std::cout << "\nUsers with ID > 1:\n";
        auto rows = conn.query("SELECT name FROM users WHERE id > 1 ORDER BY id;");
        while (rows->next()) {
            auto row = rows->getRow(1);  // 1 column: name
            std::cout << "Name: " << row[0] << std::endl;
        }
    } catch (const godbc::Error& e) {
        std::cerr << "Database error: " << e.what() << std::endl;
        throw;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        throw;
    }
}

int main() {
    try {
        // Define two different connection strings
        const std::string prodConnStr = 
            "server=localhost;user id=sa;password=Password123;database=proddb";
        const std::string testConnStr = 
            "server=localhost;user id=sa;password=Password123;database=testdb";

        // Get connections from different pools
        auto prodConn = godbc::ConnectionPool::getConnection(
            prodConnStr,    // Connection string
            5,             // Min connections
            20,            // Max connections
            30000,         // Connection timeout (ms)
            1000,          // Retry delay (ms)
            3              // Retry attempts
        );

        auto testConn = godbc::ConnectionPool::getConnection(testConnStr);

        std::cout << "Setting up databases...\n";
        setupDatabases(testConn);

        std::cout << "Running queries...\n";
        runQueries(testConn);

        std::cout << "All operations completed successfully.\n";
        return 0;
    } catch (const godbc::Error& e) {
        std::cerr << "Database error: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}

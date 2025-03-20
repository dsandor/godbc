#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include "../cpp/godbc.hpp"

void printRow(const std::vector<std::string>& row) {
    std::cout << "ID: " << row[0] << ", Name: " << row[1] << ", Age: " << row[2] << ", Email: " << row[3] << std::endl;
}

void runQueryExamples(const std::string& connStr) {
    try {
        // Create a connection
        std::cout << "Connecting to database..." << std::endl;
        auto conn = godbc::ConnectionPool::getConnection(connStr, 1, 10, 30000, 1000, 3, 1);

        // Create a test table
        std::cout << "\nCreating test table..." << std::endl;
        conn.execute(R"(
            IF EXISTS (SELECT * FROM sys.tables WHERE name = 'query_examples')
                DROP TABLE query_examples;
            CREATE TABLE query_examples (
                id INT IDENTITY(1,1) PRIMARY KEY,
                name NVARCHAR(100),
                age INT,
                email NVARCHAR(200)
            )
        )");

        // 2. Prepared statement with multiple parameters
        std::cout << "\n2. Prepared statement example:" << std::endl;
        auto stmt = conn.prepare("INSERT INTO query_examples (name, age, email) VALUES (?, ?, ?)");
        
        // Insert multiple rows using the prepared statement
        stmt.execute("John Doe", 30, "john@example.com");
        stmt.execute("Jane Smith", 25, "jane@example.com");
        stmt.execute("Bob Wilson", 35, "bob@example.com");

        // 1. Simple parameterized query
        std::cout << "\n1. Parameterized query example:" << std::endl;
        std::cout << "Querying users older than 25:" << std::endl;
        auto result = conn.query("SELECT * FROM query_examples WHERE age > ?", 25);
        std::vector<std::string> row(4);  // 4 columns: id, name, age, email
        while (result.next()) {
            result.scan(row);
            printRow(row);
        }

        // 3. Transaction with multiple operations
        std::cout << "\n3. Transaction example:" << std::endl;
        auto tx = conn.beginTransaction();
        try {
            // Insert
            tx.execute("INSERT INTO query_examples (name, age, email) VALUES (?, ?, ?)", 
                      "Alice Brown", 28, "alice@example.com");
            
            // Update
            tx.execute("UPDATE query_examples SET age = ? WHERE name = ?", 31, "John Doe");
            
            // Delete
            tx.execute("DELETE FROM query_examples WHERE name = ?", "Bob Wilson");
            
            // Commit the transaction
            tx.commit();
            std::cout << "Transaction committed successfully" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Transaction failed: " << e.what() << std::endl;
            tx.rollback();
            throw;
        }

        // 4. Query with multiple parameters
        std::cout << "\n4. Query with multiple parameters:" << std::endl;
        std::cout << "Querying users older than 25 with example.com email:" << std::endl;
        result = conn.query("SELECT * FROM query_examples WHERE age > ? AND email LIKE ?", 
                          25, "%@example.com");
        while (result.next()) {
            result.scan(row);
            printRow(row);
        }

        // 5. Batch insert using prepared statement
        std::cout << "\n5. Batch insert using prepared statement:" << std::endl;
        stmt = conn.prepare("INSERT INTO query_examples (name, age, email) VALUES (?, ?, ?)");
        
        // Insert multiple rows in a loop
        for (int i = 0; i < 3; i++) {
            std::string name = "Batch User " + std::to_string(i + 1);
            std::string email = "batch" + std::to_string(i + 1) + "@example.com";
            stmt.execute(name, 20 + i, email);
        }

        // 6. Final results
        std::cout << "\n6. Final table contents:" << std::endl;
        result = conn.query("SELECT * FROM query_examples ORDER BY id");
        while (result.next()) {
            result.scan(row);
            printRow(row);
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        throw;
    }
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <connection_string>" << std::endl;
        std::cerr << "Example: " << argv[0] << " \"server=localhost;user id=sa;password=Password123;database=testdb\"" << std::endl;
        return 1;
    }

    try {
        runQueryExamples(argv[1]);
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
} 
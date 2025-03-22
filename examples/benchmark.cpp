#include <iostream>
#include <stdexcept>
#include <string>
#include <memory>
#include <vector>
#include <chrono>
#include <iomanip>
#include <atomic>
#include "../cpp/godbc.hpp"
#include <thread>
#include <mutex>
#include <cstring>
#include <cstdlib>
#include <tuple>

#ifdef _WIN32
#include <windows.h>
#else
#include <thread>
#include <mutex>
#endif

struct BenchmarkConfig {
    std::string connStr;
    int numQueries;
    int numThreads;
    int delayMs;
    bool useTransactions;
    bool useWriteQueries;  // New: whether to use write queries that need transactions
    int reportIntervalMs;
    bool verboseLogging;  // New: whether to show verbose logs
    int networkRetryAttempts;  // New: number of network failure retries
};

struct Metrics {
    std::atomic<int> successCount{0};
    std::atomic<int> errorCount{0};
    std::atomic<int> retryCount{0};
    std::atomic<int> connectionCreationCount{0};
    std::atomic<int64_t> totalConnectionTimeMs{0};
    std::atomic<int64_t> totalQueryTimeMs{0};
    std::atomic<bool> shouldStop{false};  // New: flag to stop the reporter thread
#ifdef _WIN32
    CRITICAL_SECTION timeMutex;
#else
    std::mutex timeMutex;
#endif
};

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " [options]\n"
              << "Options:\n"
              << "  -c, --connection <string>  Connection string (required)\n"
              << "  -n, --num-queries <int>    Number of queries to run (default: 1000)\n"
              << "  -t, --threads <int>        Number of threads (default: 1)\n"
              << "  -d, --delay <int>          Delay between queries in ms (default: 0)\n"
              << "  -x, --transactions         Use transactions (default: false)\n"
              << "  -w, --write-queries        Use write queries that need transactions (default: false)\n"
              << "  -r, --report <int>         Report interval in ms (default: 1000)\n"
              << "  -v, --verbose             Enable verbose logging (default: false)\n"
              << "  -a, --retry-attempts <int> Number of network failure retries (default: 3)\n"
              << "  -h, --help                 Show this help message\n";
}

BenchmarkConfig parseArgs(int argc, char* argv[]) {
    BenchmarkConfig config{
        .connStr = "",
        .numQueries = 1000,
        .numThreads = 1,
        .delayMs = 0,
        .useTransactions = false,
        .useWriteQueries = false,
        .reportIntervalMs = 1000,
        .verboseLogging = false,
        .networkRetryAttempts = 3
    };

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            exit(0);
        } else if (arg == "-c" || arg == "--connection") {
            if (++i < argc) config.connStr = argv[i];
        } else if (arg == "-n" || arg == "--num-queries") {
            if (++i < argc) config.numQueries = std::stoi(argv[i]);
        } else if (arg == "-t" || arg == "--threads") {
            if (++i < argc) config.numThreads = std::stoi(argv[i]);
        } else if (arg == "-d" || arg == "--delay") {
            if (++i < argc) config.delayMs = std::stoi(argv[i]);
        } else if (arg == "-x" || arg == "--transactions") {
            config.useTransactions = true;
        } else if (arg == "-w" || arg == "--write-queries") {
            config.useWriteQueries = true;
        } else if (arg == "-r" || arg == "--report") {
            if (++i < argc) config.reportIntervalMs = std::stoi(argv[i]);
        } else if (arg == "-v" || arg == "--verbose") {
            config.verboseLogging = true;
        } else if (arg == "-a" || arg == "--retry-attempts") {
            if (++i < argc) config.networkRetryAttempts = std::stoi(argv[i]);
        }
    }

    if (config.connStr.empty()) {
        std::cerr << "Error: Connection string is required\n";
        printUsage(argv[0]);
        exit(1);
    }

    return config;
}

void printMetrics(const Metrics& metrics, int totalQueries, const std::chrono::milliseconds& elapsed) {
    int completed = metrics.successCount + metrics.errorCount;
    double progress = (completed * 100.0) / totalQueries;
    
    std::cout << "\rProgress: " << std::fixed << std::setprecision(1) << progress << "% | "
              << "Success: " << metrics.successCount << " | "
              << "Errors: " << metrics.errorCount << " | "
              << "Retries: " << metrics.retryCount << " | "
              << "Avg Query Time: " << std::fixed << std::setprecision(2)
              << (metrics.totalQueryTimeMs / static_cast<double>(metrics.successCount)) << "ms | "
              << "Avg Conn Time: " << std::fixed << std::setprecision(2)
              << (metrics.totalConnectionTimeMs / static_cast<double>(metrics.connectionCreationCount)) << "ms | "
              << "QPS: " << std::fixed << std::setprecision(2)
              << (metrics.successCount * 1000.0 / elapsed.count()) << std::flush;
}

void runQueries(const BenchmarkConfig& config, int threadId, int queriesPerThread,
                Metrics& metrics) {
    try {
        if (config.verboseLogging) {
            std::cout << "\nThread " << threadId << " attempting to connect with string: " << config.connStr << std::endl;
        }
        
        auto connStart = std::chrono::high_resolution_clock::now();
        try {
            // First try to get a connection
            if (config.verboseLogging) {
                std::cout << "\nThread " << threadId << " getting connection from pool..." << std::endl;
            }
            auto conn = godbc::ConnectionPool::getConnection(config.connStr, 1, 10, 30000, 1000, config.networkRetryAttempts, config.verboseLogging ? 1 : 0);
            
            if (config.verboseLogging) {
                std::cout << "\nThread " << threadId << " got connection from pool" << std::endl;
            }
            
            auto connEnd = std::chrono::high_resolution_clock::now();
            auto connDuration = std::chrono::duration_cast<std::chrono::milliseconds>(connEnd - connStart);
            metrics.totalConnectionTimeMs += connDuration.count();
            metrics.connectionCreationCount++;
            
            if (config.verboseLogging) {
                std::cout << "\nThread " << threadId << " connected successfully in " << connDuration.count() << "ms" << std::endl;
            }
            
            // Test the connection with a simple query
            try {
                if (config.verboseLogging) {
                    std::cout << "\nThread " << threadId << " executing test query..." << std::endl;
                }
                conn.execute("SELECT @@VERSION");
                if (config.verboseLogging) {
                    std::cout << "\nThread " << threadId << " successfully executed test query" << std::endl;
                }
            } catch (const std::exception& e) {
                std::cerr << "\nThread " << threadId << " failed to execute test query: " << e.what() << std::endl;
                std::cerr << "Connection string used: " << config.connStr << std::endl;
                throw;
            }
            
            // Create test table if using write queries
            if (config.useWriteQueries) {
                try {
                    if (config.verboseLogging) {
                        std::cout << "\nThread " << threadId << " creating test table..." << std::endl;
                    }
                    conn.execute("IF NOT EXISTS (SELECT * FROM sys.tables WHERE name = 'benchmark_test') "
                               "CREATE TABLE benchmark_test (id INT IDENTITY(1,1) PRIMARY KEY, value INT)");
                    if (config.verboseLogging) {
                        std::cout << "\nThread " << threadId << " created test table successfully" << std::endl;
                    }
                } catch (const std::exception& e) {
                    std::cerr << "\nThread " << threadId << " failed to create test table: " << e.what() << std::endl;
                    throw;
                }
            }
            
            for (int i = 0; i < queriesPerThread; i++) {
                auto start = std::chrono::high_resolution_clock::now();
                bool shouldRetry = true;
                int retryCount = 0;
                
                while (shouldRetry && retryCount < config.networkRetryAttempts) {
                    try {
                        if (config.useWriteQueries || config.useTransactions) {
                            // Validate connection before starting transaction
                            conn.execute("SELECT 1");
                            
                            auto tx = conn.beginTransaction();
                            try {
                                auto queryStart = std::chrono::high_resolution_clock::now();
                                
                                if (config.useWriteQueries) {
                                    tx.execute("INSERT INTO benchmark_test (value) VALUES (" + std::to_string(i) + ")");
                                } else {
                                    tx.execute("SELECT @@VERSION");
                                }
                                
                                auto queryEnd = std::chrono::high_resolution_clock::now();
                                auto queryDuration = std::chrono::duration_cast<std::chrono::milliseconds>(queryEnd - queryStart);
                                
                                if (config.verboseLogging) {
                                    std::cout << "\nThread " << threadId << " executed " 
                                            << (config.useWriteQueries ? "write" : "read") 
                                            << " query in " << queryDuration.count() << "ms" << std::endl;
                                }
                                
                                tx.commit();
                                shouldRetry = false;  // Success, no need to retry
                                
                            } catch (const std::exception& e) {
                                std::string errorMsg = e.what();
                                if (errorMsg.find("invalid transaction handle") != std::string::npos ||
                                    errorMsg.find("connection") != std::string::npos) {
                                    std::cerr << "\nThread " << threadId << " transaction failed: " << e.what() << std::endl;
                                    try {
                                        tx.rollback();
                                    } catch (...) {
                                        // Ignore rollback errors
                                    }
                                    throw e;  // Re-throw to trigger retry
                                }
                                // For other transaction errors, don't retry
                                std::cerr << "\nThread " << threadId << " query " << i << " failed: " << e.what() << std::endl;
                                metrics.errorCount++;
                                shouldRetry = false;
                            }
                        } else {
                            // Simple read query without transaction
                            auto queryStart = std::chrono::high_resolution_clock::now();
                            conn.execute("SELECT @@VERSION");
                            auto queryEnd = std::chrono::high_resolution_clock::now();
                            auto queryDuration = std::chrono::duration_cast<std::chrono::milliseconds>(queryEnd - queryStart);
                            
                            if (config.verboseLogging) {
                                std::cout << "\nThread " << threadId << " executed read query in " << queryDuration.count() << "ms" << std::endl;
                            }
                            shouldRetry = false;
                        }
                        
                        if (config.delayMs > 0) {
    #ifdef _WIN32
                            Sleep(config.delayMs);
    #else
                            std::this_thread::sleep_for(std::chrono::milliseconds(config.delayMs));
    #endif
                        }
                        
                        auto end = std::chrono::high_resolution_clock::now();
                        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
                        metrics.totalQueryTimeMs += duration.count();
                        metrics.successCount++;
                        
                        if (config.verboseLogging && i % 100 == 0) {
                            std::cout << "\nThread " << threadId << " completed " << i << " queries" << std::endl;
                        }
                        
                    } catch (const std::exception& e) {
                        std::string errorMsg = e.what();
                        if (errorMsg.find("connection") != std::string::npos || 
                            errorMsg.find("invalid handle") != std::string::npos ||
                            errorMsg.find("invalid transaction handle") != std::string::npos) {
                            
                            retryCount++;
                            if (retryCount < config.networkRetryAttempts) {
                                if (config.verboseLogging) {
                                    std::cout << "\nThread " << threadId << " attempting to reconnect (attempt " 
                                            << retryCount << "/" << config.networkRetryAttempts << ")..." << std::endl;
                                }
                                try {
                                    conn = godbc::ConnectionPool::getConnection(config.connStr, 1, 10, 30000, 1000, 
                                                                             config.networkRetryAttempts, 
                                                                             config.verboseLogging ? 1 : 0);
                                    metrics.retryCount++;
                                    if (config.verboseLogging) {
                                        std::cout << "\nThread " << threadId << " reconnected successfully" << std::endl;
                                    }
                                    continue;  // Retry the query
                                } catch (const std::exception& connErr) {
                                    std::cerr << "\nThread " << threadId << " failed to reconnect: " << connErr.what() << std::endl;
                                }
                            }
                        }
                        metrics.errorCount++;
                        shouldRetry = false;
                    }
                }
            }
            
            if (config.verboseLogging) {
                std::cout << "\nThread " << threadId << " completed all " << queriesPerThread << " queries" << std::endl;
            }
        } catch (const std::exception& e) {
            std::string errorMsg = e.what();
            std::cerr << "\nThread " << threadId << " connection error: " << errorMsg << std::endl;
            std::cerr << "Connection string used: " << config.connStr << std::endl;
            
            // Check for common connection issues
            if (errorMsg.find("connection refused") != std::string::npos) {
                std::cerr << "Connection refused - Check if SQL Server is running and accessible" << std::endl;
            } else if (errorMsg.find("timeout") != std::string::npos) {
                std::cerr << "Connection timeout - Check network connectivity and firewall settings" << std::endl;
            } else if (errorMsg.find("authentication") != std::string::npos) {
                std::cerr << "Authentication failed - Check username and password" << std::endl;
            } else if (errorMsg.find("server not found") != std::string::npos) {
                std::cerr << "Server not found - Check server name and DNS resolution" << std::endl;
            } else if (errorMsg.find("invalid transaction handle") != std::string::npos) {
                std::cerr << "Invalid transaction handle - This might indicate a connection issue or driver problem" << std::endl;
            }
            
            throw;
        }
    } catch (const std::exception& e) {
        std::cerr << "\nThread " << threadId << " failed: " << e.what() << std::endl;
        metrics.errorCount += queriesPerThread;
    }
}

int main(int argc, char* argv[]) {
    try {
        auto config = parseArgs(argc, argv);
        
        std::cout << "Starting benchmark with configuration:\n"
                  << "Connection string: " << config.connStr << "\n"
                  << "Number of queries: " << config.numQueries << "\n"
                  << "Number of threads: " << config.numThreads << "\n"
                  << "Delay between queries: " << config.delayMs << "ms\n"
                  << "Using transactions: " << (config.useTransactions ? "yes" : "no") << "\n"
                  << "Using write queries: " << (config.useWriteQueries ? "yes" : "no") << "\n"
                  << "Report interval: " << config.reportIntervalMs << "ms\n"
                  << "Verbose logging: " << (config.verboseLogging ? "yes" : "no") << "\n"
                  << "Network retry attempts: " << config.networkRetryAttempts << "\n\n";
        
        Metrics metrics;
        auto start = std::chrono::high_resolution_clock::now();

        std::vector<std::thread> threads;
        threads.reserve(config.numThreads);

        // Create reporter thread
        std::thread reporter([&metrics, &config, &start]() {
            while (!metrics.shouldStop) {
                std::this_thread::sleep_for(std::chrono::milliseconds(config.reportIntervalMs));
                auto now = std::chrono::high_resolution_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start);
                printMetrics(metrics, config.numQueries, elapsed);
            }
        });

        // Create worker threads
        int threadQueries = config.numQueries / config.numThreads;
        for (int i = 0; i < config.numThreads; i++) {
            threads.emplace_back([config, i, threadQueries, &metrics]() {
                runQueries(config, i, threadQueries, metrics);
            });
        }

        // Wait for all worker threads to complete
        for (auto& thread : threads) {
            thread.join();
        }

        // Signal reporter thread to stop and wait for it
        metrics.shouldStop = true;
        reporter.join();

        auto end = std::chrono::high_resolution_clock::now();
        auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        std::cout << "\n\nFinal Results:\n"
                  << "Total time: " << totalDuration.count() << "ms\n"
                  << "Successful queries: " << metrics.successCount << "\n"
                  << "Failed queries: " << metrics.errorCount << "\n"
                  << "Connection retries: " << metrics.retryCount << "\n"
                  << "Average query time: " << std::fixed << std::setprecision(2)
                  << (metrics.totalQueryTimeMs / static_cast<double>(metrics.successCount)) << "ms\n"
                  << "Average connection time: " << std::fixed << std::setprecision(2)
                  << (metrics.totalConnectionTimeMs / static_cast<double>(metrics.connectionCreationCount)) << "ms\n"
                  << "Queries per second: " << std::fixed << std::setprecision(2)
                  << (metrics.successCount * 1000.0 / totalDuration.count()) << "\n";
        
        return metrics.errorCount > 0 ? 1 : 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
} 
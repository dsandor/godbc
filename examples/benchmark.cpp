#include <iostream>
#include <stdexcept>
#include <string>
#include <memory>
#include <vector>
#include <chrono>
#include <thread>
#include <iomanip>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include "../cpp/godbc.hpp"

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
    std::mutex timeMutex;
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
        auto connStart = std::chrono::high_resolution_clock::now();
        auto conn = godbc::ConnectionPool::getConnection(config.connStr, 1, 10, 30000, 1000, config.networkRetryAttempts, config.verboseLogging ? 1 : 0);
        auto connEnd = std::chrono::high_resolution_clock::now();
        auto connDuration = std::chrono::duration_cast<std::chrono::milliseconds>(connEnd - connStart);
        metrics.totalConnectionTimeMs += connDuration.count();
        metrics.connectionCreationCount++;
        
        if (config.verboseLogging) {
            std::cout << "\nThread " << threadId << " connected successfully in " << connDuration.count() << "ms" << std::endl;
        }
        
        // Create test table if using write queries
        if (config.useWriteQueries) {
            conn.execute("IF NOT EXISTS (SELECT * FROM sys.tables WHERE name = 'benchmark_test') "
                       "CREATE TABLE benchmark_test (id INT IDENTITY(1,1) PRIMARY KEY, value INT)");
        }
        
        for (int i = 0; i < queriesPerThread; i++) {
            auto start = std::chrono::high_resolution_clock::now();
            
            try {
                if (config.useWriteQueries) {
                    // Write query that needs a transaction
                    auto tx = conn.beginTransaction();
                    try {
                        auto queryStart = std::chrono::high_resolution_clock::now();
                        tx.execute("INSERT INTO benchmark_test (value) VALUES (" + std::to_string(i) + ")");
                        auto queryEnd = std::chrono::high_resolution_clock::now();
                        auto queryDuration = std::chrono::duration_cast<std::chrono::milliseconds>(queryEnd - queryStart);
                        
                        if (config.verboseLogging) {
                            std::cout << "\nThread " << threadId << " executed write query in " << queryDuration.count() << "ms" << std::endl;
                        }
                        
                        tx.commit();
                    } catch (const std::exception& e) {
                        std::cerr << "\nThread " << threadId << " write query " << i << " failed: " << e.what() << std::endl;
                        tx.rollback();
                        throw e;
                    }
                } else if (config.useTransactions) {
                    // Read query with transaction (not recommended)
                    auto tx = conn.beginTransaction();
                    try {
                        auto queryStart = std::chrono::high_resolution_clock::now();
                        tx.execute("SELECT @@VERSION");
                        auto queryEnd = std::chrono::high_resolution_clock::now();
                        auto queryDuration = std::chrono::duration_cast<std::chrono::milliseconds>(queryEnd - queryStart);
                        
                        if (config.verboseLogging) {
                            std::cout << "\nThread " << threadId << " executed read query in transaction in " << queryDuration.count() << "ms" << std::endl;
                        }
                        
                        tx.commit();
                    } catch (const std::exception& e) {
                        std::cerr << "\nThread " << threadId << " read query " << i << " failed: " << e.what() << std::endl;
                        tx.rollback();
                        throw e;
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
                }
                
                if (config.delayMs > 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(config.delayMs));
                }
                
                auto end = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
                metrics.totalQueryTimeMs += duration.count();
                metrics.successCount++;
                
                if (config.verboseLogging && i % 100 == 0) {  // Log progress every 100 queries
                    std::cout << "\nThread " << threadId << " completed " << i << " queries" << std::endl;
                }
            } catch (const std::exception& e) {
                std::cerr << "\nThread " << threadId << " query " << i << " failed: " << e.what() << std::endl;
                metrics.errorCount++;
                
                // Check for connection-related errors
                std::string errorMsg = e.what();
                if (errorMsg.find("connection") != std::string::npos || 
                    errorMsg.find("invalid handle") != std::string::npos) {
                    try {
                        if (config.verboseLogging) {
                            std::cout << "\nThread " << threadId << " attempting to reconnect..." << std::endl;
                        }
                        // Get a new connection
                        conn = godbc::ConnectionPool::getConnection(config.connStr, 1, 10, 30000, 1000, config.networkRetryAttempts, config.verboseLogging ? 1 : 0);
                        metrics.retryCount++;
                        if (config.verboseLogging) {
                            std::cout << "\nThread " << threadId << " reconnected successfully" << std::endl;
                        }
                        // Retry the query
                        i--;  // Decrement i to retry the failed query
                        continue;
                    } catch (const std::exception& connErr) {
                        std::cerr << "\nThread " << threadId << " failed to reconnect: " << connErr.what() << std::endl;
                    }
                }
            }
        }
        
        if (config.verboseLogging) {
            std::cout << "\nThread " << threadId << " completed all " << queriesPerThread << " queries" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "\nThread " << threadId << " failed to connect: " << e.what() << std::endl;
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
        
        std::vector<std::thread> threads;
        Metrics metrics;
        
        int queriesPerThread = config.numQueries / config.numThreads;
        int remainingQueries = config.numQueries % config.numThreads;
        
        auto start = std::chrono::high_resolution_clock::now();
        
        // Start metrics reporting thread
        std::thread reporter([&metrics, &config, &start]() {
            while (!metrics.shouldStop) {
                auto now = std::chrono::high_resolution_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start);
                printMetrics(metrics, config.numQueries, elapsed);
                std::this_thread::sleep_for(std::chrono::milliseconds(config.reportIntervalMs));
            }
        });
        
        // Start worker threads
        for (int i = 0; i < config.numThreads; i++) {
            int threadQueries = queriesPerThread + (i < remainingQueries ? 1 : 0);
            threads.emplace_back(runQueries, std::ref(config), i, threadQueries,
                               std::ref(metrics));
        }
        
        // Wait for all worker threads to complete
        for (auto& thread : threads) {
            thread.join();
        }
        
        // Stop the reporter thread
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
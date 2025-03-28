#include <godbc.hpp>
#include <iostream>
#include <fstream>
#ifdef __APPLE__
#include <filesystem>
namespace fs = std::filesystem;
#else
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#endif
#include <vector>
#include <thread>
#include <mutex>
#include <chrono>
#include <atomic>
#include <iomanip>
#include <sstream>

struct Metrics {
    std::atomic<uint64_t> totalQueries{0};
    std::atomic<uint64_t> successfulQueries{0};
    std::atomic<uint64_t> failedQueries{0};
    std::atomic<uint64_t> totalExecutionTime{0};
    std::atomic<uint64_t> totalConnectionTime{0};
    std::atomic<uint64_t> totalConnections{0};
    std::atomic<bool> shouldStop{false};
    mutable std::mutex printMutex;
};

struct Config {
    std::string connectionString;
    std::string sqlDir;
    int numThreads;
    int iterations;
    int delayMs;
    bool infinite;
    int reportIntervalMs;
    bool verbose;
};

void printMetrics(const Metrics& metrics, const Config& config, const std::chrono::milliseconds& elapsed) {
    std::lock_guard<std::mutex> lock(metrics.printMutex);
    std::cout << "\rProgress: " << std::fixed << std::setprecision(1)
              << (metrics.totalQueries * 100.0 / (config.numThreads * config.iterations)) << "% | "
              << "Success: " << metrics.successfulQueries << " | "
              << "Errors: " << metrics.failedQueries << " | "
              << "Avg Query Time: " << (metrics.totalExecutionTime / metrics.totalQueries) << "ms | "
              << "Avg Conn Time: " << (metrics.totalConnectionTime / metrics.totalConnections) << "ms | "
              << "QPS: " << (metrics.totalQueries * 1000.0 / elapsed.count())
              << std::flush;
}

void runQueries(const Config& config, int threadId, Metrics& metrics) {
    if (config.verbose) {
        std::cout << "[Thread " << threadId << "] Starting query execution..." << std::endl;
    }
    std::vector<std::string> sqlFiles;
    
    // Read SQL files from directory
    if (config.verbose) {
        std::cout << "[Thread " << threadId << "] Reading SQL files from " << config.sqlDir << std::endl;
    }
    for (const auto& entry : fs::directory_iterator(config.sqlDir)) {
        if (entry.path().extension() == ".sql") {
            sqlFiles.push_back(entry.path().string());
        }
    }
    
    if (sqlFiles.empty()) {
        std::cerr << "[Thread " << threadId << "] No SQL files found in directory: " << config.sqlDir << std::endl;
        return;
    }
    if (config.verbose) {
        std::cout << "[Thread " << threadId << "] Found " << sqlFiles.size() << " SQL files" << std::endl;
    }

    int iteration = 0;
    while (!metrics.shouldStop && (config.infinite || iteration < config.iterations)) {
        if (config.verbose) {
            std::cout << "[Thread " << threadId << "] Starting iteration " << (iteration + 1) << std::endl;
        }
        try {
            auto start = std::chrono::high_resolution_clock::now();

            // Get connection from pool
            if (config.verbose) {
                std::cout << "[Thread " << threadId << "] Getting connection from pool..." << std::endl;
            }
            auto conn = godbc::ConnectionPool::getConnection(config.connectionString);
            metrics.totalConnections++;
            
            auto connEnd = std::chrono::high_resolution_clock::now();
            auto connTime = std::chrono::duration_cast<std::chrono::milliseconds>(connEnd - start);
            metrics.totalConnectionTime += connTime.count();
            
            if (config.verbose) {
                std::cout << "[Thread " << threadId << "] Connected successfully in " << connTime.count() << "ms" << std::endl;
            }
            
            // Execute each SQL file
            for (const auto& sqlFile : sqlFiles) {
                if (config.verbose) {
                    std::cout << "[Thread " << threadId << "] Processing " << sqlFile << std::endl;
                }
                try {
                    std::ifstream file(sqlFile);
                    if (!file.is_open()) {
                        std::cerr << "[Thread " << threadId << "] Failed to open SQL file: " << sqlFile << std::endl;
                        continue;
                    }
                    
                    std::stringstream buffer;
                    buffer << file.rdbuf();
                    std::string sql = buffer.str();
                    
                    if (config.verbose) {
                        std::cout << "[Thread " << threadId << "] Executing query (" << sql.length() << " bytes): " << sql.substr(0, 50) << "..." << std::endl;
                    }
                    auto queryStart = std::chrono::high_resolution_clock::now();
                    conn.execute(sql);
                    auto queryEnd = std::chrono::high_resolution_clock::now();
                    auto queryTime = std::chrono::duration_cast<std::chrono::milliseconds>(queryEnd - queryStart);
                    
                    metrics.totalExecutionTime += queryTime.count();
                    metrics.successfulQueries++;
                    
                    if (config.verbose) {
                        std::cout << "[Thread " << threadId << "] Query completed in " << queryTime.count() << "ms" << std::endl;
                    }
                    
                    if (config.delayMs > 0) {
                        if (config.verbose) {
                            std::cout << "[Thread " << threadId << "] Sleeping for " << config.delayMs << "ms" << std::endl;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(config.delayMs));
                    }
                    
                } catch (const std::exception& e) {
                    metrics.failedQueries++;
                    std::cerr << "[Thread " << threadId << "] Error executing SQL from " << sqlFile << ": " << e.what() << std::endl;
                }
                
                metrics.totalQueries++;
            }
            
            if (config.verbose) {
                std::cout << "[Thread " << threadId << "] Closing connection" << std::endl;
            }
            conn.close();
            
        } catch (const std::exception& e) {
            std::cerr << "[Thread " << threadId << "] Thread error: " << e.what() << std::endl;
        }
        
        iteration++;
        if (config.verbose) {
            std::cout << "[Thread " << threadId << "] Completed iteration " << iteration << std::endl;
        }
    }
}

int main(int argc, char* argv[]) {
    Config config;
    config.sqlDir = "sql";  // Default SQL directory
    config.numThreads = 1;  // Default number of threads
    config.iterations = 1;  // Default number of iterations
    config.delayMs = 0;     // Default delay between queries
    config.infinite = false; // Default to finite execution
    config.reportIntervalMs = 1000; // Default report interval
    config.verbose = false; // Default to non-verbose output
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-c" && i + 1 < argc) {
            config.connectionString = argv[++i];
        } else if (arg == "-d" && i + 1 < argc) {
            config.sqlDir = argv[++i];
        } else if (arg == "-t" && i + 1 < argc) {
            config.numThreads = std::stoi(argv[++i]);
        } else if (arg == "-n" && i + 1 < argc) {
            config.iterations = std::stoi(argv[++i]);
        } else if (arg == "-i") {
            config.infinite = true;
        } else if (arg == "-s" && i + 1 < argc) {
            config.delayMs = std::stoi(argv[++i]);
        } else if (arg == "-r" && i + 1 < argc) {
            config.reportIntervalMs = std::stoi(argv[++i]);
        } else if (arg == "-v") {
            config.verbose = true;
        } else if (arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                      << "Options:\n"
                      << "  -c <connection_string>  Connection string (required)\n"
                      << "  -d <directory>         SQL files directory (default: sql)\n"
                      << "  -t <threads>           Number of threads (default: 1)\n"
                      << "  -n <iterations>        Number of iterations (default: 1)\n"
                      << "  -i                     Run indefinitely\n"
                      << "  -s <ms>               Delay between queries in ms (default: 0)\n"
                      << "  -r <ms>               Report interval in ms (default: 1000)\n"
                      << "  -v                     Verbose output\n"
                      << "  -h                     Show this help message\n";
            return 0;
        }
    }
    
    if (config.connectionString.empty()) {
        std::cerr << "Error: Connection string is required. Use -h for help." << std::endl;
        return 1;
    }
    
    if (!fs::exists(config.sqlDir)) {
        std::cerr << "Error: SQL directory does not exist: " << config.sqlDir << std::endl;
        return 1;
    }
    
    std::cout << "Starting SQL runner with configuration:\n"
              << "Connection string: " << config.connectionString << "\n"
              << "SQL directory: " << config.sqlDir << "\n"
              << "Number of threads: " << config.numThreads << "\n"
              << "Iterations: " << (config.infinite ? "infinite" : std::to_string(config.iterations)) << "\n"
              << "Delay between queries: " << config.delayMs << "ms\n"
              << "Report interval: " << config.reportIntervalMs << "ms\n"
              << "Verbose logging: " << (config.verbose ? "yes" : "no") << "\n\n";
    
    Metrics metrics;
    std::vector<std::thread> threads;
    auto start = std::chrono::high_resolution_clock::now();
    
    // Create reporter thread
    std::thread reporter([&metrics, &config, &start]() {
        while (!metrics.shouldStop) {
            std::this_thread::sleep_for(std::chrono::milliseconds(config.reportIntervalMs));
            auto now = std::chrono::high_resolution_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start);
            printMetrics(metrics, config, elapsed);
        }
    });
    
    // Create worker threads
    for (int i = 0; i < config.numThreads; i++) {
        threads.emplace_back([&config, i, &metrics]() {
            runQueries(config, i, metrics);
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
    auto totalTime = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "\n\nFinal Results:\n"
              << "Total time: " << totalTime.count() << "ms\n"
              << "Successful queries: " << metrics.successfulQueries << "\n"
              << "Failed queries: " << metrics.failedQueries << "\n"
              << "Average query time: " << (metrics.totalExecutionTime / metrics.totalQueries) << "ms\n"
              << "Average connection time: " << (metrics.totalConnectionTime / metrics.totalConnections) << "ms\n"
              << "Queries per second: " << (metrics.totalQueries * 1000.0 / totalTime.count()) << "\n";
    
    return 0;
} 
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
#include <limits>
#include <cstdlib>
#include <cstring>

struct Metrics {
    std::atomic<uint64_t> totalQueries{0};
    std::atomic<uint64_t> successfulQueries{0};
    std::atomic<uint64_t> failedQueries{0};
    std::atomic<uint64_t> totalExecutionTime{0};
    std::atomic<uint64_t> totalConnectionTime{0};
    std::atomic<uint64_t> totalConnections{0};
    std::atomic<uint64_t> minQueryTime{std::numeric_limits<uint64_t>::max()};
    std::atomic<uint64_t> maxQueryTime{0};
    std::atomic<uint64_t> minConnectionTime{std::numeric_limits<uint64_t>::max()};
    std::atomic<uint64_t> maxConnectionTime{0};
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
    double expectedQueries = config.numThreads * config.iterations * 3.0; // 3 SQL files
    double progress = (metrics.totalQueries * 100.0 / expectedQueries);
    if (progress > 100.0) progress = 100.0;
    
    std::cout << "\rProgress: " << std::fixed << std::setprecision(1)
              << progress << "% | "
              << "Success: " << metrics.successfulQueries << " | "
              << "Errors: " << metrics.failedQueries << " | "
              << "Query Time: " << (metrics.totalExecutionTime / metrics.totalQueries) << "ms (min: " 
              << metrics.minQueryTime << "ms, max: " << metrics.maxQueryTime << "ms) | "
              << "Conn Time: " << (metrics.totalConnectionTime / metrics.totalConnections) << "ms (min: "
              << metrics.minConnectionTime << "ms, max: " << metrics.maxConnectionTime << "ms) | "
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
    int consecutiveErrors = 0;
    const int maxConsecutiveErrors = 5;
    
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
            
            // Add retry logic for connection failures
            godbc::Connection conn;
            int retryCount = 0;
            const int maxRetries = 3;
            
            while (retryCount < maxRetries) {
                try {
                    conn = godbc::ConnectionPool::getConnection(config.connectionString);
                    break; // Success, exit retry loop
                } catch (const std::exception& e) {
                    retryCount++;
                    if (retryCount >= maxRetries) {
                        throw; // Re-throw if we've exhausted retries
                    }
                    
                    // Wait before retrying, with exponential backoff
                    int backoffMs = 100 * (1 << (retryCount - 1));
                    if (config.verbose) {
                        std::cout << "[Thread " << threadId << "] Connection failed, retrying in " 
                                  << backoffMs << "ms (attempt " << retryCount << "/" << maxRetries << "): " 
                                  << e.what() << std::endl;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(backoffMs));
                }
            }
            
            metrics.totalConnections++;
            
            auto connEnd = std::chrono::high_resolution_clock::now();
            auto connTime = std::chrono::duration_cast<std::chrono::milliseconds>(connEnd - start);
            metrics.totalConnectionTime += connTime.count();
            
            // Update min/max connection times
            uint64_t currentConnTime = connTime.count();
            uint64_t currentMin;
            do {
                currentMin = metrics.minConnectionTime;
            } while (currentConnTime < currentMin && 
                    !metrics.minConnectionTime.compare_exchange_weak(currentMin, currentConnTime));
            
            uint64_t currentMax;
            do {
                currentMax = metrics.maxConnectionTime;
            } while (currentConnTime > currentMax && 
                    !metrics.maxConnectionTime.compare_exchange_weak(currentMax, currentConnTime));

            if (config.verbose) {
                std::cout << "[Thread " << threadId << "] Connected successfully in " << connTime.count() << "ms" << std::endl;
            }
            
            // Reset consecutive errors counter on successful connection
            consecutiveErrors = 0;
            
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
                    
                    // Update min/max query times
                    uint64_t currentQueryTime = queryTime.count();
                    uint64_t currentMin;
                    do {
                        currentMin = metrics.minQueryTime;
                    } while (currentQueryTime < currentMin && 
                            !metrics.minQueryTime.compare_exchange_weak(currentMin, currentQueryTime));
                    
                    uint64_t currentMax;
                    do {
                        currentMax = metrics.maxQueryTime;
                    } while (currentQueryTime > currentMax && 
                            !metrics.maxQueryTime.compare_exchange_weak(currentMax, currentQueryTime));
                    
                    if (config.verbose) {
                        std::cout << "[Thread " << threadId << "] Query completed in " << queryTime.count() << "ms" << std::endl;
                    }
                    
                    if (config.delayMs > 0) {
                        if (config.verbose) {
                            std::cout << "[Thread " << threadId << "] Sleeping for " << config.delayMs << "ms" << std::endl;
                        }
                        
                        // Print metrics before sleep
                        std::cout << "\nAfter query " << metrics.totalQueries << ":\n"
                                  << "  Query times (ms): min=" << metrics.minQueryTime 
                                  << ", avg=" << (metrics.totalExecutionTime / metrics.totalQueries)
                                  << ", max=" << metrics.maxQueryTime << "\n"
                                  << "  Connection times (ms): min=" << metrics.minConnectionTime
                                  << ", avg=" << (metrics.totalConnectionTime / metrics.totalConnections)
                                  << ", max=" << metrics.maxConnectionTime << "\n";
                        
                        // Handle large sleep times by breaking them into smaller chunks
                        int remainingSleep = config.delayMs;
                        const int maxSleepChunk = 1000; // 1 second chunks
                        
                        while (remainingSleep > 0) {
                            int sleepChunk = std::min(remainingSleep, maxSleepChunk);
                            std::this_thread::sleep_for(std::chrono::milliseconds(sleepChunk));
                            remainingSleep -= sleepChunk;
                            
                            // Check if we should stop during sleep
                            if (metrics.shouldStop) {
                                break;
                            }
                        }
                    }
                    
                } catch (const std::exception& e) {
                    metrics.failedQueries++;
                    std::cerr << "[Thread " << threadId << "] Error executing SQL from " << sqlFile << ": " << e.what() << std::endl;
                } catch (...) {
                    metrics.failedQueries++;
                    std::cerr << "[Thread " << threadId << "] Unknown error executing SQL from " << sqlFile << std::endl;
                }   
                
                metrics.totalQueries++;
            }
            
            if (config.verbose) {
                std::cout << "[Thread " << threadId << "] Closing connection" << std::endl;
            }
            conn.close();
            
        } catch (const std::exception& e) {
            std::cerr << "[Thread " << threadId << "] Thread error: " << e.what() << std::endl;
            consecutiveErrors++;
            
            // If we've had too many consecutive errors, pause this thread to allow system resources to be freed
            if (consecutiveErrors >= maxConsecutiveErrors) {
                std::cerr << "[Thread " << threadId << "] Too many consecutive errors, pausing for 30 seconds to allow system resources to be freed" << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(30));
                consecutiveErrors = 0;
            }
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
            try {
                // Use long long to handle large sleep values
                long long sleepMs = std::stoll(argv[++i]);
                if (sleepMs < 0) {
                    std::cerr << "Error: Sleep time cannot be negative" << std::endl;
                    return 1;
                }
                if (sleepMs > std::numeric_limits<int>::max()) {
                    std::cerr << "Error: Sleep time too large (max: " << std::numeric_limits<int>::max() << "ms)" << std::endl;
                    return 1;
                }
                config.delayMs = static_cast<int>(sleepMs);
            } catch (const std::exception& e) {
                std::cerr << "Error parsing sleep time: " << e.what() << std::endl;
                return 1;
            }
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
              << "Query times (ms):\n"
              << "  Average: " << (metrics.totalExecutionTime / metrics.totalQueries) << "\n"
              << "  Minimum: " << metrics.minQueryTime << "\n"
              << "  Maximum: " << metrics.maxQueryTime << "\n"
              << "Connection times (ms):\n"
              << "  Average: " << (metrics.totalConnectionTime / metrics.totalConnections) << "\n"
              << "  Minimum: " << metrics.minConnectionTime << "\n"
              << "  Maximum: " << metrics.maxConnectionTime << "\n"
              << "Queries per second: " << (metrics.totalQueries * 1000.0 / totalTime.count()) << "\n";
    
    // Check and increase file descriptor limits if possible
    struct rlimit rlim;
    if (getrlimit(RLIMIT_NOFILE, &rlim) == 0) {
        if (rlim.rlim_cur < rlim.rlim_max) {
            rlim.rlim_cur = rlim.rlim_max;
            if (setrlimit(RLIMIT_NOFILE, &rlim) == 0) {
                std::cout << "Increased file descriptor limit to " << rlim.rlim_cur << std::endl;
            } else {
                std::cerr << "Warning: Could not increase file descriptor limit: " << strerror(errno) << std::endl;
            }
        }
    } else {
        std::cerr << "Warning: Could not get file descriptor limit: " << strerror(errno) << std::endl;
    }
    
    return 0;
} 
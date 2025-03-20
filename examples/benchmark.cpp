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
#include "../cpp/godbc.hpp"

struct BenchmarkConfig {
    std::string connStr;
    int numQueries;
    int numThreads;
    int delayMs;
    bool useTransactions;
};

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " [options]\n"
              << "Options:\n"
              << "  -c, --connection <string>  Connection string (required)\n"
              << "  -n, --num-queries <int>    Number of queries to run (default: 1000)\n"
              << "  -t, --threads <int>        Number of threads (default: 1)\n"
              << "  -d, --delay <int>          Delay between queries in ms (default: 0)\n"
              << "  -x, --transactions         Use transactions (default: false)\n"
              << "  -h, --help                 Show this help message\n";
}

BenchmarkConfig parseArgs(int argc, char* argv[]) {
    BenchmarkConfig config{
        .connStr = "",
        .numQueries = 1000,
        .numThreads = 1,
        .delayMs = 0,
        .useTransactions = false
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
        }
    }

    if (config.connStr.empty()) {
        std::cerr << "Error: Connection string is required\n";
        printUsage(argv[0]);
        exit(1);
    }

    return config;
}

void runQueries(const BenchmarkConfig& config, int threadId, int queriesPerThread,
                std::atomic<int>& successCount, std::atomic<int>& errorCount,
                double& totalTime, std::mutex& timeMutex) {
    try {
        auto conn = godbc::ConnectionPool::getConnection(config.connStr);
        
        for (int i = 0; i < queriesPerThread; i++) {
            auto start = std::chrono::high_resolution_clock::now();
            
            try {
                if (config.useTransactions) {
                    auto tx = conn.beginTransaction();
                    tx.execute("SELECT @@VERSION");
                    tx.commit();
                } else {
                    conn.execute("SELECT @@VERSION");
                }
                
                if (config.delayMs > 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(config.delayMs));
                }
                
                auto end = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
                double queryTime = duration.count() / 1000.0; // Convert to milliseconds
                {
                    std::lock_guard<std::mutex> lock(timeMutex);
                    totalTime += queryTime;
                }
                successCount++;
            } catch (const std::exception& e) {
                std::cerr << "Thread " << threadId << " query " << i << " failed: " << e.what() << std::endl;
                errorCount++;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Thread " << threadId << " failed to connect: " << e.what() << std::endl;
        errorCount += queriesPerThread;
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
                  << "Using transactions: " << (config.useTransactions ? "yes" : "no") << "\n\n";
        
        std::vector<std::thread> threads;
        std::atomic<int> successCount(0);
        std::atomic<int> errorCount(0);
        double totalTime = 0;
        std::mutex timeMutex;
        
        int queriesPerThread = config.numQueries / config.numThreads;
        int remainingQueries = config.numQueries % config.numThreads;
        
        auto start = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < config.numThreads; i++) {
            int threadQueries = queriesPerThread + (i < remainingQueries ? 1 : 0);
            threads.emplace_back(runQueries, std::ref(config), i, threadQueries,
                               std::ref(successCount), std::ref(errorCount),
                               std::ref(totalTime), std::ref(timeMutex));
        }
        
        for (auto& thread : threads) {
            thread.join();
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        std::cout << "\nBenchmark Results:\n"
                  << "Total time: " << totalDuration.count() << "ms\n"
                  << "Successful queries: " << successCount << "\n"
                  << "Failed queries: " << errorCount << "\n"
                  << "Average query time: " << std::fixed << std::setprecision(2)
                  << (totalTime / successCount) << "ms\n"
                  << "Queries per second: " << std::fixed << std::setprecision(2)
                  << (successCount * 1000.0 / totalDuration.count()) << "\n";
        
        return errorCount > 0 ? 1 : 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
} 
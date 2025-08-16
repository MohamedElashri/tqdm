// benchmark.cpp 
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <random>
#include <algorithm>
#include <numeric>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <memory>
#include <atomic>
#include <cstring>

// Include tqdm from the repository root
#include "tqdm.h"

// Platform-specific includes for memory measurement
#ifdef __linux__
#include <sys/resource.h>
#include <unistd.h>
#endif

// C++11 compatibility helpers
namespace cpp11_compat {
    // make_unique for C++11
    template<typename T, typename... Args>
    std::unique_ptr<T> make_unique(Args&&... args) {
        return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
    }
}

// Benchmarking utilities
namespace benchmark {

// High-resolution timer
class Timer {
    using clock = std::chrono::high_resolution_clock;
    using time_point = clock::time_point;
    using duration = std::chrono::duration<double>;
    
    time_point start_;
    
public:
    Timer() : start_(clock::now()) {}
    
    void reset() {
        start_ = clock::now();
    }
    
    double elapsed() const {
        return duration(clock::now() - start_).count();
    }
    
    double elapsed_ns() const {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            clock::now() - start_
        ).count();
    }
};

// Memory usage tracker
class MemoryTracker {
#ifdef __linux__
    long initial_rss_;
    
    long get_current_rss() {
        std::ifstream stat_stream("/proc/self/stat", std::ios_base::in);
        std::string pid, comm, state, ppid, pgrp, session, tty_nr;
        std::string tpgid, flags, minflt, cminflt, majflt, cmajflt;
        std::string utime, stime, cutime, cstime, priority, nice;
        std::string O, itrealvalue, starttime;
        unsigned long vsize;
        long rss;
        
        stat_stream >> pid >> comm >> state >> ppid >> pgrp >> session >> tty_nr
                    >> tpgid >> flags >> minflt >> cminflt >> majflt >> cmajflt
                    >> utime >> stime >> cutime >> cstime >> priority >> nice
                    >> O >> itrealvalue >> starttime >> vsize >> rss;
        
        long page_size = sysconf(_SC_PAGE_SIZE);
        return rss * page_size;
    }
#endif
    
public:
    MemoryTracker() {
#ifdef __linux__
        initial_rss_ = get_current_rss();
#endif
    }
    
    long get_memory_usage() {
#ifdef __linux__
        return get_current_rss() - initial_rss_;
#else
        return -1; // Not supported on this platform
#endif
    }
    
    std::string format_bytes(long bytes) {
        if (bytes < 0) return "N/A";
        
        const char* units[] = {"B", "KB", "MB", "GB"};
        int unit_idx = 0;
        double size = static_cast<double>(bytes);
        
        while (size >= 1024 && unit_idx < 3) {
            size /= 1024;
            unit_idx++;
        }
        
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << size << " " << units[unit_idx];
        return oss.str();
    }
};

// Statistics calculator
class Statistics {
    std::vector<double> samples_;
    
public:
    void add_sample(double value) {
        samples_.push_back(value);
    }
    
    void clear() {
        samples_.clear();
    }
    
    double mean() const {
        if (samples_.empty()) return 0.0;
        double sum = std::accumulate(samples_.begin(), samples_.end(), 0.0);
        return sum / samples_.size();
    }
    
    double median() {
        if (samples_.empty()) return 0.0;
        std::vector<double> sorted = samples_;
        std::sort(sorted.begin(), sorted.end());
        size_t n = sorted.size();
        return n % 2 == 0 ? (sorted[n/2-1] + sorted[n/2]) / 2 : sorted[n/2];
    }
    
    double stddev() const {
        if (samples_.size() < 2) return 0.0;
        double m = mean();
        double sq_sum = std::accumulate(samples_.begin(), samples_.end(), 0.0,
            [m](double acc, double val) { 
                double diff = val - m;
                return acc + diff * diff; 
            });
        return std::sqrt(sq_sum / (samples_.size() - 1));
    }
    
    double min() const {
        return samples_.empty() ? 0.0 : *std::min_element(samples_.begin(), samples_.end());
    }
    
    double max() const {
        return samples_.empty() ? 0.0 : *std::max_element(samples_.begin(), samples_.end());
    }
    
    double percentile(double p) const {
        if (samples_.empty()) return 0.0;
        std::vector<double> sorted = samples_;
        std::sort(sorted.begin(), sorted.end());
        size_t idx = static_cast<size_t>(p * sorted.size() / 100.0);
        return sorted[std::min(idx, sorted.size() - 1)];
    }
};

// Benchmark result structure
struct BenchmarkResult {
    std::string name;
    size_t iterations;
    size_t threads;
    Statistics time_per_op;
    double total_time;
    long memory_usage;
    double ops_per_second;
};

// Result formatter
class ResultFormatter {
public:
    static void print_header() {
        std::cout << "\n"
                  << std::setw(30) << std::left << "Benchmark"
                  << std::setw(12) << std::right << "Iterations"
                  << std::setw(10) << "Threads"
                  << std::setw(15) << "Time/Op"
                  << std::setw(12) << "Std Dev"
                  << std::setw(12) << "Min"
                  << std::setw(12) << "Max"
                  << std::setw(15) << "Throughput"
                  << std::setw(12) << "Memory"
                  << "\n" << std::string(120, '-') << "\n";
    }
    
    static void print_result(const BenchmarkResult& result) {
        std::cout << std::setw(30) << std::left << result.name
                  << std::setw(12) << std::right << result.iterations
                  << std::setw(10) << result.threads
                  << std::setw(12) << format_time(result.time_per_op.mean()) << "/op"
                  << std::setw(12) << format_time(result.time_per_op.stddev())
                  << std::setw(12) << format_time(result.time_per_op.min())
                  << std::setw(12) << format_time(result.time_per_op.max())
                  << std::setw(12) << format_throughput(result.ops_per_second) << "/s";
        
        if (result.memory_usage >= 0) {
            MemoryTracker tracker;
            std::cout << std::setw(12) << tracker.format_bytes(result.memory_usage);
        } else {
            std::cout << std::setw(12) << "N/A";
        }
        std::cout << "\n";
    }
    
private:
    static std::string format_time(double seconds) {
        std::ostringstream oss;
        if (seconds < 1e-6) {
            oss << std::fixed << std::setprecision(1) << seconds * 1e9 << "ns";
        } else if (seconds < 1e-3) {
            oss << std::fixed << std::setprecision(1) << seconds * 1e6 << "µs";
        } else if (seconds < 1) {
            oss << std::fixed << std::setprecision(1) << seconds * 1e3 << "ms";
        } else {
            oss << std::fixed << std::setprecision(2) << seconds << "s";
        }
        return oss.str();
    }
    
    static std::string format_throughput(double ops_per_sec) {
        std::ostringstream oss;
        if (ops_per_sec >= 1e9) {
            oss << std::fixed << std::setprecision(2) << ops_per_sec / 1e9 << "G";
        } else if (ops_per_sec >= 1e6) {
            oss << std::fixed << std::setprecision(2) << ops_per_sec / 1e6 << "M";
        } else if (ops_per_sec >= 1e3) {
            oss << std::fixed << std::setprecision(2) << ops_per_sec / 1e3 << "K";
        } else {
            oss << std::fixed << std::setprecision(0) << ops_per_sec;
        }
        return oss.str();
    }
};

// Main benchmark runner
class BenchmarkRunner {
    static constexpr size_t WARMUP_ITERATIONS = 100;
    static constexpr size_t MIN_BENCHMARK_TIME = 1; // seconds
    static constexpr size_t MAX_SAMPLES = 1000;
    
public:
    template<typename Func>
    static BenchmarkResult run(const std::string& name, 
                              size_t iterations,
                              size_t threads,
                              Func&& benchmark_func) {
        std::cout << "Running: " << name << "... " << std::flush;
        
        // Warmup
        for (size_t i = 0; i < WARMUP_ITERATIONS && i < iterations / 10; ++i) {
            benchmark_func();
        }
        
        // Memory measurement
        MemoryTracker mem_tracker;
        
        // Time measurement
        Statistics stats;
        Timer total_timer;
        
        // Auto-adjust sample count based on operation speed
        size_t samples = 0;
        double elapsed = 0;
        
        while (elapsed < MIN_BENCHMARK_TIME && samples < MAX_SAMPLES) {
            Timer timer;
            benchmark_func();
            double op_time = timer.elapsed();
            stats.add_sample(op_time);
            samples++;
            elapsed = total_timer.elapsed();
        }
        
        double total_time = total_timer.elapsed();
        
        BenchmarkResult result;
        result.name = name;
        result.iterations = iterations;
        result.threads = threads;
        result.time_per_op = stats;
        result.total_time = total_time;
        result.memory_usage = mem_tracker.get_memory_usage();
        result.ops_per_second = samples / total_time;
        
        std::cout << "Done\n";
        return result;
    }
};

} // namespace benchmark

// Helper class to manage progress bars with proper ownership
class ProgressBarManager {
    std::unique_ptr<tqdm::progress_bar<>> bar_;
    
public:
    explicit ProgressBarManager(size_t total) 
        : bar_(new tqdm::progress_bar<>(total)) {}
    
    void advance(size_t n = 1) {
        bar_->advance(n);
    }
    
    void finish() {
        bar_->finish();
    }
};

// Benchmark implementations
void benchmark_single_thread_advance() {
    using namespace benchmark;
    std::vector<BenchmarkResult> results;
    
    // Test different iteration counts
    std::vector<size_t> iteration_counts = {1000, 10000, 100000, 1000000};
    
    for (auto iterations : iteration_counts) {
        auto result = BenchmarkRunner::run(
            "Single-thread advance(" + std::to_string(iterations) + ")",
            iterations,
            1,
            [iterations]() {
                // Redirect output to null
                std::streambuf* orig = std::cout.rdbuf();
                std::cout.rdbuf(nullptr);
                
                ProgressBarManager bar(iterations);
                for (size_t i = 0; i < iterations; ++i) {
                    bar.advance();
                }
                bar.finish();
                
                std::cout.rdbuf(orig);
            }
        );
        results.push_back(result);
    }
    
    // Batch updates
    for (auto iterations : iteration_counts) {
        size_t batch_size = std::max<size_t>(1, iterations / 1000);
        auto result = BenchmarkRunner::run(
            "Batch advance(" + std::to_string(iterations) + ", batch=" + 
            std::to_string(batch_size) + ")",
            iterations,
            1,
            [iterations, batch_size]() {
                std::streambuf* orig = std::cout.rdbuf();
                std::cout.rdbuf(nullptr);
                
                ProgressBarManager bar(iterations);
                for (size_t i = 0; i < iterations; i += batch_size) {
                    bar.advance(batch_size);
                }
                bar.finish();
                
                std::cout.rdbuf(orig);
            }
        );
        results.push_back(result);
    }
    
    // Print results
    ResultFormatter::print_header();
    for (const auto& result : results) {
        ResultFormatter::print_result(result);
    }
}

void benchmark_multi_thread_advance() {
    using namespace benchmark;
    std::vector<BenchmarkResult> results;
    
    size_t num_cores = std::thread::hardware_concurrency();
    std::vector<size_t> thread_counts = {2, 4, num_cores, num_cores * 2};
    std::vector<size_t> iteration_counts = {10000, 100000, 1000000};
    
    for (auto threads : thread_counts) {
        for (auto iterations : iteration_counts) {
            auto result = BenchmarkRunner::run(
                "Multi-thread advance(" + std::to_string(iterations) + ")",
                iterations,
                threads,
                [iterations, threads]() {
                    std::streambuf* orig = std::cout.rdbuf();
                    std::cout.rdbuf(nullptr);
                    
                    std::shared_ptr<ProgressBarManager> bar = 
                        std::make_shared<ProgressBarManager>(iterations);
                    std::vector<std::thread> workers;
                    size_t items_per_thread = iterations / threads;
                    
                    for (size_t t = 0; t < threads; ++t) {
                        workers.emplace_back([bar, items_per_thread]() {
                            for (size_t i = 0; i < items_per_thread; ++i) {
                                bar->advance();
                            }
                        });
                    }
                    
                    for (auto& worker : workers) {
                        worker.join();
                    }
                    
                    bar->finish();
                    std::cout.rdbuf(orig);
                }
            );
            results.push_back(result);
        }
    }
    
    // Print results
    std::cout << "\n\nMulti-threaded Performance:\n";
    ResultFormatter::print_header();
    for (const auto& result : results) {
        ResultFormatter::print_result(result);
    }
}

void benchmark_display_overhead() {
    using namespace benchmark;
    std::vector<BenchmarkResult> results;
    
    // Test display refresh rates
    size_t iterations = 100000;
    
    // With display
    auto with_display = BenchmarkRunner::run(
        "With display output",
        iterations,
        1,
        [iterations]() {
            ProgressBarManager bar(iterations);
            for (size_t i = 0; i < iterations; ++i) {
                bar.advance();
                // Force more frequent updates for testing
                if (i % 100 == 0) {
                    std::this_thread::sleep_for(std::chrono::microseconds(1));
                }
            }
        }
    );
    results.push_back(with_display);
    
    // Without display (null output)
    auto without_display = BenchmarkRunner::run(
        "Without display (null output)",
        iterations,
        1,
        [iterations]() {
            std::streambuf* orig = std::cout.rdbuf();
            std::cout.rdbuf(nullptr);
            
            ProgressBarManager bar(iterations);
            for (size_t i = 0; i < iterations; ++i) {
                bar.advance();
                if (i % 100 == 0) {
                    std::this_thread::sleep_for(std::chrono::microseconds(1));
                }
            }
            
            std::cout.rdbuf(orig);
        }
    );
    results.push_back(without_display);
    
    // Range-based loop
    std::vector<int> data(iterations);
    auto range_based = BenchmarkRunner::run(
        "Range-based for loop",
        iterations,
        1,
        [&data]() {
            std::streambuf* orig = std::cout.rdbuf();
            std::cout.rdbuf(nullptr);
            
            for (auto& item : tqdm::tqdm(data)) {
                // Minimal work
                item++;
            }
            
            std::cout.rdbuf(orig);
        }
    );
    results.push_back(range_based);
    
    // Print results
    std::cout << "\n\nDisplay Overhead:\n";
    ResultFormatter::print_header();
    for (const auto& result : results) {
        ResultFormatter::print_result(result);
    }
}

void benchmark_memory_usage() {
    using namespace benchmark;
    std::vector<BenchmarkResult> results;
    
    // Test memory usage with different progress bar counts
    std::vector<size_t> bar_counts = {1, 10, 100, 1000};
    
    for (auto count : bar_counts) {
        auto result = BenchmarkRunner::run(
            "Memory usage (" + std::to_string(count) + " bars)",
            1000,
            1,
            [count]() {
                std::streambuf* orig = std::cout.rdbuf();
                std::cout.rdbuf(nullptr);
                
                std::vector<std::shared_ptr<ProgressBarManager>> bars;
                for (size_t i = 0; i < count; ++i) {
                    bars.push_back(std::make_shared<ProgressBarManager>(1000));
                }
                
                // Update all bars
                for (size_t i = 0; i < 1000; ++i) {
                    for (auto& bar : bars) {
                        bar->advance();
                    }
                }
                
                std::cout.rdbuf(orig);
            }
        );
        results.push_back(result);
    }
    
    // Print results
    std::cout << "\n\nMemory Usage:\n";
    ResultFormatter::print_header();
    for (const auto& result : results) {
        ResultFormatter::print_result(result);
    }
}

// System information
void print_system_info() {
    std::cout << "System Information:\n";
    std::cout << "==================\n";
    
    // CPU info
    std::cout << "CPU Cores: " << std::thread::hardware_concurrency() << "\n";
    
    // Compiler info
    std::cout << "Compiler: ";
#ifdef __clang__
    std::cout << "Clang " << __clang_major__ << "." << __clang_minor__;
#elif defined(__GNUC__)
    std::cout << "GCC " << __GNUC__ << "." << __GNUC_MINOR__;
#else
    std::cout << "Unknown";
#endif
    std::cout << "\n";
    
    // C++ standard
    std::cout << "C++ Standard: ";
#if __cplusplus >= 202002L
    std::cout << "C++20";
#elif __cplusplus >= 201703L
    std::cout << "C++17";
#elif __cplusplus >= 201402L
    std::cout << "C++14";
#elif __cplusplus >= 201103L
    std::cout << "C++11";
#else
    std::cout << "Pre-C++11";
#endif
    std::cout << "\n";
    
    // Build type
    std::cout << "Build Type: ";
#ifdef NDEBUG
    std::cout << "Release";
#else
    std::cout << "Debug";
#endif
    std::cout << "\n\n";
}

int main(int, char*[]) {
    std::cout << "tqdm C++ Benchmark Suite\n";
    std::cout << "========================\n\n";
    
    print_system_info();
    
    // Run all benchmarks
    std::cout << "Starting benchmarks...\n";
    
    benchmark_single_thread_advance();
    benchmark_multi_thread_advance();
    benchmark_display_overhead();
    benchmark_memory_usage();
    
    std::cout << "\nBenchmark complete!\n";
    
    return 0;
}
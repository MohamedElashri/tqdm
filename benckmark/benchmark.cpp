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
#include <mutex>

#include "../tqdm.h"

#ifdef __linux__
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace benchmark {

// -------------------- Timer --------------------
class Timer {
    using clock = std::chrono::high_resolution_clock;
    using time_point = clock::time_point;
    using duration = std::chrono::duration<double>;
    time_point start_;
public:
    Timer() : start_(clock::now()) {}
    void reset() { start_ = clock::now(); }
    double elapsed() const { return duration(clock::now() - start_).count(); } // seconds
};

// -------------------- MemoryTracker --------------------
class MemoryTracker {
#ifdef __linux__
    long initial_rss_;
    static long get_current_rss() {
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
    static std::string format_bytes(long bytes) {
        if (bytes < 0) return "N/A";
        const char* units[] = {"B", "KB", "MB", "GB"};
        int unit_idx = 0;
        double size = static_cast<double>(bytes);
        while (size >= 1024 && unit_idx < 3) { size /= 1024; unit_idx++; }
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << size << " " << units[unit_idx];
        return oss.str();
    }
};

// -------------------- Statistics --------------------
class Statistics {
    std::vector<double> samples_; // seconds per run
public:
    void add_sample(double v) { samples_.push_back(v); }
    void clear() { samples_.clear(); }
    double mean() const {
        if (samples_.empty()) return 0.0;
        double s = std::accumulate(samples_.begin(), samples_.end(), 0.0);
        return s / samples_.size();
    }
    double stddev() const {
        if (samples_.size() < 2) return 0.0;
        double m = mean();
        double sq = 0.0;
        for (double v : samples_) { double d=v-m; sq += d*d; }
        return std::sqrt(sq / (samples_.size() - 1));
    }
    double min() const {
        return samples_.empty() ? 0.0 : *std::min_element(samples_.begin(), samples_.end());
    }
    double max() const {
        return samples_.empty() ? 0.0 : *std::max_element(samples_.begin(), samples_.end());
    }
};

// -------------------- BenchmarkResult --------------------
struct BenchmarkResult {
    std::string name;
    size_t iterations{0};
    size_t threads{1};
    Statistics run_time;                 // stats for full-run durations (s)
    double total_benchmark_time{0.0};    // s, wall over sampling
    long memory_usage{-1};               // bytes
    // Derived per-update stats:
    double mean_update_s{0.0};
    double stddev_update_s{0.0};
    double min_update_s{0.0};
    double max_update_s{0.0};
    double updates_per_second{0.0};

    // Baseline comparison (optional)
    bool has_baseline{false};
    double baseline_mean_update_s{0.0};
    double delta_update_s{0.0}; // mean_update_s - baseline_mean_update_s
};

// -------------------- Formatting helpers --------------------
static std::string format_seconds(double s) {
    std::ostringstream oss;
    if (s < 1e-6) {
        oss << std::fixed << std::setprecision(1) << s * 1e9 << "ns";
    } else if (s < 1e-3) {
        oss << std::fixed << std::setprecision(1) << s * 1e6 << "us";
    } else if (s < 1.0) {
        oss << std::fixed << std::setprecision(1) << s * 1e3 << "ms";
    } else {
        oss << std::fixed << std::setprecision(2) << s << "s";
    }
    return oss.str();
}

static std::string format_throughput(double upd_per_sec) {
    std::ostringstream oss;
    if (upd_per_sec >= 1e9)      { oss << std::fixed << std::setprecision(2) << upd_per_sec / 1e9 << " G"; }
    else if (upd_per_sec >= 1e6) { oss << std::fixed << std::setprecision(2) << upd_per_sec / 1e6 << " M"; }
    else if (upd_per_sec >= 1e3) { oss << std::fixed << std::setprecision(2) << upd_per_sec / 1e3 << " K"; }
    else                         { oss << std::fixed << std::setprecision(0) << upd_per_sec; }
    return oss.str();
}

// -------------------- ResultFormatter --------------------
class ResultFormatter {
public:
    static void print_header() {
        std::cout << "\n"
                  << std::setw(36) << std::left << "Benchmark"
                  << std::setw(13) << std::right << "Iterations"
                  << std::setw(10) << "Threads"
                  << std::setw(16) << "Mean/update"
                  << std::setw(14) << "StdDev/update"
                  << std::setw(14) << "Min/update"
                  << std::setw(14) << "Max/update"
                  << std::setw(18) << "Delta vs base"
                  << std::setw(14) << "upd/s"
                  << std::setw(12) << "Memory"
                  << "\n" << std::string(161, '-') << "\n";
    }

    static void print_result(const BenchmarkResult& r) {
        std::cout << std::setw(36) << std::left << r.name
                  << std::setw(13) << std::right << add_commas(r.iterations)
                  << std::setw(10) << r.threads
                  << std::setw(16) << format_seconds(r.mean_update_s)
                  << std::setw(14) << format_seconds(r.stddev_update_s)
                  << std::setw(14) << format_seconds(r.min_update_s)
                  << std::setw(14) << format_seconds(r.max_update_s);

        if (r.has_baseline) {
            std::ostringstream d; d << format_seconds(r.delta_update_s);
            std::cout << std::setw(18) << d.str();
        } else {
            std::cout << std::setw(18) << "-";
        }

        std::ostringstream t;
        t << format_throughput(r.updates_per_second) << " upd/s";
        std::cout << std::setw(14) << t.str();

        std::cout << std::setw(12) << MemoryTracker::format_bytes(r.memory_usage)
                  << "\n";
    }

private:
    static std::string add_commas(size_t x) {
        std::string s = std::to_string(x);
        for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(static_cast<size_t>(i), ",");
        return s;
    }
};

// -------------------- Runner --------------------
class BenchmarkRunner {
    static constexpr size_t WARMUP_ITER = 50;
    static constexpr double MIN_TIME_S = 1.0;
    static constexpr size_t MAX_SAMPLES = 1000;

public:
    template<typename Func>
    static BenchmarkResult run(const std::string& name,
                               size_t iterations,
                               size_t threads,
                               Func&& fn) {
        // Warmup
        for (size_t i = 0; i < WARMUP_ITER; ++i) fn();

        MemoryTracker mem;
        Statistics stats;
        Timer wall;
        size_t samples = 0;

        while (wall.elapsed() < MIN_TIME_S && samples < MAX_SAMPLES) {
            Timer t;
            fn(); // one full run
            stats.add_sample(t.elapsed());
            ++samples;
        }

        double mean_run_s = stats.mean();
        double stddev_run_s = stats.stddev();
        double min_run_s = stats.min();
        double max_run_s = stats.max();

        BenchmarkResult r;
        r.name = name;
        r.iterations = iterations;
        r.threads = threads;
        r.run_time = stats;
        r.total_benchmark_time = wall.elapsed();
        r.memory_usage = mem.get_memory_usage();

        // Derive per-update stats
        if (iterations > 0) {
            r.mean_update_s   = mean_run_s / static_cast<double>(iterations);
            r.stddev_update_s = stddev_run_s / static_cast<double>(iterations);
            r.min_update_s    = min_run_s / static_cast<double>(iterations);
            r.max_update_s    = max_run_s / static_cast<double>(iterations);
            r.updates_per_second = static_cast<double>(iterations) / mean_run_s;
        }
        return r;
    }
};

} // namespace benchmark

// -------------------- Utilities for baselines --------------------
static inline void spin_empty_work(size_t iters) {
    volatile size_t sink = 0;
    for (size_t i = 0; i < iters; ++i) sink += i;
}

// Baseline for batch-stepped loops: i += step
static inline void spin_empty_work_batched(size_t total, size_t step) {
    volatile size_t sink = 0;
    for (size_t i = 0; i < total; i += step) sink += i;
}

// -------------------- Null display (tracker-only) --------------------
class null_display : public tqdm::display_policy {
public:
    void render(const tqdm::progress_tracker<>&) override {}
    void finish(const tqdm::progress_tracker<>&) override {}
};

// -------------------- ProgressBarManager with pluggable display --------------------
class ProgressBarManager {
    std::unique_ptr<tqdm::progress_bar<>> bar_;
public:
    explicit ProgressBarManager(size_t total, std::unique_ptr<tqdm::display_policy> disp = nullptr) {
        if (disp) {
            bar_.reset(new tqdm::progress_bar<>(total, std::move(disp)));
        } else {
            bar_.reset(new tqdm::progress_bar<>(total)); // default display (TTY-aware)
        }
    }
    void advance(size_t n = 1) { bar_->advance(n); }
    void finish() { bar_->finish(); }
};

// -------------------- Sections --------------------
using benchmark::BenchmarkRunner;
using benchmark::BenchmarkResult;
using benchmark::ResultFormatter;

static void print_system_info() {
    std::cout << "System Information:\n";
    std::cout << "==================\n";
    std::cout << "CPU Cores: " << std::thread::hardware_concurrency() << "\n";
    std::cout << "Compiler: ";
#ifdef __clang__
    std::cout << "Clang " << __clang_major__ << "." << __clang_minor__;
#elif defined(__GNUC__)
    std::cout << "GCC " << __GNUC__ << "." << __GNUC_MINOR__;
#else
    std::cout << "Unknown";
#endif
    std::cout << "\n";
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
    std::cout << "Build Type: ";
#ifdef NDEBUG
    std::cout << "Release";
#else
    std::cout << "Debug";
#endif
    std::cout << "\n\n";
}

static void benchmark_single_thread() {
    using namespace benchmark;
    std::vector<BenchmarkResult> out;

    std::vector<size_t> iters = {1000, 10000, 100000, 1000000};

    for (auto n : iters) {
        // Baseline
        auto base = BenchmarkRunner::run(
            "Baseline loop (" + std::to_string(n) + ")",
            n, 1, [n]() { spin_empty_work(n); }
        );

        // Tracker-only (null display)
        auto r = BenchmarkRunner::run(
            "Single-thread advance(" + std::to_string(n) + ") [tracker-only]",
            n, 1, [n]() {
                ProgressBarManager bar(n, std::unique_ptr<tqdm::display_policy>(new null_display()));
                for (size_t i = 0; i < n; ++i) bar.advance();
                bar.finish();
            }
        );
        r.has_baseline = true;
        r.baseline_mean_update_s = base.mean_update_s;
        r.delta_update_s = r.mean_update_s - base.mean_update_s;
        out.push_back(r);
    }

    // Batch updates: same total updates, fewer calls
    for (auto n : iters) {
        size_t batch = std::max<size_t>(1, n / 1000);
        auto base = BenchmarkRunner::run(
            "Baseline batched (" + std::to_string(n) + ", step=" + std::to_string(batch) + ")",
            n, 1, [n, batch]() { spin_empty_work_batched(n, batch); }
        );

        auto r = BenchmarkRunner::run(
            "Batch advance(" + std::to_string(n) + ", batch=" + std::to_string(batch) + ") [tracker-only]",
            n, 1, [n, batch]() {
                ProgressBarManager bar(n, std::unique_ptr<tqdm::display_policy>(new null_display()));
                for (size_t i = 0; i < n; i += batch) bar.advance(batch);
                bar.finish();
            }
        );
        r.has_baseline = true;
        r.baseline_mean_update_s = base.mean_update_s;
        r.delta_update_s = r.mean_update_s - base.mean_update_s;
        out.push_back(r);
    }

    ResultFormatter::print_header();
    for (const auto& r : out) ResultFormatter::print_result(r);
}

static void benchmark_multi_thread() {
    using namespace benchmark;
    std::vector<BenchmarkResult> out;

    const size_t cores = std::max(1u, std::thread::hardware_concurrency());
    std::vector<size_t> thread_counts = {2, 4, cores, cores * 2};
    std::vector<size_t> iters = {10000, 100000, 1000000};

    for (auto th : thread_counts) {
        for (auto n : iters) {
            // Baseline multi-thread
            auto base = BenchmarkRunner::run(
                "Baseline MT (" + std::to_string(n) + ", t=" + std::to_string(th) + ")",
                n, th, [n, th]() {
                    size_t per = n / th;
                    std::vector<std::thread> ws;
                    ws.reserve(th);
                    for (size_t t = 0; t < th; ++t) {
                        ws.emplace_back([per]() { spin_empty_work(per); });
                    }
                    for (auto& w : ws) w.join();
                }
            );

            auto r = BenchmarkRunner::run(
                "Multi-thread advance(" + std::to_string(n) + ") [tracker-only]",
                n, th, [n, th]() {
                    size_t per = n / th;
                    auto bar = std::make_shared<ProgressBarManager>(n, std::unique_ptr<tqdm::display_policy>(new null_display()));
                    std::vector<std::thread> ws;
                    ws.reserve(th);
                    for (size_t t = 0; t < th; ++t) {
                        ws.emplace_back([bar, per]() {
                            for (size_t i = 0; i < per; ++i) bar->advance();
                        });
                    }
                    for (auto& w : ws) w.join();
                    bar->finish();
                }
            );
            r.has_baseline = true;
            r.baseline_mean_update_s = base.mean_update_s;
            r.delta_update_s = r.mean_update_s - base.mean_update_s;
            out.push_back(r);
        }
    }

    std::cout << "\n\nMulti-threaded Performance (tracker-only):\n";
    ResultFormatter::print_header();
    for (const auto& r : out) ResultFormatter::print_result(r);
}

static void benchmark_tracker_vs_display() {
    using namespace benchmark;
    std::vector<BenchmarkResult> out;
    const size_t n = 100000;

    // Baseline
    auto base = BenchmarkRunner::run("Baseline loop (" + std::to_string(n) + ")", n, 1, [n]() { spin_empty_work(n); });

    // Tracker-only
    auto tracker_only = BenchmarkRunner::run(
        "Tracker-only (null display)", n, 1, [n]() {
            ProgressBarManager bar(n, std::unique_ptr<tqdm::display_policy>(new null_display()));
            for (size_t i = 0; i < n; ++i) bar.advance();
            bar.finish();
        }
    );
    tracker_only.has_baseline = true;
    tracker_only.baseline_mean_update_s = base.mean_update_s;
    tracker_only.delta_update_s = tracker_only.mean_update_s - base.mean_update_s;
    out.push_back(tracker_only);

    // Standard display (TTY-aware; throttled inside)
    auto std_display = BenchmarkRunner::run(
        "Standard display (TTY-aware)", n, 1, [n]() {
            ProgressBarManager bar(n); // default display
            for (size_t i = 0; i < n; ++i) {
                bar.advance();
                if (i % 100 == 0) { std::this_thread::sleep_for(std::chrono::microseconds(1)); }
            }
            bar.finish();
        }
    );
    std_display.has_baseline = true;
    std_display.baseline_mean_update_s = base.mean_update_s;
    std_display.delta_update_s = std_display.mean_update_s - base.mean_update_s;
    out.push_back(std_display);

    std::cout << "\n\nTracker vs Display:\n";
    ResultFormatter::print_header();
    for (const auto& r : out) ResultFormatter::print_result(r);
}

static void benchmark_memory_usage() {
    using namespace benchmark;
    std::vector<BenchmarkResult> out;

    std::vector<size_t> counts = {1, 10, 100, 1000};
    for (auto c : counts) {
        auto r = BenchmarkRunner::run(
            "Memory usage (" + std::to_string(c) + " bars)",
            1000, 1, [c]() {
                std::vector<std::shared_ptr<ProgressBarManager>> bars;
                bars.reserve(c);
                for (size_t i = 0; i < c; ++i) {
                    bars.push_back(std::make_shared<ProgressBarManager>(1000, std::unique_ptr<tqdm::display_policy>(new null_display())));
                }
                for (size_t i = 0; i < 1000; ++i) {
                    for (auto& b : bars) b->advance();
                }
                for (auto& b : bars) b->finish();
            }
        );
        out.push_back(r);
    }

    std::cout << "\n\nMemory Usage:\n";
    ResultFormatter::print_header();
    for (const auto& r : out) ResultFormatter::print_result(r);
}

int main(int, char*[]) {
    std::cout << "tqdm C++ Benchmark Suite\n";
    std::cout << "========================\n\n";
    print_system_info();

    std::cout << "Starting benchmarks...\n";
    benchmark_single_thread();
    benchmark_multi_thread();
    benchmark_tracker_vs_display();
    benchmark_memory_usage();

    std::cout << "\nBenchmark complete!\n";
    return 0;
}

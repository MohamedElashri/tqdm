// tqdm.hpp - Complete implementation
#ifndef TQDM_HPP
#define TQDM_HPP

// Detect C++ standard
#if __cplusplus < 201103L
#  error "tqdm++ requires at least C++11"
#elif __cplusplus >= 202002L
#  define TQDM_CPP20
#elif __cplusplus >= 201703L
#  define TQDM_CPP17
#elif __cplusplus >= 201402L
#  define TQDM_CPP14
#else
#  define TQDM_CPP11
#endif

#include <atomic>
#include <chrono>
#include <thread>
#include <mutex>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <array>
#include <vector>
#include <cmath>
#include <sstream>
#include <memory>
#include <iterator>
#include <type_traits>
#include <cstring>
#include <limits>

#ifdef TQDM_CPP17
#  include <optional>
#  include <string_view>
#  include <shared_mutex>
#endif

#ifdef TQDM_CPP20
#  include <concepts>
#  include <ranges>
#  include <span>
#endif

// Unix-specific headers
#include <unistd.h>
#include <sys/ioctl.h>

namespace tqdm {

// Forward declarations
template<typename T> class progress_bar;
template<typename ContainerT> class progress_range;

// =============================================================================
// Strong Type System
// =============================================================================

template<typename T, typename Tag>
class strong_type {
    T value_;
public:
    explicit strong_type(T value) : value_(value) {}
    T get() const { return value_; }
    operator T() const { return value_; }

    strong_type& operator+=(const strong_type& other) {
        value_ += other.value_;
        return *this;
    }

    strong_type operator+(const strong_type& other) const {
        return strong_type(value_ + other.value_);
    }

    bool operator<(const strong_type& other) const { return value_ < other.value_; }
    bool operator<=(const strong_type& other) const { return value_ <= other.value_; }
    bool operator>(const strong_type& other) const { return value_ > other.value_; }
    bool operator>=(const strong_type& other) const { return value_ >= other.value_; }
    bool operator==(const strong_type& other) const { return value_ == other.value_; }
    bool operator!=(const strong_type& other) const { return value_ != other.value_; }
};

using progress_t = strong_type<std::size_t, struct progress_tag>;
using percentage_t = strong_type<double, struct percentage_tag>;
using rate_hz_t = strong_type<double, struct rate_tag>;

// =============================================================================
// Utility Functions
// =============================================================================

inline int get_terminal_width() {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) return w.ws_col;
    return 80; // Default fallback
}

inline bool is_tty() {
    return isatty(STDOUT_FILENO) != 0;
}

inline std::string format_time(std::chrono::milliseconds ms) {
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(ms).count();
    auto minutes = seconds / 60;
    auto hours = minutes / 60;

    std::ostringstream oss;
    if (hours > 0) {
        oss << hours << "h" << (minutes % 60) << "m";
    } else if (minutes > 0) {
        oss << minutes << "m" << (seconds % 60) << "s";
    } else {
        oss << seconds << "s";
    }
    return oss.str();
}

inline std::string format_rate(double rate) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1);

    if (rate >= 1e9) {
        oss << rate / 1e9 << " G/s";
    } else if (rate >= 1e6) {
        oss << rate / 1e6 << " M/s";
    } else if (rate >= 1e3) {
        oss << rate / 1e3 << " K/s";
    } else {
        oss << rate << " /s";
    }
    return oss.str();
}

// =============================================================================
struct rgb { int r, g, b; };

inline rgb hsv_to_rgb(double h, double s, double v) {
    if (s < 1e-6) {
        int val = static_cast<int>(v * 255);
        return {val, val, val};
    }
    int i = static_cast<int>(h * 6.0);
    double f = (h * 6.0) - i;
    int p = static_cast<int>(255.0 * v * (1.0 - s));
    int q = static_cast<int>(255.0 * v * (1.0 - s * f));
    int t = static_cast<int>(255.0 * v * (1.0 - s * (1.0 - f)));
    int vi = static_cast<int>(v * 255);
    i %= 6;
    switch(i) {
        case 0: return {vi, t, p};
        case 1: return {q, vi, p};
        case 2: return {p, vi, t};
        case 3: return {p, q, vi};
        case 4: return {t, p, vi};
        case 5: return {vi, p, q};
    }
    return {0, 0, 0};
}

// =============================================================================
// Theme System
// =============================================================================

struct theme {
    std::array<const char*, 9> blocks;
    const char* right_pad;
    const char* left_bracket;
    const char* right_bracket;

#ifdef TQDM_CPP14
    constexpr
#endif
    theme(std::array<const char*, 9> b, const char* rp,
          const char* lb = "", const char* rb = "")
        : blocks(b), right_pad(rp), left_bracket(lb), right_bracket(rb) {}
};

namespace themes {
#ifdef TQDM_CPP14
    constexpr
#endif
    theme unicode{
        {{" ", ".", ":", "-", "=", "#", "#", "#", "#"}},
        "|"
    };

#ifdef TQDM_CPP14
    constexpr
#endif
    theme ascii{
        {{" ", "-", "-", "=", "=", "=", "#", "#", "#"}},
        "|", "[", "]"
    };

#ifdef TQDM_CPP14
    constexpr
#endif
    theme circles{
        {{" ", ".", "o", "o", "o", "o", "o", "o", "O"}},
        " "
    };

#ifdef TQDM_CPP14
    constexpr
#endif
    theme braille{
        {{" ", ".", ".", ":", ":", ":", "*", "*", "*"}},
        " "
    };
}

// =============================================================================
// Thread-Safe Progress Tracker
// =============================================================================

template<typename ClockT = std::chrono::steady_clock>
class progress_tracker {
private:
    std::atomic<std::size_t> current_{0};
    std::atomic<std::size_t> total_{0};
    const typename ClockT::time_point start_time_;

    static constexpr std::size_t HISTORY_SIZE = 64;
    struct alignas(64) history_entry {
        std::atomic<std::size_t> progress{0};
        std::atomic<int64_t> timestamp{0};
    };
    mutable std::array<history_entry, HISTORY_SIZE> history_;
    std::atomic<std::size_t> history_index_{0};

    struct stats_cache {
        double rate{0.0};
        std::chrono::milliseconds elapsed{0};
        typename ClockT::time_point last_update;
    };
    mutable stats_cache cache_;

#ifdef TQDM_CPP17
    mutable std::shared_mutex cache_mutex_;
#else
    mutable std::mutex cache_mutex_;
#endif

public:
    explicit progress_tracker(std::size_t total)
        : total_(total), start_time_(ClockT::now()) {
        history_[0].progress.store(0);
        history_[0].timestamp.store(0);
    }

    void advance(std::size_t n = 1) noexcept {
        auto new_progress = current_.fetch_add(n, std::memory_order_relaxed) + n;
        auto idx = history_index_.fetch_add(1, std::memory_order_relaxed) % HISTORY_SIZE;
        auto now = ClockT::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(now - start_time_).count();
        history_[idx].progress.store(new_progress, std::memory_order_relaxed);
        history_[idx].timestamp.store(timestamp, std::memory_order_relaxed);
    }

    void set_total(std::size_t total) noexcept { total_.store(total, std::memory_order_relaxed); }
    std::size_t current() const noexcept { return current_.load(std::memory_order_relaxed); }
    std::size_t total() const noexcept { return total_.load(std::memory_order_relaxed); }

    double percentage() const noexcept {
        auto t = total_.load(std::memory_order_relaxed);
        if (t == 0) return 0.0;
        auto c = current_.load(std::memory_order_relaxed);
        return std::min(100.0 * static_cast<double>(c) / static_cast<double>(t), 100.0);
    }

    double get_rate() const noexcept {
        auto now = ClockT::now();

        {
#ifdef TQDM_CPP17
            std::shared_lock<std::shared_mutex> lock(cache_mutex_);
#else
            std::lock_guard<std::mutex> lock(cache_mutex_);
#endif
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - cache_.last_update).count() < 100) {
                return cache_.rate;
            }
        }

        std::size_t oldest_idx = 0;
        int64_t oldest_time = std::numeric_limits<int64_t>::max();
        std::size_t newest_idx = 0;
        int64_t newest_time = 0;

        auto current_idx = history_index_.load(std::memory_order_relaxed);
        std::size_t entries_to_check = std::min<std::size_t>(HISTORY_SIZE, current_idx);

        for (std::size_t i = 0; i < entries_to_check; ++i) {
            auto time = history_[i].timestamp.load(std::memory_order_relaxed);
            if (time > 0) {
                if (time < oldest_time) {
                    oldest_time = time;
                    oldest_idx = i;
                }
                if (time > newest_time) {
                    newest_time = time;
                    newest_idx = i;
                }
            }
        }

        double rate = 0.0;
        if (newest_time > oldest_time && newest_idx != oldest_idx) {
            auto progress_diff = history_[newest_idx].progress.load() - history_[oldest_idx].progress.load();
            auto time_diff = newest_time - oldest_time;
            if (time_diff > 0) {
                rate = 1e6 * static_cast<double>(progress_diff) / static_cast<double>(time_diff);
            }
        }

        {
            std::lock_guard<std::mutex> lock(cache_mutex_);
            cache_.rate = rate;
            cache_.last_update = now;
            cache_.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_);
        }
        return rate;
    }

    std::chrono::milliseconds elapsed() const noexcept {
        return std::chrono::duration_cast<std::chrono::milliseconds>(ClockT::now() - start_time_);
    }

    std::chrono::milliseconds eta() const noexcept {
        auto rate = get_rate();
        if (rate <= 0) return std::chrono::milliseconds(0);
        auto remaining = total_.load() - current_.load();
        auto eta_seconds = static_cast<double>(remaining) / rate;
        return std::chrono::milliseconds(static_cast<int64_t>(eta_seconds * 1000));
    }
};

// =============================================================================
// Display Policy System
// =============================================================================

class display_policy {
public:
    virtual ~display_policy() = default;
    virtual void render(const progress_tracker<>& tracker) = 0;
    virtual void finish(const progress_tracker<>& tracker) = 0;
};

template<typename ThemeT = decltype(themes::unicode)>
class bar_display : public display_policy {
private:
    ThemeT theme_;
    std::size_t width_;
    bool use_color_;
    std::atomic<int> last_width_{0};
    std::string label_;
    bool show_rate_;
    bool show_eta_;
    bool show_percentage_;

public:
    bar_display(ThemeT theme = themes::unicode,
                std::size_t width = 40,
                bool use_color = true,
                bool show_rate = true,
                bool show_eta = true,
                bool show_percentage = true)
        : theme_(theme)
        , width_(width)
        , use_color_(use_color && is_tty())
        , show_rate_(show_rate)
        , show_eta_(show_eta)
        , show_percentage_(show_percentage) {}

    void set_label(const std::string& label) { label_ = label; }

    void render(const progress_tracker<>& tracker) override {
        std::ostringstream oss;

        if (!label_.empty()) oss << label_ << ": ";

        auto percentage = tracker.percentage();

        if (show_percentage_) {
            oss << std::setw(3) << std::fixed << std::setprecision(0) << percentage << "% ";
        }

        if (use_color_) {
            auto color = hsv_to_rgb(percentage / 300.0, 0.8, 1.0);
            oss << "\033[38;2;" << color.r << ";" << color.g << ";" << color.b << "m";
        }

        oss << theme_.left_bracket;

        double fills = (percentage / 100.0) * width_;
        int whole_fills = static_cast<int>(fills);
        double fraction = fills - whole_fills;

        for (int i = 0; i < whole_fills && i < static_cast<int>(width_); ++i) {
            oss << theme_.blocks[8];
        }

        if (whole_fills < static_cast<int>(width_)) {
            int frac_idx = static_cast<int>(fraction * 8);
            if (frac_idx < 0) frac_idx = 0;
            if (frac_idx > 8) frac_idx = 8;
            oss << theme_.blocks[frac_idx];
            for (int i = whole_fills + 1; i < static_cast<int>(width_); ++i) {
                oss << theme_.blocks[0];
            }
        }

        oss << theme_.right_bracket;

        if (use_color_) oss << "\033[0m";

        oss << theme_.right_pad << " ";

        oss << tracker.current() << "/" << tracker.total();

        if (show_rate_) {
            auto rate = tracker.get_rate();
            oss << " [" << format_rate(rate);
        }

        if (show_rate_ || show_eta_) {
            auto elapsed = tracker.elapsed();
            oss << ", " << format_time(elapsed);
            if (show_eta_ && percentage < 100.0) {
                auto eta = tracker.eta();
                oss << "<" << format_time(eta);
            }
            oss << "]";
        }

        std::string output = oss.str();
        auto current_width = static_cast<int>(output.length());
        auto last = last_width_.exchange(current_width);
        if (last > current_width) output.append(last - current_width, ' ');

        std::cout << '\r' << output << std::flush;
    }

    void finish(const progress_tracker<>& tracker) override {
        render(tracker);
        std::cout << '\n';
    }
};

// =============================================================================
// Main Progress Bar Class
// =============================================================================

template<typename T = std::size_t>
class progress_bar {
private:
    std::unique_ptr<progress_tracker<>> tracker_;
    std::unique_ptr<display_policy> display_;
    std::atomic<bool> finished_{false};

    std::atomic<int64_t> last_render_time_{0};
    static constexpr auto min_render_interval = std::chrono::milliseconds(33);

    std::mutex render_mutex_;

public:
    explicit progress_bar(T total,
                          std::unique_ptr<display_policy> display = nullptr)
        : tracker_(new progress_tracker<>(static_cast<std::size_t>(total)))
        , display_(display ? std::move(display)
                           : std::unique_ptr<display_policy>(new bar_display<decltype(themes::unicode)>())) {
        if (is_tty()) {
            std::lock_guard<std::mutex> lock(render_mutex_);
            display_->render(*tracker_);
        }
    }

    // Custom move constructor, do not move mutex
    progress_bar(progress_bar&& other) noexcept
        : tracker_(std::move(other.tracker_))
        , display_(std::move(other.display_))
        , finished_(other.finished_.load(std::memory_order_relaxed))
        , last_render_time_(other.last_render_time_.load(std::memory_order_relaxed)) {
        other.finished_.store(true, std::memory_order_relaxed);
    }

    // Custom move assignment, do not move mutex
    progress_bar& operator=(progress_bar&& other) noexcept {
        if (this != &other) {
            tracker_ = std::move(other.tracker_);
            display_ = std::move(other.display_);
            finished_.store(other.finished_.load(std::memory_order_relaxed), std::memory_order_relaxed);
            last_render_time_.store(other.last_render_time_.load(std::memory_order_relaxed), std::memory_order_relaxed);
            other.finished_.store(true, std::memory_order_relaxed);
        }
        return *this;
    }

    ~progress_bar() {
        if (!finished_.load() && is_tty()) finish();
    }

    // Disable copy
    progress_bar(const progress_bar&) = delete;
    progress_bar& operator=(const progress_bar&) = delete;

    progress_bar& operator++() { advance(1); return *this; }
    progress_bar& operator+=(std::size_t n) { advance(n); return *this; }

    void advance(std::size_t n = 1) {
        if (tracker_) tracker_->advance(n);
        try_render();
    }

    void set_label(const std::string& label) {
        if (auto* bar_disp = dynamic_cast<bar_display<>*>(display_.get())) {
            bar_disp->set_label(label);
        }
    }

    void finish() {
        bool expected = false;
        if (finished_.compare_exchange_strong(expected, true)) {
            if (is_tty() && tracker_ && display_) {
                std::lock_guard<std::mutex> lock(render_mutex_);
                display_->finish(*tracker_);
            }
        }
    }

    std::size_t current() const { return tracker_ ? tracker_->current() : 0; }
    std::size_t total() const { return tracker_ ? tracker_->total() : 0; }
    double percentage() const { return tracker_ ? tracker_->percentage() : 0.0; }
    double rate() const { return tracker_ ? tracker_->get_rate() : 0.0; }

private:
    void try_render() {
        if (!is_tty() || !tracker_ || !display_) return;

        auto now = std::chrono::steady_clock::now();
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        auto last_ms = last_render_time_.load(std::memory_order_relaxed);

        if (now_ms - last_ms >= min_render_interval.count()) {
            if (last_render_time_.compare_exchange_weak(last_ms, now_ms)) {
                std::lock_guard<std::mutex> lock(render_mutex_);
                display_->render(*tracker_);
            }
        }
    }

    void force_render() {
        if (is_tty() && tracker_ && display_) {
            std::lock_guard<std::mutex> lock(render_mutex_);
            display_->render(*tracker_);
        }
    }
};

// C++17: Class template argument deduction guides
#ifdef TQDM_CPP17
progress_bar(std::size_t) -> progress_bar<std::size_t>;
progress_bar(int) -> progress_bar<int>;
template<typename T>
progress_bar(T, std::unique_ptr<display_policy>) -> progress_bar<T>;
#endif

// =============================================================================
// Iterator Wrapper
// =============================================================================

template<typename IterT, typename ProgressBarT>
class progress_iterator {
    IterT current_;
    IterT end_;
    ProgressBarT* bar_;

public:
    using iterator_category = typename std::iterator_traits<IterT>::iterator_category;
    using value_type = typename std::iterator_traits<IterT>::value_type;
    using difference_type = typename std::iterator_traits<IterT>::difference_type;
    using pointer = typename std::iterator_traits<IterT>::pointer;
    using reference = typename std::iterator_traits<IterT>::reference;

    progress_iterator(IterT current, IterT end, ProgressBarT* bar)
        : current_(current), end_(end), bar_(bar) {}

    progress_iterator& operator++() {
        ++current_;
        if (bar_ && current_ != end_) bar_->advance();
        return *this;
    }

    progress_iterator operator++(int) {
        progress_iterator tmp = *this;
        ++(*this);
        return tmp;
    }

    reference operator*() const { return *current_; }
    pointer operator->() const { return &(*current_); }

    bool operator==(const progress_iterator& other) const { return current_ == other.current_; }
    bool operator!=(const progress_iterator& other) const { return !(*this == other); }
};

// =============================================================================
// Range Wrapper
// =============================================================================

template<typename ContainerT>
class progress_range {
    using iterator_t = decltype(std::begin(std::declval<ContainerT&>()));
    using const_iterator_t = decltype(std::begin(std::declval<const ContainerT&>()));

    ContainerT* container_;
    mutable progress_bar<> bar_;

public:
    explicit progress_range(ContainerT& container)
        : container_(&container)
        , bar_(static_cast<std::size_t>(std::distance(std::begin(container), std::end(container)))) {}

    // Move constructor
    progress_range(progress_range&& other) noexcept
        : container_(other.container_)
        , bar_(std::move(other.bar_)) {
        other.container_ = nullptr;
    }

    auto begin() -> progress_iterator<iterator_t, progress_bar<>> {
        return progress_iterator<iterator_t, progress_bar<>>(std::begin(*container_), std::end(*container_), &bar_);
    }

    auto end() -> progress_iterator<iterator_t, progress_bar<>> {
        return progress_iterator<iterator_t, progress_bar<>>(std::end(*container_), std::end(*container_), nullptr);
    }

    auto begin() const -> progress_iterator<const_iterator_t, progress_bar<>> {
        return progress_iterator<const_iterator_t, progress_bar<>>(std::begin(*container_), std::end(*container_), &bar_);
    }

    auto end() const -> progress_iterator<const_iterator_t, progress_bar<>> {
        return progress_iterator<const_iterator_t, progress_bar<>>(std::end(*container_), std::end(*container_), nullptr);
    }

    progress_bar<>& get_bar() { return bar_; }
};

// =============================================================================
// Factory Functions
// =============================================================================

template<typename ContainerT>
inline auto tqdm(ContainerT& container) -> progress_range<ContainerT> {
    progress_range<ContainerT> range(container);
    return range;
}

// Overload for custom label
template<typename ContainerT>
inline auto tqdm(ContainerT& container, const std::string& label) -> progress_range<ContainerT> {
    progress_range<ContainerT> range(container);
    range.get_bar().set_label(label);
    return range;
}

// Create a manual progress bar
inline progress_bar<> tqdm_manual(std::size_t total) {
    return progress_bar<>(total);
}

template<typename ThemeT>
inline progress_bar<> tqdm_manual(std::size_t total, ThemeT theme) {
    return progress_bar<>(total, std::unique_ptr<display_policy>(new bar_display<ThemeT>(theme)));
}

// =============================================================================
// C++14 and beyond enhancements
// =============================================================================

#ifdef TQDM_CPP14
// Make function for progress bars
template<typename... Args>
inline auto make_progress_bar(Args&&... args) {
    return progress_bar<>(std::forward<Args>(args)...);
}
#endif

// =============================================================================
// C++17 specific features
// =============================================================================

#ifdef TQDM_CPP17
template<typename ExecutionPolicy, typename ContainerT, typename Func>
void parallel_for_each_with_progress(ExecutionPolicy&& policy,
                                    ContainerT& container,
                                    Func func) {
    auto pbar = tqdm_manual(container.size());
    std::mutex pbar_mutex;

    std::for_each(std::forward<ExecutionPolicy>(policy),
                  container.begin(), container.end(),
                  [&func, &pbar, &pbar_mutex](auto&& item) {
                      func(item);
                      std::lock_guard<std::mutex> lock(pbar_mutex);
                      pbar.advance();
                  });
}
#endif

// =============================================================================
// C++20 specific features
// =============================================================================

#ifdef TQDM_CPP20

template<typename T>
concept ProgressType = std::integral<T> || std::floating_point<T>;

template<typename R>
concept ProgressRange = std::ranges::forward_range<R> &&
    requires(R r) {
        { std::ranges::size(r) } -> std::convertible_to<std::size_t>;
    };

namespace views {
struct with_progress_fn {
    template<ProgressRange R>
    auto operator()(R&& range) const {
        return tqdm(std::forward<R>(range));
    }
};
inline constexpr with_progress_fn with_progress{};
} // namespace views

#endif

} // namespace tqdm

#endif // TQDM_HPP

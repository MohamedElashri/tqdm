# tqdm++ - Thread-Safe Progress Bars for Modern C++

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![C++11](https://img.shields.io/badge/C%2B%2B-11/14/17/20-blue.svg)](https://en.cppreference.com/)
[![Unix](https://img.shields.io/badge/platform-Unix/Linux/macOS-green.svg)](https://en.wikipedia.org/wiki/Unix)

A modern, header-only C++ progress bar library inspired by Python's tqdm. Features thread-safe operation, zero dependencies, and progressive enhancement from C++11 to C++20.

## Features

-  **Header-only** - Just include and use
-  **Thread-safe by default** - Safe progress tracking from multiple threads
-  **Multiple themes** - Unicode, ASCII, circles, braille patterns
-  **Minimal overhead** - Template-based design with inline optimizations
-  **Progressive enhancement** - Uses C++11 baseline, adds features up to C++20

## Table of Contents

- [Installation](#installation)
- [Basic Usage](#basic-usage)
- [Advanced Usage](#advanced-usage)
- [Use Cases](#use-cases)
- [API Reference](#api-reference)
- [Performance](#performance)
- [Compatibility](#compatibility)
- [License](#license)

## Installation

### Single Header

Simply download `tqdm.h` and include it in your project:

```cpp
#include "tqdm.h"
```

### CMake

```cmake
# FetchContent (CMake 3.11+)
include(FetchContent)
FetchContent_Declare(
    tqdm
    GIT_REPOSITORY https://github.com/MohamedElashri/tqdm.git
    GIT_TAG main
)
FetchContent_MakeAvailable(tqdm)

target_link_libraries(your_target PRIVATE tqdm::tqdm)
```

### Manual Installation

```bash
# Clone the repository
git clone https://github.com/MohamedElashri/tqdm.git

# Copy the header to your project
cp tqdm-cpp/include/tqdm.h /path/to/your/project/
```

## Basic Usage

### Range-based For Loop

The simplest way to add a progress bar to your loops:

```cpp
#include "tqdm.h"
#include <vector>

int main() {
    std::vector<int> data(1000);
    
    // Basic progress bar
    for (auto& item : tqdm::tqdm(data)) {
        // Your processing here
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // With a label
    for (auto& item : tqdm::tqdm(data, "Processing")) {
        // Your processing here
    }
    
    return 0;
}
```

Output:
```
Processing: 100% ████████████████████ 1000/1000 [12.5 K/s, 1m20s]
```

### Manual Control

For cases where you need fine-grained control:

```cpp
auto bar = tqdm::tqdm_manual(1000);
bar.set_label("Downloading");

for (int i = 0; i < 1000; ++i) {
    // Simulate work
    download_chunk(i);
    
    // Update progress
    bar.advance();
}
bar.finish();  // Optional - destructor will handle it
```

### Custom Increments

```cpp
auto bar = tqdm::tqdm_manual(total_bytes);

while (bytes_processed < total_bytes) {
    size_t chunk = process_next_chunk();
    bar.advance(chunk);  // Advance by chunk size
    bytes_processed += chunk;
}
```

## Advanced Usage

### Custom Themes

Choose from built-in themes or create your own:

```cpp
// Built-in themes
auto bar1 = tqdm::tqdm_manual(100, tqdm::themes::unicode);  // Default
auto bar2 = tqdm::tqdm_manual(100, tqdm::themes::ascii);    // ASCII-only
auto bar3 = tqdm::tqdm_manual(100, tqdm::themes::circles);  // ◓◑◒◐
auto bar4 = tqdm::tqdm_manual(100, tqdm::themes::braille);  // ⡏⡟⡿⣿

// Custom theme
tqdm::theme custom_theme{
    {{" ", "·", ":", "!", "|", "┃", "┃", "█", "█"}},  // blocks
    " ",      // right pad
    "[", "]"  // brackets
};
auto bar5 = tqdm::tqdm_manual(100, custom_theme);
```

### Custom Display Policy

Create your own display format:

```cpp
class minimal_display : public tqdm::display_policy {
public:
    void render(const tqdm::progress_tracker<>& tracker) override {
        std::cout << "\r" << tracker.current() << "/" << tracker.total() 
                  << std::flush;
    }
    
    void finish(const tqdm::progress_tracker<>& tracker) override {
        render(tracker);
        std::cout << " Done!\n";
    }
};

auto bar = tqdm::progress_bar<>(100, 
    std::make_unique<minimal_display>());
```

### Thread-Safe Parallel Processing

```cpp
#include <thread>
#include <vector>

void parallel_example() {
    const size_t num_items = 10000;
    const size_t num_threads = std::thread::hardware_concurrency();
    
    auto bar = tqdm::tqdm_manual(num_items);
    std::vector<std::thread> threads;
    
    size_t items_per_thread = num_items / num_threads;
    
    for (size_t t = 0; t < num_threads; ++t) {
        threads.emplace_back([&bar, items_per_thread]() {
            for (size_t i = 0; i < items_per_thread; ++i) {
                // Simulate work
                process_item();
                
                // Thread-safe progress update
                bar.advance();
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
}
```

### C++17 Parallel Algorithms

```cpp
#ifdef __cpp_lib_parallel_algorithm
#include <execution>

void parallel_stl_example() {
    std::vector<int> data(1000);
    
    tqdm::parallel_for_each_with_progress(
        std::execution::par,
        data,
        [](int& item) {
            // Process item
            expensive_operation(item);
        }
    );
}
#endif
```

## Use Cases

### 1. File Processing

```cpp
void process_files(const std::vector<std::string>& files) {
    for (const auto& file : tqdm::tqdm(files, "Processing files")) {
        auto content = read_file(file);
        auto result = transform_content(content);
        write_file(file + ".processed", result);
    }
}
```

### 2. Network Downloads

```cpp
void download_files(const std::vector<url>& urls) {
    auto bar = tqdm::tqdm_manual(urls.size());
    bar.set_label("Downloading");
    
    for (const auto& url : urls) {
        try {
            download(url);
            bar.advance();
        } catch (const std::exception& e) {
            // Progress bar continues even if individual downloads fail
            std::cerr << "\nFailed: " << url << ": " << e.what() << "\n";
            bar.advance();  // Still count failed downloads
        }
    }
}
```

### 3. Data Processing Pipeline

```cpp
template<typename Container>
void processing_pipeline(Container& data) {
    // Stage 1: Preprocessing
    for (auto& item : tqdm::tqdm(data, "Preprocessing")) {
        preprocess(item);
    }
    
    // Stage 2: Main processing
    auto bar = tqdm::tqdm_manual(data.size());
    bar.set_label("Processing");
    
    size_t processed = 0;
    for (auto& item : data) {
        if (needs_processing(item)) {
            heavy_computation(item);
        }
        bar.advance();
    }
    
    // Stage 3: Post-processing
    for (auto& item : tqdm::tqdm(data, "Finalizing")) {
        finalize(item);
    }
}
```

### 4. Scientific Computing

```cpp
void monte_carlo_simulation(size_t iterations) {
    auto bar = tqdm::tqdm_manual(iterations);
    bar.set_label("Monte Carlo");
    
    double sum = 0.0;
    double sum_sq = 0.0;
    
    for (size_t i = 0; i < iterations; ++i) {
        double sample = random_sample();
        sum += sample;
        sum_sq += sample * sample;
        
        if (i % 1000 == 0) {  // Update every 1000 iterations
            bar.advance(1000);
            
            // Can also update label with current statistics
            double mean = sum / (i + 1);
            double variance = (sum_sq / (i + 1)) - mean * mean;
            
            std::ostringstream oss;
            oss << "MC (μ=" << std::fixed << std::setprecision(3) 
                << mean << ", σ²=" << variance << ")";
            bar.set_label(oss.str());
        }
    }
}
```

### 5. Machine Learning Training

```cpp
void train_model(Model& model, const Dataset& data, size_t epochs) {
    for (size_t epoch = 0; epoch < epochs; ++epoch) {
        auto bar = tqdm::tqdm_manual(data.num_batches());
        
        std::ostringstream label;
        label << "Epoch " << (epoch + 1) << "/" << epochs;
        bar.set_label(label.str());
        
        for (size_t batch = 0; batch < data.num_batches(); ++batch) {
            auto loss = model.train_batch(data.get_batch(batch));
            bar.advance();
            
            // Could update label with loss if desired
        }
    }
}
```

### 6. Nested Progress Bars

```cpp
void nested_operation() {
    std::vector<std::string> categories(5);
    
    for (auto& category : tqdm::tqdm(categories, "Categories")) {
        std::cout << "\n";  // New line for inner progress bar
        
        std::vector<int> items(100);
        for (auto& item : tqdm::tqdm(items, "  Items")) {
            process_item(category, item);
        }
    }
}
```

## API Reference

### Main Functions

#### `tqdm::tqdm(container [, label])`
Creates a progress bar for a container.
- **Parameters:**
  - `container`: Any container with begin/end iterators
  - `label` (optional): String label for the progress bar
- **Returns:** Progress range wrapper

#### `tqdm::tqdm_manual(total [, theme])`
Creates a manually controlled progress bar.
- **Parameters:**
  - `total`: Total number of iterations
  - `theme` (optional): Display theme
- **Returns:** `progress_bar` object

### Progress Bar Methods

#### `advance(n = 1)`
Advances the progress bar by n steps.

#### `set_label(label)`
Sets the display label.

#### `finish()`
Finalizes the progress bar (called automatically on destruction).

#### `current()`, `total()`, `percentage()`, `rate()`
Get current progress statistics.

### Themes

- `tqdm::themes::unicode` - Unicode blocks (default)
- `tqdm::themes::ascii` - ASCII characters only
- `tqdm::themes::circles` - Circle symbols
- `tqdm::themes::braille` - Braille patterns

## Performance


### Overhead at a glance

* Single-thread call to `advance()`: \~0.5 us per update.
* With on-screen display enabled: +\~0.05 us per update (same order of magnitude).
* Many threads updating the same bar (8–16 threads): \~1.0–1.5 us per update.
* Multiple bars updated in the same tick: \~0.5 us per bar; total cost adds linearly.
* Batching: amortized overhead per element ≈ 0.5 us / batch\_size.

Quick guide:

```
Batch size   Overhead per element
1            ~0.5 us
10           ~0.05 us
100          ~0.005 us
1000         ~0.0005 us
```

Rule of thumb:

```
Work per item    Overhead share (single-thread)
0.5 us           ~100%  -> batch updates
5 us             ~10%
50 us            ~1%
500 us           ~0.1%
```

Recommended default: update every 1–10 ms or every \~10^3 items, whichever comes first.

### Tips for Optimal Performance

1. **Batch Updates**: For very fast loops, update every N iterations:
   ```cpp
   auto bar = tqdm::tqdm_manual(1000000);
   for (size_t i = 0; i < 1000000; ++i) {
       fast_operation();
       if (i % 1000 == 999) {
           bar.advance(1000);
       }
   }
   ```

2. **Disable Display**: For benchmarking or non-TTY environments:
   ```cpp
   if (!tqdm::is_tty()) {
       // Progress tracking continues but no display
   }
   ```

## Compatibility

### C++ Standard Support

| Feature | C++11 | C++14 | C++17 | C++20 |
|---------|-------|-------|-------|-------|
| Basic progress bars | ✓ | ✓ | ✓ | ✓ |
| Thread safety | ✓ | ✓ | ✓ | ✓ |
| Range-based for loops | ✓ | ✓ | ✓ | ✓ |
| Perfect forwarding | | ✓ | ✓ | ✓ |
| Class template argument deduction | | | ✓ | ✓ |
| Parallel algorithm support | | | ✓ | ✓ |
| Concepts | | | | ✓ |
| Ranges support | | | | ✓ |

### Compiler Support

- GCC 4.8+
- Clang 3.4+
- MSVC 2015+ (Unix subsystem)
- ICC 15.0+

### Platform Support

- Linux
- macOS
- BSD variants
- WSL/WSL2
- Any POSIX-compliant system

## Thread Safety

All progress tracking operations are thread-safe by default:

- `advance()` uses atomic operations
- Rate calculation uses a lock-free ring buffer
- Display updates are mutex-protected and throttled

Multiple threads can safely update the same progress bar without external synchronization.

## Contributing

Contributions are welcome! Please feel free to submit pull requests or open issues.

## License

This project is licensed under the GNU General Public License v3.0 - see the [LICENSE](LICENSE) file for details.

## Acknowledgments

- Inspired by the Python [tqdm](https://github.com/tqdm/tqdm) library


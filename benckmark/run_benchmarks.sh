#!/bin/bash
# run_benchmarks.sh

echo "Building tqdm benchmark suite..."
make clean
make

echo -e "\n=== Running standard benchmark ==="
./tqdm_benchmark

echo -e "\n=== Running with different optimization levels ==="
for opt in O0 O1 O2 O3 Os; do
    echo -e "\nOptimization level: -$opt"
    make clean > /dev/null 2>&1
    make CXXFLAGS="-std=c++11 -$opt -Wall -Wextra -pthread" > /dev/null 2>&1
    ./tqdm_benchmark | grep -E "(Single-thread advance\(1000000\)|Time/Op)"
done

echo -e "\n=== Testing different C++ standards ==="
for std in c++11 c++14 c++17 c++20; do
    if make clean > /dev/null 2>&1 && make CXXFLAGS="-std=$std -O3 -Wall -Wextra -pthread" > /dev/null 2>&1; then
        echo -e "\nC++ Standard: $std"
        ./tqdm_benchmark | grep -E "(Single-thread advance\(1000000\)|Time/Op)"
    fi
done

echo -e "\nBenchmark complete!"
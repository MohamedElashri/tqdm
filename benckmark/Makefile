# Makefile for tqdm benchmark
CXX = g++
CXXFLAGS = -std=c++11 -O3 -Wall -Wextra -pthread -march=native
LDFLAGS = -pthread

# Debug build flags
DEBUG_FLAGS = -g -O0 -DDEBUG -fsanitize=thread

# Source files
SOURCES = benchmark.cpp
EXECUTABLE = tqdm_benchmark

# Default target
all: $(EXECUTABLE)

$(EXECUTABLE): $(SOURCES)
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $(EXECUTABLE) $(LDFLAGS)

# Debug build
debug: CXXFLAGS += $(DEBUG_FLAGS)
debug: LDFLAGS += -fsanitize=thread
debug: $(EXECUTABLE)

# Run benchmark
run: $(EXECUTABLE)
	./$(EXECUTABLE)

# Run with performance monitoring
perf: $(EXECUTABLE)
	perf stat -d ./$(EXECUTABLE)

# Clean
clean:
	rm -f $(EXECUTABLE)

# Install (fetches tqdm.h if not present)
install-deps:
	@if [ ! -f tqdm.h ]; then \
		echo "Downloading tqdm.h..."; \
		curl -O https://raw.githubusercontent.com/MohamedElashri/tqdm/main/tqdm.h; \
	fi

.PHONY: all debug run perf clean install-deps
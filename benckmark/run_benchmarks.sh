#!/usr/bin/env bash
# run_benchmarks.sh
#
# Usage:
#   ./benckmark/run_benchmarks.sh                # regular run with tee logging
#   ./benckmark/run_benchmarks.sh --pty          # re-exec under a PTY for realistic progress-bar behavior
#
# Notes:
# - --pty writes an additional full terminal transcript alongside the raw log.
# - On Linux (util-linux script), uses: script -q -e -c "cmd" file
# - On macOS/BSD (BSD script), uses:   script -q -e -t 0 file cmd

set -euo pipefail

# ---------- args ----------
USE_PTY=0
ORIG_ARGS=()
for arg in "$@"; do
  case "$arg" in
    --pty) USE_PTY=1 ;;
    *) ORIG_ARGS+=("$arg") ;;
  esac
done

# ---------- time stamp and logs ----------
mkdir -p logs
TS="$(date -u +%Y%m%dT%H%M%SZ)"
RAW_LOG="logs/bench_${TS}.raw.txt"
TTY_LOG="logs/bench_${TS}.tty.txt"

# ---------- optional PTY re-exec ----------
if [[ "${USE_PTY}" -eq 1 && "${RB_PTY_WRAP:-}" != "1" ]]; then
  # Detect util-linux vs BSD script
  if script -V 2>&1 | grep -qi "util-linux"; then
    # Linux
    export RB_PTY_WRAP=1
    exec script -q -e -c "$0 ${ORIG_ARGS[*]}" "$TTY_LOG"
  else
    # BSD/macOS
    export RB_PTY_WRAP=1
    exec script -q -e -t 0 "$TTY_LOG" "$0" "${ORIG_ARGS[@]}"
  fi
fi

# ---------- logging to RAW_LOG via tee if attached to a TTY ----------
if [ -t 1 ]; then
  exec > >(tee -a "$RAW_LOG") 2>&1
else
  exec >>"$RAW_LOG" 2>&1
fi

# ---------- toolchain detection ----------
# Prefer existing CXX, else clang++ if present, else g++
CXX_BIN="${CXX:-}"
if [[ -z "${CXX_BIN}" ]]; then
  if command -v clang++ >/dev/null 2>&1; then
    CXX_BIN="clang++"
  elif command -v g++ >/dev/null 2>&1; then
    CXX_BIN="g++"
  else
    printf "No C++ compiler found (clang++ or g++).\n" >&2
    exit 1
  fi
fi
export CXX="${CXX_BIN}"

# Arch flags: prefer -mcpu=native on arm64, -march=native otherwise
ARCH_FLAGS="-march=native"
if uname -m | grep -qi "arm64\|aarch64"; then
  ARCH_FLAGS="-mcpu=native"
fi

BASE_WARN="-Wall -Wextra -pthread"
STD_DEFAULT="-std=c++11"

# ---------- build and run ----------
printf "Building tqdm benchmark suite...\n"
make clean
make

printf "\n=== Running standard benchmark ===\n"
./tqdm_benchmark

printf "\n=== Running with different optimization levels (per-update) ===\n"
for opt in O0 O1 O2 O3 Os; do
  printf "\nOptimization level: -%s\n" "$opt"
  make clean >/dev/null 2>&1
  if [ "$opt" = "O0" ]; then
    make CXXFLAGS="$STD_DEFAULT -O0 ${BASE_WARN} ${ARCH_FLAGS}" >/dev/null 2>&1
  else
    make CXXFLAGS="$STD_DEFAULT -${opt} -DNDEBUG ${BASE_WARN} ${ARCH_FLAGS}" >/dev/null 2>&1
  fi
  ./tqdm_benchmark | grep -E "(Single-thread .*1,000,000|Mean/update|Delta vs base|upd/s)" || true
done

printf "\n=== Testing different C++ standards (O3, -DNDEBUG) ===\n"
for std in c++11 c++14 c++17 c++20; do
  if make clean >/dev/null 2>&1 && \
     make CXXFLAGS="-std=${std} -O3 -DNDEBUG ${BASE_WARN} ${ARCH_FLAGS}" >/dev/null 2>&1; then
    printf "\nC++ Standard: %s\n" "$std"
    ./tqdm_benchmark | grep -E "(Single-thread .*1,000,000|Mean/update|Delta vs base|upd/s)" || true
  fi
done

printf "\nBenchmark complete!\n"
printf "Raw log: %s\n" "$RAW_LOG"
if [[ "${RB_PTY_WRAP:-}" == "1" ]]; then
  printf "TTY log: %s\n" "$TTY_LOG"
fi

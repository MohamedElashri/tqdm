#!/usr/bin/env bash
# run_benchmarks.sh
#

set -euo pipefail

mkdir -p logs
TS="$(date -u +%Y%m%dT%H%M%SZ)"
RAW_LOG="logs/bench_${TS}.raw.txt"

# Detect util-linux vs BSD script once
if script -V 2>&1 | grep -qi "util-linux"; then
  SCRIPT_FLAVOR="linux"
else
  SCRIPT_FLAVOR="bsd"
fi

# Print to terminal and append to log
log() {
  # %b expands backslash escapes, so "\n..." works
  printf "%b" "$1" | tee -a "$RAW_LOG"
  # Add trailing newline if caller didn't include it
  case "$1" in
    *$'\n') : ;;
    *) echo >>"$RAW_LOG"; echo ;;
  esac
}

# Run a shell command under a PTY and tee to the log.
# Returns the child exit code.
run_sh_logged() {
  local cmd="$1"
  set -o pipefail
  if [[ "$SCRIPT_FLAVOR" == "linux" ]]; then
    # util-linux script: -c "..." form, -f = flush typescript
    script -q -e -f -c "bash -lc \"$cmd\"" /dev/null 2>&1 | tee -a "$RAW_LOG"
    return ${PIPESTATUS[0]}
  else
    # BSD/macOS script: file first, then argv...; use bash -lc to interpret the pipeline
    script -q -e -t 0 /dev/null bash -lc "$cmd" 2>&1 | tee -a "$RAW_LOG"
    return ${PIPESTATUS[0]}
  fi
}

# Convenience wrapper for simple commands without pipelines if you prefer
run_logged() {
  run_sh_logged "$1"
}

# Toolchain prefs (optional, but handy)
# Prefer existing $CXX, else clang++, else g++
if [[ -z "${CXX:-}" ]]; then
  if command -v clang++ >/dev/null 2>&1; then
    export CXX=clang++
  elif command -v g++ >/dev/null 2>&1; then
    export CXX=g++
  fi
fi

# Arch flags: arm64 uses -mcpu=native, others -march=native
ARCH_FLAGS="-march=native"
if uname -m | grep -qiE 'arm64|aarch64'; then
  ARCH_FLAGS="-mcpu=native"
fi
BASE_WARN="-Wall -Wextra -pthread"
STD_DEFAULT="-std=c++11"

# -------------------- Build and run --------------------

log "Building tqdm benchmark suite..."
run_logged "make clean"
run_sh_logged "make"

log "\n=== Running standard benchmark ==="
run_logged "./tqdm_benchmark"

log "\n=== Running with different optimization levels (per-update) ==="
for opt in O0 O1 O2 O3 Os; do
  log "\nOptimization level: -$opt"
  run_logged "make clean >/dev/null 2>&1"
  if [[ "$opt" == "O0" ]]; then
    run_sh_logged "make CXXFLAGS='$STD_DEFAULT -O0 $BASE_WARN $ARCH_FLAGS' >/dev/null 2>&1"
  else
    run_sh_logged "make CXXFLAGS='$STD_DEFAULT -$opt -DNDEBUG $BASE_WARN $ARCH_FLAGS' >/dev/null 2>&1"
  fi
  # Filter the summary lines live
  run_sh_logged "./tqdm_benchmark | grep -E '(Single-thread .*1,000,000|Mean/update|Delta vs base|upd/s)'" || true
done

log "\n=== Testing different C++ standards (O3, -DNDEBUG) ==="
for std in c++11 c++14 c++17 c++20; do
  if run_sh_logged "make clean >/dev/null 2>&1 && make CXXFLAGS='-std=$std -O3 -DNDEBUG $BASE_WARN $ARCH_FLAGS' >/dev/null 2>&1"; then
    log "\nC++ Standard: $std"
    run_sh_logged "./tqdm_benchmark | grep -E '(Single-thread .*1,000,000|Mean/update|Delta vs base|upd/s)'" || true
  fi
done

log "\nBenchmark complete!"
log "Log: $RAW_LOG"

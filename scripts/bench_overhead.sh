#!/usr/bin/env bash
# ==============================================================================
# SysPulse Overhead Benchmark (Sampling Frequency Sweep)
# ==============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
OUT_CSV="${ROOT_DIR}/out/bench_overhead.csv"

mkdir -p "${ROOT_DIR}/out"

echo "interval_ms,duration_s,samples_collected,samples_dropped,avg_cpu_pct,max_rss_kb" > "${OUT_CSV}"

INTERVALS=(1000 500 250 100 50)
DURATION=10

echo "Running SysPulse overhead benchmarks across sampling frequencies..."

for INTERVAL in "${INTERVALS[@]}"; do
    echo "  -> Benchmarking interval: ${INTERVAL}ms (duration: ${DURATION}s)..."
    NDJSON="${ROOT_DIR}/out/bench_${INTERVAL}ms.ndjson"

    "${ROOT_DIR}/build/syspulse_agent" \
        --interval-ms "${INTERVAL}" \
        --duration-s "${DURATION}" \
        --out "${NDJSON}" \
        --self-stats > /dev/null 2>&1

    # Extract metrics from output NDJSON using python
    python3 -c "
import json
with open('${NDJSON}') as f:
    lines = [json.loads(line) for line in f if line.strip()]
cpu_vals = [s.get('self_stats', {}).get('cpu_pct', 0.0) for s in lines]
rss_vals = [s.get('self_stats', {}).get('rss_bytes', 0) / 1024.0 for s in lines]
avg_cpu = sum(cpu_vals) / len(cpu_vals) if cpu_vals else 0.0
max_rss = max(rss_vals) if rss_vals else 0.0
dropped = lines[-1].get('self_stats', {}).get('ring_buffer_dropped_samples', 0) if lines else 0
print(f'${INTERVAL},${DURATION},{len(lines)},{dropped},{avg_cpu:.2f},{max_rss:.1f}')
" >> "${OUT_CSV}"
    rm -f "${NDJSON}"
done

echo "Benchmark complete. Results written to ${OUT_CSV}:"
cat "${OUT_CSV}"

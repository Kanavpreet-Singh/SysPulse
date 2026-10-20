#!/usr/bin/env bash
# ==============================================================================
# SysPulse Automated Fault-Injection & Diagnostic Experiment Runner (Linux)
# ==============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
OUT_DIR="${ROOT_DIR}/out"

mkdir -p "${OUT_DIR}"

BUILD_DIR="${ROOT_DIR}/build"
if [[ ! -f "${BUILD_DIR}/syspulse_agent" ]]; then
    echo "[!] syspulse_agent binary not found. Please compile the project first."
    exit 1
fi

echo "======================================================================"
echo " Starting SysPulse End-to-End Experiment (Linux)"
echo "======================================================================"

SAMPLES_FILE="${OUT_DIR}/experiment_samples.ndjson"
REPORT_DIR="${OUT_DIR}/report"

# 1. Start Telemetry Agent in background
echo "[*] Launching syspulse_agent..."
"${BUILD_DIR}/syspulse_agent" \
    --interval-ms 200 \
    --duration-s 15 \
    --out "${SAMPLES_FILE}" \
    --self-stats \
    --filter-name "mem_leak" &
AGENT_PID=$!

sleep 1

# 2. Start Fault-Injection Workload (mem_leak + cpu_spin)
echo "[*] Launching fault generators (mem_leak: 8 MB/s, cpu_spin: 75% duty)..."
"${BUILD_DIR}/mem_leak" --rate-mb 8 --interval-ms 200 --duration-s 10 &
FAULT_PID=$!

"${BUILD_DIR}/cpu_spin" --threads 2 --duty 75 --duration-s 8 &
CPU_PID=$!

# 3. Wait for workloads and agent completion
echo "[*] Waiting for experiment run to complete..."
wait "${FAULT_PID}" || true
wait "${CPU_PID}" || true
wait "${AGENT_PID}" || true

echo "[*] Telemetry collection complete: ${SAMPLES_FILE}"

# 4. Run Python Analysis Engine
echo "[*] Executing Python analysis pipeline..."
export PYTHONPATH="${ROOT_DIR}/analysis"
python3 -m syspulse_analysis report "${SAMPLES_FILE}" --out "${REPORT_DIR}"

echo "======================================================================"
echo " Experiment Successful. Diagnostic Report: ${REPORT_DIR}/DIAGNOSTIC_REPORT.md"
echo "======================================================================"

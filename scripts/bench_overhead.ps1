# ==============================================================================
# SysPulse Overhead Benchmark (Windows PowerShell)
# ==============================================================================
$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = Split-Path -Parent $ScriptDir
$OutDir = Join-Path $RootDir "out"

if (-not (Test-Path $OutDir)) {
    New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
}

$OutCsv = Join-Path $OutDir "bench_overhead.csv"
"interval_ms,duration_s,samples_collected,samples_dropped,avg_cpu_pct,max_rss_kb" | Out-File -FilePath $OutCsv -Encoding utf8

$AgentExe = Join-Path $RootDir "build\syspulse_agent.exe"
$Intervals = @(1000, 500, 250, 100)
$Duration = 5

Write-Host "Running SysPulse overhead benchmarks across sampling frequencies..." -ForegroundColor Cyan

foreach ($Interval in $Intervals) {
    Write-Host "  -> Benchmarking interval: ${Interval}ms (duration: ${Duration}s)..." -ForegroundColor Yellow
    $Ndjson = Join-Path $OutDir "bench_${Interval}ms.ndjson"

    $p = Start-Process -FilePath $AgentExe `
        -ArgumentList "--interval-ms", "$Interval", "--duration-s", "$Duration", "--out", $Ndjson, "--self-stats" `
        -PassThru -NoNewWindow
    $p.WaitForExit()

    $pyScript = @"
import json
with open(r'$Ndjson') as f:
    lines = [json.loads(line) for line in f if line.strip()]
cpu_vals = [s.get('self_stats', {}).get('cpu_pct', 0.0) for s in lines]
rss_vals = [s.get('self_stats', {}).get('rss_bytes', 0) / 1024.0 for s in lines]
avg_cpu = sum(cpu_vals) / len(cpu_vals) if cpu_vals else 0.0
max_rss = max(rss_vals) if rss_vals else 0.0
dropped = lines[-1].get('self_stats', {}).get('ring_buffer_dropped_samples', 0) if lines else 0
print(f'$Interval,$Duration,{len(lines)},{dropped},{avg_cpu:.2f},{max_rss:.1f}')
"@

    $res = python -c $pyScript
    $res | Out-File -FilePath $OutCsv -Append -Encoding utf8
    Remove-Item $Ndjson -Force -ErrorAction SilentlyContinue
}

Write-Host "Benchmark complete. Results written to $OutCsv" -ForegroundColor Green
Get-Content $OutCsv

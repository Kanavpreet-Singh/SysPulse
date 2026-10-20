<#
.SYNOPSIS
    SysPulse Automated Fault-Injection & Diagnostic Experiment Runner (Windows PowerShell)
#>
$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = Split-Path -Parent $ScriptDir
$OutDir = Join-Path $RootDir "out"

if (-not (Test-Path $OutDir)) {
    New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
}

$BuildDir = Join-Path $RootDir "build"
$AgentExe = Join-Path $BuildDir "syspulse_agent.exe"
$MemLeakExe = Join-Path $BuildDir "mem_leak.exe"
$CpuSpinExe = Join-Path $BuildDir "cpu_spin.exe"

if (-not (Test-Path $AgentExe)) {
    Write-Error "syspulse_agent.exe not found. Please compile the project first via cmake --build build"
}

Write-Host "======================================================================" -ForegroundColor Cyan
Write-Host " Starting SysPulse End-to-End Experiment (Windows)" -ForegroundColor Cyan
Write-Host "======================================================================" -ForegroundColor Cyan

$SamplesFile = Join-Path $OutDir "experiment_samples.ndjson"
$ReportDir = Join-Path $OutDir "report"

# 1. Start Telemetry Agent
Write-Host "[*] Launching syspulse_agent..." -ForegroundColor Yellow
$AgentProcess = Start-Process -FilePath $AgentExe `
    -ArgumentList "--interval-ms", "200", "--duration-s", "14", "--out", $SamplesFile, "--self-stats", "--filter-name", "mem_leak" `
    -PassThru -NoNewWindow

Start-Sleep -Milliseconds 600

# 2. Start Workloads
Write-Host "[*] Launching fault generators (mem_leak: 8 MB/s, cpu_spin: 80% duty)..." -ForegroundColor Yellow
$MemProcess = Start-Process -FilePath $MemLeakExe -ArgumentList "--rate-mb", "8", "--interval-ms", "150", "--duration-s", "8" -PassThru -NoNewWindow
$CpuProcess = Start-Process -FilePath $CpuSpinExe -ArgumentList "--threads", "2", "--duty", "80", "--duration-s", "6" -PassThru -NoNewWindow

# 3. Wait for processes to exit
Write-Host "[*] Waiting for experiment run to complete..." -ForegroundColor Yellow
$MemProcess.WaitForExit()
$CpuProcess.WaitForExit()
$AgentProcess.WaitForExit()

Write-Host "[*] Telemetry collection complete: $SamplesFile" -ForegroundColor Green

# 4. Run Python Analysis Engine
Write-Host "[*] Executing Python analysis pipeline..." -ForegroundColor Yellow
$env:PYTHONPATH = Join-Path $RootDir "analysis"
python -m syspulse_analysis report $SamplesFile --out $ReportDir

Write-Host "======================================================================" -ForegroundColor Cyan
Write-Host " Experiment Successful. Diagnostic Report: $(Join-Path $ReportDir 'DIAGNOSTIC_REPORT.md')" -ForegroundColor Green
Write-Host "======================================================================" -ForegroundColor Cyan

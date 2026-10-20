"""Diagnostic Report Generator with Matplotlib Charts and Markdown Summaries."""

from pathlib import Path
from typing import List, Dict, Any, Optional
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd


def generate_report(
    df: pd.DataFrame,
    anomalies_df: pd.DataFrame,
    leaks: List[Dict[str, Any]],
    output_dir: Path,
) -> Path:
    """Generate Markdown diagnostic report and matplotlib figures in output_dir."""
    output_dir.mkdir(parents=True, exist_ok=True)
    figures_dir = output_dir / "figures"
    figures_dir.mkdir(parents=True, exist_ok=True)

    # 1. Plot System CPU & Memory Time Series with Anomaly Overlays
    cpu_chart_path = figures_dir / "system_telemetry.png"
    plot_system_telemetry(df, anomalies_df, cpu_chart_path)

    # 2. Plot Memory Leak Regressions
    leaks_chart_path = None
    if leaks:
        leaks_chart_path = figures_dir / "memory_leaks.png"
        plot_memory_leaks(df, leaks, leaks_chart_path)

    # 3. Generate Markdown Report
    report_md_path = output_dir / "DIAGNOSTIC_REPORT.md"
    write_markdown_report(df, anomalies_df, leaks, report_md_path, cpu_chart_path, leaks_chart_path)

    return report_md_path


def plot_system_telemetry(df: pd.DataFrame, anomalies_df: pd.DataFrame, out_path: Path):
    """Render system CPU utilization and per-process load with anomaly scatter markers."""
    plt.figure(figsize=(12, 6))

    # Aggregate system time series
    sys_ts = df.groupby(df.index)["system_cpu_total_pct"].first()
    plt.plot(sys_ts.index, sys_ts.values, label="System CPU %", color="#1f77b4", linewidth=1.8)

    # Anomaly points
    cpu_anoms = anomalies_df[anomalies_df["is_cpu_anomaly"]]
    if not cpu_anoms.empty:
        anom_ts = cpu_anoms.groupby(cpu_anoms.index)["system_cpu_total_pct"].first()
        plt.scatter(
            anom_ts.index,
            anom_ts.values,
            color="#d62728",
            s=40,
            label="Detected CPU Anomaly",
            zorder=5,
        )

    plt.title("SysPulse System Telemetry & Detected Anomalies", fontsize=14, fontweight="bold")
    plt.xlabel("Time (UTC)", fontsize=11)
    plt.ylabel("CPU Utilization (%)", fontsize=11)
    plt.grid(True, linestyle="--", alpha=0.6)
    plt.legend(loc="upper right")
    plt.tight_layout()
    plt.savefig(out_path, dpi=150)
    plt.close()


def plot_memory_leaks(df: pd.DataFrame, leaks: List[Dict[str, Any]], out_path: Path):
    """Render memory progression and OLS linear regression fits for top leaking processes."""
    plt.figure(figsize=(12, 6))

    top_leaks = leaks[:4] # Plot top 4 leaking processes
    colors = ["#d62728", "#ff7f0e", "#2ca02c", "#9467bd"]

    for idx, leak in enumerate(top_leaks):
        pid = leak["pid"]
        proc_data = df[df["pid"] == pid].sort_values("timestamp_ns")
        if proc_data.empty:
            continue

        t_sec = (proc_data["timestamp_ns"].values - proc_data["timestamp_ns"].values[0]) / 1e9
        rss_mb = proc_data["rss_bytes"].values / (1024.0 * 1024.0)

        color = colors[idx % len(colors)]
        label = f"PID {pid} ({leak['name']}) [+{leak['slope_mb_s']} MB/s, R²={leak['r_squared']}]"

        plt.plot(t_sec, rss_mb, "o-", label=label, color=color, markersize=3, alpha=0.7)

        # Plot linear regression trendline
        p = np.poly1d(np.polyfit(t_sec, rss_mb, 1))
        plt.plot(t_sec, p(t_sec), "--", color=color, alpha=0.9, linewidth=1.5)

    plt.title("Memory Leak Diagnostics: RSS Working Set Growth & OLS Trendlines", fontsize=14, fontweight="bold")
    plt.xlabel("Elapsed Time (seconds)", fontsize=11)
    plt.ylabel("Resident Set Size (MB)", fontsize=11)
    plt.grid(True, linestyle="--", alpha=0.6)
    plt.legend(loc="upper left")
    plt.tight_layout()
    plt.savefig(out_path, dpi=150)
    plt.close()


def write_markdown_report(
    df: pd.DataFrame,
    anomalies_df: pd.DataFrame,
    leaks: List[Dict[str, Any]],
    out_path: Path,
    cpu_chart: Path,
    leaks_chart: Optional[Path],
):
    """Write structured diagnostic report in GitHub markdown format."""
    total_samples = len(df.groupby("timestamp_ns"))
    unique_pids = len(df[df["pid"] != 0]["pid"].unique())
    start_time = df.index.min()
    end_time = df.index.max()
    duration_s = (df["timestamp_ns"].max() - df["timestamp_ns"].min()) / 1e9

    cpu_anom_count = int(anomalies_df["is_cpu_anomaly"].sum())
    mem_anom_count = int(anomalies_df["is_mem_anomaly"].sum())

    lines = [
        "# SysPulse Automated Telemetry & Anomaly Diagnostic Report\n",
        "## Executive Summary\n",
        f"- **Collection Duration**: {duration_s:.1f} seconds ({total_samples} discrete collection cycles)",
        f"- **Time Window**: `{start_time}` to `{end_time}`",
        f"- **Total Monitored PIDs**: {unique_pids}",
        f"- **Detected Anomalies**: {cpu_anom_count} CPU spikes, {mem_anom_count} memory rate surges",
        f"- **Flagged Memory Leaks**: {len(leaks)} process(es)\n",
        "## System Telemetry Overview\n",
        f"![System Telemetry]({cpu_chart.name})\n",
        "## Memory Leak Diagnostics\n",
    ]

    if leaks:
        lines.append("| PID | Process Name | Leak Slope (MB/s) | $R^2$ Score | Total Growth | Confidence |")
        lines.append("|---|---|---|---|---|---|")
        for lk in leaks:
            lines.append(
                f"| `{lk['pid']}` | `{lk['name']}` | **+{lk['slope_mb_s']} MB/s** | `{lk['r_squared']}` | "
                f"+{lk['total_growth_mb']} MB | `{int(lk['confidence'] * 100)}%` |"
            )
        lines.append("\n")
        if leaks_chart:
            lines.append(f"![Memory Leaks]({leaks_chart.name})\n")
    else:
        lines.append("> [!NOTE]\n> No sustained linear memory growth patterns detected across monitored processes.\n")

    lines.extend([
        "## Root Cause & Action Items\n",
        "1. **Memory Leaks**: Inspect highlighted processes using GDB/Valgrind (Linux) or WinDbg `!heap` (Windows).",
        "2. **CPU Spikes**: Cross-reference timestamps with thread dump captures or ETW trace events.",
    ])

    with open(out_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))

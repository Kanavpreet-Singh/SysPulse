"""Robust Anomaly Detection for CPU Spikes and Memory Surges."""

from typing import Dict, Any
import numpy as np
import pandas as pd


def robust_zscore(series: pd.Series) -> pd.Series:
    """Calculate modified Z-score using Median and Median Absolute Deviation (MAD)."""
    median = series.median()
    diff = (series - median).abs()
    mad = diff.median()

    if mad == 0 or np.isnan(mad):
        # Fallback to mean absolute deviation or direct deviation if data is flat
        mean_dev = diff.mean()
        if mean_dev == 0 or np.isnan(mean_dev):
            return pd.Series(0.0, index=series.index)
        return (0.6745 * (series - median)) / mean_dev

    return (0.6745 * (series - median)) / mad


def detect_anomalies(
    df: pd.DataFrame,
    cpu_z_thresh: float = 3.5,
    cpu_residual_thresh: float = 25.0,
    rss_z_thresh: float = 3.5,
) -> pd.DataFrame:
    """Detect anomalies in system and per-process telemetry using robust z-scores and EWMA residuals."""
    res_df = df.copy()

    # Calculate system CPU anomaly score
    if "system_cpu_total_pct" in res_df.columns:
        sys_cpu = res_df.groupby("timestamp_ns")["system_cpu_total_pct"].first()
        sys_z = robust_zscore(sys_cpu)
        res_df["system_cpu_zscore"] = res_df["timestamp_ns"].map(sys_z).fillna(0.0)
    else:
        res_df["system_cpu_zscore"] = 0.0

    # Per-process robust z-score calculation
    res_df["proc_cpu_zscore"] = res_df.groupby("pid")["cpu_pct"].transform(robust_zscore).fillna(0.0)

    # RSS rate of change z-score
    if "rss_rate_mb_s" in res_df.columns:
        res_df["rss_rate_zscore"] = (
            res_df.groupby("pid")["rss_rate_mb_s"]
            .transform(lambda s: robust_zscore(s.fillna(0.0)))
            .fillna(0.0)
        )
    else:
        res_df["rss_rate_zscore"] = 0.0

    # Composite boolean anomaly flags
    is_cpu_spike = (
        (res_df["proc_cpu_zscore"] >= cpu_z_thresh) |
        (res_df.get("cpu_ewma_residual", 0.0) >= cpu_residual_thresh)
    )

    is_mem_surge = res_df["rss_rate_zscore"] >= rss_z_thresh

    res_df["is_cpu_anomaly"] = is_cpu_spike
    res_df["is_mem_anomaly"] = is_mem_surge
    res_df["is_anomaly"] = is_cpu_spike | is_mem_surge

    return res_df


def summarize_anomalies(df_anomalies: pd.DataFrame) -> Dict[str, Any]:
    """Aggregate detected anomalies into a diagnostic summary."""
    anom_rows = df_anomalies[df_anomalies["is_anomaly"]]

    summary = {
        "total_anomalies": int(len(anom_rows)),
        "cpu_anomalies": int(df_anomalies["is_cpu_anomaly"].sum()),
        "mem_anomalies": int(df_anomalies["is_mem_anomaly"].sum()),
        "affected_pids": [int(p) for p in anom_rows["pid"].unique() if p != 0],
        "top_offenders": [],
    }

    if not anom_rows.empty:
        top_procs = (
            anom_rows[anom_rows["pid"] != 0]
            .groupby(["pid", "name"])
            .size()
            .reset_index(name="anomaly_count")
            .sort_values(by="anomaly_count", ascending=False)
            .head(5)
        )
        summary["top_offenders"] = top_procs.to_dict(orient="records")

    return summary

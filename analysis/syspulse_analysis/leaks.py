"""Memory Leak Detection via Linear Regression (NumPy OLS)."""

from typing import Dict, List, Any
import numpy as np
import pandas as pd


def detect_memory_leaks(
    df: pd.DataFrame,
    min_slope_mb_s: float = 0.5,
    min_r2: float = 0.85,
    min_samples: int = 8,
    warmup_fraction: float = 0.15,
) -> List[Dict[str, Any]]:
    """Detect sustained memory leaks per process using Ordinary Least Squares linear regression.
    
    Parameters:
      min_slope_mb_s: Minimum growth rate in MB/s to classify as leak.
      min_r2: Minimum coefficient of determination (goodness of fit).
      min_samples: Minimum data points required after warmup.
      warmup_fraction: Initial fraction of samples to discard (initialization warmup).
    """
    leaks = []

    for pid, group in df.groupby("pid"):
        if pid == 0:
            continue  # Skip system aggregate

        proc_name = group["name"].iloc[0] if not group["name"].empty else "unknown"
        sorted_grp = group.sort_values(by="timestamp_ns")

        # Discard warmup samples
        n_total = len(sorted_grp)
        if n_total < min_samples + 2:
            continue

        n_warmup = int(n_total * warmup_fraction)
        active_data = sorted_grp.iloc[n_warmup:].copy()

        if len(active_data) < min_samples:
            continue

        # Convert timestamps to relative seconds from post-warmup start
        t_sec = (active_data["timestamp_ns"].values - active_data["timestamp_ns"].values[0]) / 1e9
        rss_mb = active_data["rss_bytes"].values / (1024.0 * 1024.0)

        # Degenerate case: all memory values identical
        if np.all(rss_mb == rss_mb[0]):
            continue

        # Fit 1st order polynomial using NumPy (y = slope * t + intercept)
        slope, intercept = np.polyfit(t_sec, rss_mb, deg=1)

        # Compute R^2 (coefficient of determination)
        predicted = slope * t_sec + intercept
        ss_res = np.sum((rss_mb - predicted) ** 2)
        ss_tot = np.sum((rss_mb - np.mean(rss_mb)) ** 2)

        if ss_tot == 0:
            r2 = 0.0
        else:
            r2 = max(0.0, 1.0 - (ss_res / ss_tot))

        # Check monotonicity: fraction of step deltas that are >= 0
        deltas = np.diff(rss_mb)
        positive_steps = np.sum(deltas >= -0.01) # Allow minor noise
        monotonicity = positive_steps / len(deltas) if len(deltas) > 0 else 0.0

        total_growth_mb = rss_mb[-1] - rss_mb[0]

        is_leak = (slope >= min_slope_mb_s) and (r2 >= min_r2) and (monotonicity >= 0.70)

        if is_leak:
            confidence = min(1.0, r2 * monotonicity)
            leaks.append({
                "pid": int(pid),
                "name": proc_name,
                "slope_mb_s": float(round(slope, 3)),
                "r_squared": float(round(r2, 4)),
                "monotonicity": float(round(monotonicity, 3)),
                "total_growth_mb": float(round(total_growth_mb, 2)),
                "start_rss_mb": float(round(rss_mb[0], 2)),
                "final_rss_mb": float(round(rss_mb[-1], 2)),
                "sample_count": int(len(active_data)),
                "duration_s": float(round(t_sec[-1], 2)),
                "confidence": float(round(confidence, 3)),
            })

    # Sort flagged leaks descending by leak slope
    leaks.sort(key=lambda x: x["slope_mb_s"], reverse=True)
    return leaks

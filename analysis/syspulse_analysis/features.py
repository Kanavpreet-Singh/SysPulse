"""Feature Engineering for System and Process Telemetry."""

import numpy as np
import pandas as pd


def compute_features(df: pd.DataFrame, window_size: int = 5, ewma_span: int = 5) -> pd.DataFrame:
    """Compute rolling statistics, rate of change, and EWMA baselines per process."""
    df_feat = df.copy()

    # Per-process grouped feature engineering
    grouped = df_feat.groupby("pid", group_keys=False)

    # 1. Delta time in seconds between successive samples of the same PID
    dt_sec = grouped["timestamp_ns"].diff() / 1e9
    dt_sec = dt_sec.replace(0, np.nan)

    # 2. Rate of change (derivative)
    df_feat["rss_rate_mb_s"] = (grouped["rss_bytes"].diff() / (1024 * 1024)) / dt_sec
    df_feat["cpu_delta"] = grouped["cpu_pct"].diff()

    # 3. Rolling Statistics
    df_feat["cpu_rolling_mean"] = grouped["cpu_pct"].transform(
        lambda s: s.rolling(window=window_size, min_periods=1).mean()
    )
    df_feat["cpu_rolling_std"] = grouped["cpu_pct"].transform(
        lambda s: s.rolling(window=window_size, min_periods=1).std().fillna(0.0)
    )
    df_feat["rss_rolling_mean_mb"] = grouped["rss_bytes"].transform(
        lambda s: (s / (1024 * 1024)).rolling(window=window_size, min_periods=1).mean()
    )

    # 4. Exponentially Weighted Moving Average (EWMA)
    df_feat["cpu_ewma"] = grouped["cpu_pct"].transform(
        lambda s: s.ewm(span=ewma_span, adjust=False).mean()
    )
    df_feat["rss_ewma_mb"] = grouped["rss_bytes"].transform(
        lambda s: (s / (1024 * 1024)).ewm(span=ewma_span, adjust=False).mean()
    )

    # 5. EWMA Residuals (signal - baseline)
    df_feat["cpu_ewma_residual"] = df_feat["cpu_pct"] - df_feat["cpu_ewma"]
    df_feat["rss_ewma_residual_mb"] = (df_feat["rss_bytes"] / (1024 * 1024)) - df_feat["rss_ewma_mb"]

    return df_feat

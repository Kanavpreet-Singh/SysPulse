"""Unit tests for SysPulse Python Analysis Pipeline."""

import json
import numpy as np
import pandas as pd
import pytest

from syspulse_analysis.loader import load_telemetry
from syspulse_analysis.features import compute_features
from syspulse_analysis.anomaly import detect_anomalies, robust_zscore
from syspulse_analysis.leaks import detect_memory_leaks


def create_synthetic_telemetry(n_samples: int = 40, leak_rate_mb_s: float = 0.0, cpu_spike_idx: int = -1):
    """Generate synthetic telemetry DataFrame for controlled verification."""
    start_ns = 1_700_000_000_000_000_000
    interval_ns = 500_000_000 # 500ms
    timestamps = [start_ns + i * interval_ns for i in range(n_samples)]

    rows = []
    base_rss = 50 * 1024 * 1024 # 50 MB

    for i, ts in enumerate(timestamps):
        t_sec = i * 0.5
        # Steady process CPU (10% with small noise)
        cpu = 10.0 + np.random.normal(0, 0.5)
        if i == cpu_spike_idx:
            cpu = 95.0 # Injected anomaly spike

        # Memory footprint
        rss = int(base_rss + (leak_rate_mb_s * 1024 * 1024 * t_sec))

        rows.append({
            "timestamp_ns": ts,
            "hostname": "test-box",
            "system_cpu_total_pct": cpu * 0.8,
            "system_mem_total_bytes": 16 * 1024 * 1024 * 1024,
            "system_mem_available_bytes": 12 * 1024 * 1024 * 1024,
            "system_swap_used_bytes": 0,
            "context_switches": 1000 + i * 50,
            "page_faults": 500 + i * 10,
            "pid": 4200,
            "ppid": 1,
            "name": "synthetic_daemon",
            "cpu_pct": cpu,
            "rss_bytes": rss,
            "virtual_bytes": rss * 2,
            "thread_count": 4,
            "handle_or_fd_count": 16,
            "io_read_bytes": 1024 * i,
            "io_write_bytes": 2048 * i,
        })

    df = pd.DataFrame(rows)
    df["datetime"] = pd.to_datetime(df["timestamp_ns"], unit="ns", utc=True)
    df.set_index("datetime", drop=True, inplace=True)
    return df


def test_loader_valid_ndjson(tmp_path):
    log_file = tmp_path / "samples.ndjson"
    sample = {
        "timestamp_ns": 1700000000000000000,
        "hostname": "test-node",
        "system": {
            "cpu_total_pct": 25.5,
            "mem_total_bytes": 16000000000,
            "mem_available_bytes": 10000000000,
            "swap_used_bytes": 0,
            "context_switches": 5000,
            "page_faults": 1000,
        },
        "processes": [
            {
                "pid": 101,
                "ppid": 1,
                "name": "app",
                "cpu_pct": 12.0,
                "rss_bytes": 52428800,
                "virtual_bytes": 104857600,
                "thread_count": 2,
                "handle_or_fd_count": 10,
            }
        ],
    }
    log_file.write_text(json.dumps(sample) + "\n", encoding="utf-8")

    df = load_telemetry(log_file)
    assert not df.empty
    assert len(df) == 1
    assert df.iloc[0]["pid"] == 101
    assert df.iloc[0]["name"] == "app"
    assert df.index.name == "datetime"


def test_loader_missing_columns(tmp_path):
    bad_csv = tmp_path / "bad.csv"
    bad_csv.write_text("timestamp_ns,foo,bar\n123,1,2\n", encoding="utf-8")
    with pytest.raises(ValueError, match="Missing required columns"):
        load_telemetry(bad_csv)


def test_features_computation():
    df = create_synthetic_telemetry(n_samples=20)
    df_feat = compute_features(df, window_size=3, ewma_span=3)

    assert "rss_rate_mb_s" in df_feat.columns
    assert "cpu_ewma" in df_feat.columns
    assert "cpu_rolling_mean" in df_feat.columns
    assert "cpu_ewma_residual" in df_feat.columns
    assert not df_feat["cpu_rolling_mean"].isna().all()


def test_anomaly_detection_with_injected_spike():
    # Inject spike at index 25
    df = create_synthetic_telemetry(n_samples=40, cpu_spike_idx=25)
    df_feat = compute_features(df)
    anomalies = detect_anomalies(df_feat, cpu_z_thresh=3.5)

    assert anomalies.iloc[25]["is_cpu_anomaly"]
    assert anomalies.iloc[25]["is_anomaly"]
    assert anomalies.iloc[25]["proc_cpu_zscore"] > 3.5

    # Check normal timestamps are not flagged
    assert not anomalies.iloc[5]["is_cpu_anomaly"]
    assert not anomalies.iloc[10]["is_cpu_anomaly"]


def test_anomaly_detection_clean_data_no_false_positives():
    # Clean stationary distribution
    df = create_synthetic_telemetry(n_samples=30, cpu_spike_idx=-1)
    df_feat = compute_features(df)
    anomalies = detect_anomalies(df_feat, cpu_z_thresh=4.0)

    # Clean data should produce 0 false positives at threshold 4.0
    assert anomalies["is_cpu_anomaly"].sum() == 0


def test_memory_leak_detection_injected_leak():
    # Injected leak of 8 MB/s
    df = create_synthetic_telemetry(n_samples=40, leak_rate_mb_s=8.0)
    leaks = detect_memory_leaks(df, min_slope_mb_s=5.0, min_r2=0.90)

    assert len(leaks) == 1
    leak = leaks[0]
    assert leak["pid"] == 4200
    assert leak["name"] == "synthetic_daemon"
    assert pytest.approx(leak["slope_mb_s"], rel=0.15) == 8.0
    assert leak["r_squared"] > 0.95
    assert leak["confidence"] > 0.80


def test_memory_leak_detection_clean_no_leak():
    # Constant memory (0 MB/s leak)
    df = create_synthetic_telemetry(n_samples=40, leak_rate_mb_s=0.0)
    leaks = detect_memory_leaks(df, min_slope_mb_s=1.0, min_r2=0.85)

    assert len(leaks) == 0

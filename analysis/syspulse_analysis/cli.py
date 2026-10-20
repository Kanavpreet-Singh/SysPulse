"""Command Line Interface for syspulse_analysis."""

import argparse
import sys
from pathlib import Path

from .loader import load_telemetry
from .features import compute_features
from .anomaly import detect_anomalies, summarize_anomalies
from .leaks import detect_memory_leaks
from .report import generate_report


def main():
    parser = argparse.ArgumentParser(
        description="SysPulse Telemetry Diagnostic & Anomaly Detection Pipeline"
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    # Subcommand: report
    report_parser = subparsers.add_parser("report", help="Generate full diagnostic report from telemetry")
    report_parser.add_argument("input_file", type=Path, help="Path to input NDJSON or CSV file")
    report_parser.add_argument("--out", type=Path, default=Path("report"), help="Output directory for report and figures")
    report_parser.add_argument("--cpu-thresh", type=float, default=3.5, help="Robust z-score threshold for CPU spikes")
    report_parser.add_argument("--leak-slope", type=float, default=0.5, help="Min memory leak rate in MB/s")
    report_parser.add_argument("--leak-r2", type=float, default=0.85, help="Min R^2 score for memory leak fit")

    args = parser.parse_args()

    if args.command == "report":
        print(f"[SysPulse Analysis] Loading telemetry from: {args.input_file}")
        try:
            df = load_telemetry(args.input_file)
        except Exception as e:
            print(f"Error loading telemetry: {e}", file=sys.stderr)
            sys.exit(1)

        print(f"[SysPulse Analysis] Loaded {len(df)} records. Engineering features...")
        df_feat = compute_features(df)

        print("[SysPulse Analysis] Running robust anomaly detection...")
        anomalies_df = detect_anomalies(df_feat, cpu_z_thresh=args.cpu_thresh)
        anom_summary = summarize_anomalies(anomalies_df)
        print(f"  -> Detected {anom_summary['total_anomalies']} anomalies ({anom_summary['cpu_anomalies']} CPU spikes)")

        print("[SysPulse Analysis] Running linear regression memory leak detection...")
        leaks = detect_memory_leaks(df_feat, min_slope_mb_s=args.leak_slope, min_r2=args.leak_r2)
        print(f"  -> Flagged {len(leaks)} leaking processes")

        for lk in leaks:
            print(f"     PID {lk['pid']} ({lk['name']}): +{lk['slope_mb_s']} MB/s (R^2={lk['r_squared']}, confidence={lk['confidence']})")

        print(f"[SysPulse Analysis] Generating diagnostic report into: {args.out}")
        report_path = generate_report(df, anomalies_df, leaks, args.out)
        print(f"[SysPulse Analysis] Diagnostic report complete: {report_path}")


if __name__ == "__main__":
    main()

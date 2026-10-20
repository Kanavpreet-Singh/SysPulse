"""Telemetry Loader for SysPulse NDJSON and CSV logs."""

import json
from pathlib import Path
from typing import Union
import pandas as pd


REQUIRED_COLUMNS = {
    "timestamp_ns",
    "system_cpu_total_pct",
    "system_mem_total_bytes",
    "system_mem_available_bytes",
    "pid",
    "name",
    "cpu_pct",
    "rss_bytes",
}


def load_telemetry(file_path: Union[str, Path]) -> pd.DataFrame:
    """Load and validate telemetry samples from NDJSON or CSV into a time-indexed DataFrame."""
    path = Path(file_path)
    if not path.exists():
        raise FileNotFoundError(f"Telemetry log file not found: {path}")

    # Determine format by suffix or content
    is_ndjson = path.suffix.lower() in [".ndjson", ".json", ".jsonl"] or "ndjson" in path.name

    if is_ndjson:
        rows = []
        with open(path, "r", encoding="utf-8") as f:
            for line_num, line in enumerate(f, 1):
                line = line.strip()
                if not line:
                    continue
                try:
                    data = json.loads(line)
                except json.JSONDecodeError as err:
                    raise ValueError(f"Malformed JSON on line {line_num} in {path}: {err}")

                ts_ns = data.get("timestamp_ns", 0)
                hostname = data.get("hostname", "localhost")
                sys_m = data.get("system", {})
                self_s = data.get("self_stats", {})

                sys_cpu = sys_m.get("cpu_total_pct", 0.0)
                sys_mem_tot = sys_m.get("mem_total_bytes", 0)
                sys_mem_avail = sys_m.get("mem_available_bytes", 0)
                sys_swap = sys_m.get("swap_used_bytes", 0)
                sys_ctxt = sys_m.get("context_switches", 0)
                sys_pf = sys_m.get("page_faults", 0)

                procs = data.get("processes", [])
                if not procs:
                    rows.append({
                        "timestamp_ns": ts_ns,
                        "hostname": hostname,
                        "system_cpu_total_pct": sys_cpu,
                        "system_mem_total_bytes": sys_mem_tot,
                        "system_mem_available_bytes": sys_mem_avail,
                        "system_swap_used_bytes": sys_swap,
                        "context_switches": sys_ctxt,
                        "page_faults": sys_pf,
                        "pid": 0,
                        "ppid": 0,
                        "name": "system",
                        "cpu_pct": 0.0,
                        "rss_bytes": 0,
                        "virtual_bytes": 0,
                        "thread_count": 0,
                        "handle_or_fd_count": 0,
                        "io_read_bytes": 0,
                        "io_write_bytes": 0,
                    })
                else:
                    for p in procs:
                        rows.append({
                            "timestamp_ns": ts_ns,
                            "hostname": hostname,
                            "system_cpu_total_pct": sys_cpu,
                            "system_mem_total_bytes": sys_mem_tot,
                            "system_mem_available_bytes": sys_mem_avail,
                            "system_swap_used_bytes": sys_swap,
                            "context_switches": sys_ctxt,
                            "page_faults": sys_pf,
                            "pid": p.get("pid", 0),
                            "ppid": p.get("ppid", 0),
                            "name": p.get("name", ""),
                            "cpu_pct": p.get("cpu_pct", 0.0),
                            "rss_bytes": p.get("rss_bytes", 0),
                            "virtual_bytes": p.get("virtual_bytes", 0),
                            "thread_count": p.get("thread_count", 0),
                            "handle_or_fd_count": p.get("handle_or_fd_count", 0),
                            "io_read_bytes": p.get("io_read_bytes", 0),
                            "io_write_bytes": p.get("io_write_bytes", 0),
                        })
        df = pd.DataFrame(rows)
    else:
        df = pd.read_csv(path)

    if df.empty:
        raise ValueError(f"Telemetry log is empty: {path}")

    # Schema validation
    missing = REQUIRED_COLUMNS - set(df.columns)
    if missing:
        raise ValueError(f"Telemetry schema invalid. Missing required columns: {missing}")

    # Convert timestamp_ns to datetime and create time index
    df["datetime"] = pd.to_datetime(df["timestamp_ns"], unit="ns", utc=True)
    df.sort_values(by=["datetime", "pid"], inplace=True)
    df.set_index("datetime", drop=True, inplace=True)

    return df

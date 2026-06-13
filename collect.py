#!/usr/bin/env python3
"""
collect.py — merge all parasitic current log files into one CSV.

Scans one or more directories recursively for *.csv files produced by
the ESP32 logger and/or the Pi Zero logger and writes a single output
CSV sorted by timestamp.

Synced rows (time_ok=1 or Pi Zero files) are sorted chronologically
and written first.  Unsynced rows (NOSYNC files, time_ok=0) are
appended at the end; their 'timestamp' column contains the original
boot-relative value (e.g. "boot+12345ms") so they remain identifiable.

Usage:
    python3 collect.py <dir> [<dir> ...] [-o output.csv]

Examples:
    python3 collect.py /media/user/SDCARD
    python3 collect.py ./esp32_logs ./pi_logs -o run1.csv
"""

import csv
import argparse
from datetime import datetime
from pathlib import Path


TIMESTAMP_FMT = "%Y-%m-%d %H:%M:%S"


def read_csv(path):
    rows = []
    with open(path, newline="", encoding="utf-8-sig") as f:
        reader = csv.DictReader(f)
        if not reader.fieldnames:
            return [], []
        fields = list(reader.fieldnames)
        for row in reader:
            rows.append(dict(row))
    return rows, fields


def parse_dt(ts):
    try:
        return datetime.strptime(ts, TIMESTAMP_FMT)
    except (ValueError, TypeError):
        return None


def collect(input_dirs, output_path):
    synced_rows   = []
    unsynced_rows = []
    data_cols     = []   # ordered, deduplicated list of non-header columns

    def add_data_cols(fields):
        skip = {"source", "time_ok", "timestamp"}
        for f in fields:
            if f not in skip and f not in data_cols:
                data_cols.append(f)

    for d in input_dirs:
        paths = sorted(Path(d).rglob("*.csv"))
        if not paths:
            print(f"[warn] no CSV files found in {d}")
            continue

        for path in paths:
            rows, fields = read_csv(path)
            if not rows:
                print(f"  skip (empty): {path.name}")
                continue

            add_data_cols(fields)

            # Pi Zero files have no time_ok column — treat as always synced
            has_time_ok = "time_ok" in fields

            n_synced = n_unsynced = 0
            for row in rows:
                row["source"] = path.name
                if not has_time_ok:
                    row["time_ok"] = "1"

                dt = parse_dt(row.get("timestamp", ""))
                trusted = row.get("time_ok", "1") == "1" and dt is not None

                if trusted:
                    row["_dt"] = dt
                    synced_rows.append(row)
                    n_synced += 1
                else:
                    row["_dt"] = None
                    unsynced_rows.append(row)
                    n_unsynced += 1

            print(f"  {path.name}: {n_synced} synced, {n_unsynced} unsynced")

    synced_rows.sort(key=lambda r: r["_dt"])

    # Sort unsynced rows by source file then by boot+ms value
    def unsynced_key(r):
        ms_str = r.get("timestamp", "")
        ms = int(ms_str.removeprefix("boot+").removesuffix("ms")) if "boot+" in ms_str else 0
        return (r["source"], ms)

    unsynced_rows.sort(key=unsynced_key)

    all_rows = synced_rows + unsynced_rows
    out_fields = ["source", "time_ok", "timestamp"] + data_cols

    with open(output_path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=out_fields, extrasaction="ignore")
        writer.writeheader()
        for row in all_rows:
            writer.writerow({col: row.get(col, "") for col in out_fields})

    print(f"\n{len(synced_rows)} synced + {len(unsynced_rows)} unsynced "
          f"= {len(all_rows)} total rows → {output_path}")


if __name__ == "__main__":
    ap = argparse.ArgumentParser(
        description="Merge ESP32 and Pi Zero parasitic current logs into one CSV."
    )
    ap.add_argument("dirs", nargs="+", metavar="DIR",
                    help="Directories to search recursively for *.csv log files")
    ap.add_argument("-o", "--output", default="collected.csv",
                    help="Output file path (default: collected.csv)")
    args = ap.parse_args()

    print(f"Scanning: {args.dirs}")
    collect(args.dirs, args.output)

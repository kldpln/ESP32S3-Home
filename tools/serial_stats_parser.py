#!/usr/bin/env python3
"""Parse ESP32 serial JSON stats and generate thesis-friendly summaries.

Usage examples:
  1) Parse an existing monitor log:
     python tools/serial_stats_parser.py --input stats.log --csv samples.csv --summary summary.json --markdown summary.md

  2) Capture live serial output (requires pyserial):
     python tools/serial_stats_parser.py --serial COM3 --baud 115200 --duration 300 --raw raw.log --csv samples.csv --summary summary.json

The ESP32 side prints one JSON object per line. This script extracts the JSON,
normalizes task CPU percentages for dual-core systems, and computes concise
metrics suitable for experimental reporting.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import statistics
import sys
import time
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable


JSON_LINE_RE = re.compile(r"\{.*\}")


@dataclass
class TaskAggregate:
    samples: int = 0
    run_pct_values: list[float] = field(default_factory=list)
    norm_pct_values: list[float] = field(default_factory=list)
    stack_high_water_values: list[int] = field(default_factory=list)
    run_time_values: list[int] = field(default_factory=list)

    def add(self, run_pct: float, norm_pct: float, stack_high_water: int, run_time: int) -> None:
        self.samples += 1
        self.run_pct_values.append(run_pct)
        self.norm_pct_values.append(norm_pct)
        self.stack_high_water_values.append(stack_high_water)
        self.run_time_values.append(run_time)


def load_pyserial():
    try:
        import serial  # type: ignore
        return serial
    except Exception as exc:  # pragma: no cover - optional dependency
        raise SystemExit(
            "pyserial is required for live capture. Install it with: pip install pyserial"
        ) from exc


def extract_json_objects(lines: Iterable[str]) -> list[dict[str, Any]]:
    samples: list[dict[str, Any]] = []
    for raw_line in lines:
        line = raw_line.strip()
        if not line:
            continue
        match = JSON_LINE_RE.search(line)
        if not match:
            continue
        candidate = match.group(0)
        try:
            obj = json.loads(candidate)
        except json.JSONDecodeError:
            continue
        if isinstance(obj, dict) and "uptime_ms" in obj and "free_heap" in obj:
            samples.append(obj)
    return samples


def percentile(values: list[float], p: float) -> float:
    if not values:
        return 0.0
    values = sorted(values)
    if len(values) == 1:
        return values[0]
    k = (len(values) - 1) * (p / 100.0)
    f = int(k)
    c = min(f + 1, len(values) - 1)
    if f == c:
        return values[f]
    d0 = values[f] * (c - k)
    d1 = values[c] * (k - f)
    return d0 + d1


def core_count_from_sample(sample: dict[str, Any]) -> int:
    tasks = sample.get("tasks") or []
    idle_tasks = [task for task in tasks if isinstance(task, dict) and str(task.get("name", "")).startswith("IDLE")]
    return max(1, len(idle_tasks))


def sample_summary_row(sample: dict[str, Any], cores: int) -> dict[str, Any]:
    tasks = sample.get("tasks") or []
    task_dicts = [task for task in tasks if isinstance(task, dict)]
    idle_pcts = [float(task.get("run_pct", 0.0)) for task in task_dicts if str(task.get("name", "")).startswith("IDLE")]
    non_idle = [task for task in task_dicts if not str(task.get("name", "")).startswith("IDLE")]

    total_idle_pct = sum(idle_pcts)
    estimated_util_pct = max(0.0, 100.0 - (total_idle_pct / cores)) if cores else 0.0
    total_raw_task_pct = sum(float(task.get("run_pct", 0.0)) for task in task_dicts)

    top_task = None
    if non_idle:
        top_task = max(non_idle, key=lambda task: float(task.get("run_pct", 0.0)))

    min_stack_task = None
    if task_dicts:
        min_stack_task = min(task_dicts, key=lambda task: int(task.get("stack_high_water", 10**9)))

    return {
        "uptime_ms": int(sample.get("uptime_ms", 0)),
        "free_heap": int(sample.get("free_heap", 0)),
        "min_free_heap": int(sample.get("min_free_heap", 0)),
        "free_8bit": int(sample.get("free_8bit", 0)),
        "task_count": len(task_dicts),
        "core_count": cores,
        "raw_task_pct_sum": round(total_raw_task_pct, 2),
        "idle_pct_sum": round(total_idle_pct, 2),
        "system_util_pct_est": round(estimated_util_pct, 2),
        "top_task_name": top_task.get("name") if top_task else "",
        "top_task_run_pct_raw": round(float(top_task.get("run_pct", 0.0)), 2) if top_task else 0.0,
        "top_task_run_pct_norm": round(float(top_task.get("run_pct", 0.0)) / cores, 2) if top_task else 0.0,
        "min_stack_task_name": min_stack_task.get("name") if min_stack_task else "",
        "min_stack_high_water": int(min_stack_task.get("stack_high_water", 0)) if min_stack_task else 0,
    }


def summarize_samples(samples: list[dict[str, Any]]) -> dict[str, Any]:
    if not samples:
        return {"sample_count": 0}

    cores = core_count_from_sample(samples[0])
    rows = [sample_summary_row(sample, cores) for sample in samples]

    task_aggs: dict[str, TaskAggregate] = defaultdict(TaskAggregate)
    for sample in samples:
        task_list = [task for task in (sample.get("tasks") or []) if isinstance(task, dict)]
        for task in task_list:
            name = str(task.get("name", ""))
            run_pct_raw = float(task.get("run_pct", 0.0))
            run_pct_norm = run_pct_raw / cores
            stack_high_water = int(task.get("stack_high_water", 0))
            run_time = int(task.get("run_time", 0))
            task_aggs[name].add(run_pct_raw, run_pct_norm, stack_high_water, run_time)

    free_heap_values = [row["free_heap"] for row in rows]
    min_free_heap_values = [row["min_free_heap"] for row in rows]
    free_8bit_values = [row["free_8bit"] for row in rows]
    total_heap_values = [int(sample.get("total_heap_8bit", 0)) for sample in samples if int(sample.get("total_heap_8bit", 0)) > 0]
    used_heap_values = [int(sample.get("used_heap_8bit", 0)) for sample in samples if int(sample.get("used_heap_8bit", 0)) > 0]
    mem_usage_values = [float(sample.get("mem_usage_pct", 0.0)) for sample in samples if float(sample.get("mem_usage_pct", 0.0)) >= 0.0]
    util_values = [row["system_util_pct_est"] for row in rows]
    uptime_values = [row["uptime_ms"] for row in rows]

    duration_ms = max(0, uptime_values[-1] - uptime_values[0]) if len(uptime_values) > 1 else 0

    task_summary = {}
    for name, agg in task_aggs.items():
        task_summary[name] = {
            "samples": agg.samples,
            "run_pct_avg_raw": round(statistics.fmean(agg.run_pct_values), 2),
            "run_pct_max_raw": round(max(agg.run_pct_values), 2),
            "run_pct_avg_norm": round(statistics.fmean(agg.norm_pct_values), 2),
            "run_pct_max_norm": round(max(agg.norm_pct_values), 2),
            "stack_high_water_min": min(agg.stack_high_water_values),
            "stack_high_water_avg": round(statistics.fmean(agg.stack_high_water_values), 1),
            "run_time_last": agg.run_time_values[-1],
        }

    top_tasks_by_cpu = sorted(
        (
            {
                "name": name,
                "avg_run_pct_raw": data["run_pct_avg_raw"],
                "max_run_pct_raw": data["run_pct_max_raw"],
                "avg_run_pct_norm": data["run_pct_avg_norm"],
                "max_run_pct_norm": data["run_pct_max_norm"],
                "min_stack_high_water": data["stack_high_water_min"],
            }
            for name, data in task_summary.items()
            if not name.startswith("IDLE") and name != "system_monitor"
        ),
        key=lambda item: item["avg_run_pct_norm"],
        reverse=True,
    )

    return {
        "sample_count": len(samples),
        "core_count": cores,
        "duration_ms": duration_ms,
        "duration_s": round(duration_ms / 1000.0, 2),
        "free_heap": {
            "min": min(free_heap_values),
            "max": max(free_heap_values),
            "avg": round(statistics.fmean(free_heap_values), 1),
        },
        "min_free_heap": {
            "min": min(min_free_heap_values),
            "max": max(min_free_heap_values),
            "avg": round(statistics.fmean(min_free_heap_values), 1),
        },
        "free_8bit": {
            "min": min(free_8bit_values),
            "max": max(free_8bit_values),
            "avg": round(statistics.fmean(free_8bit_values), 1),
        },
        "total_heap_8bit": {
            "avg": round(statistics.fmean(total_heap_values), 1) if total_heap_values else 0.0,
        },
        "used_heap_8bit": {
            "avg": round(statistics.fmean(used_heap_values), 1) if used_heap_values else 0.0,
        },
        "mem_usage_pct": {
            "min": round(min(mem_usage_values), 2) if mem_usage_values else 0.0,
            "max": round(max(mem_usage_values), 2) if mem_usage_values else 0.0,
            "avg": round(statistics.fmean(mem_usage_values), 2) if mem_usage_values else 0.0,
        },
        "system_util_pct_est": {
            "min": round(min(util_values), 2),
            "max": round(max(util_values), 2),
            "avg": round(statistics.fmean(util_values), 2),
        },
        "p95_free_heap": round(percentile([float(v) for v in free_heap_values], 95), 1),
        "task_summary": task_summary,
        "top_tasks_by_cpu_norm": top_tasks_by_cpu[:10],
        "notes": [
            "raw run_pct is the value printed by ESP32; on dual-core systems it is not directly suitable for thesis presentation",
            "system_util_pct_est normalizes idle task percentage by core count and is better for paper tables/plots",
            "task names may be truncated by FreeRTOS configMAX_TASK_NAME_LEN",
        ],
    }


def write_csv(rows: list[dict[str, Any]], path: Path) -> None:
    fieldnames = [
        "uptime_ms",
        "free_heap",
        "min_free_heap",
        "free_8bit",
        "task_count",
        "core_count",
        "raw_task_pct_sum",
        "idle_pct_sum",
        "system_util_pct_est",
        "top_task_name",
        "top_task_run_pct_raw",
        "top_task_run_pct_norm",
        "min_stack_task_name",
        "min_stack_high_water",
    ]
    with path.open("w", newline="", encoding="utf-8") as fp:
        writer = csv.DictWriter(fp, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def write_markdown(summary: dict[str, Any], path: Path) -> None:
    lines = []
    lines.append("# System Monitor Summary")
    lines.append("")
    lines.append(f"- Sample count: {summary.get('sample_count', 0)}")
    lines.append(f"- Core count: {summary.get('core_count', 1)}")
    lines.append(f"- Duration: {summary.get('duration_s', 0)} s")
    lines.append(f"- Free heap avg: {summary['free_heap']['avg']} bytes")
    lines.append(f"- Free heap min: {summary['free_heap']['min']} bytes")
    lines.append(f"- System utilization avg: {summary['system_util_pct_est']['avg']} %")
    if summary.get("mem_usage_pct", {}).get("avg", 0.0) > 0:
        lines.append(f"- Memory usage avg: {summary['mem_usage_pct']['avg']} %")
    lines.append("")
    lines.append("## Top Tasks by Normalized CPU")
    lines.append("")
    lines.append("| Task | Avg CPU % | Max CPU % | Min Stack High Water |")
    lines.append("|---|---:|---:|---:|")
    for item in summary.get("top_tasks_by_cpu_norm", []):
        task_name = item["name"]
        task_stats = summary["task_summary"][task_name]
        lines.append(
            f"| {task_name} | {task_stats['run_pct_avg_norm']} | {task_stats['run_pct_max_norm']} | {task_stats['stack_high_water_min']} |"
        )
    lines.append("")
    lines.append("## Notes")
    for note in summary.get("notes", []):
        lines.append(f"- {note}")
    path.write_text("\n".join(lines), encoding="utf-8")


def write_thesis_table(devices: list[dict[str, Any]], path: Path) -> None:
    lines = []
    lines.append("# 端系统资源占用测试结果")
    lines.append("")
    lines.append("| 网络设备 | CPU 利用率 | 内存占用 | 占用率最高任务1 | 占用率最高任务2 | 预期结果 |")
    lines.append("|---|---:|---:|---|---|---|")
    for device in devices:
        name = device["name"]
        summary = device["summary"]
        cpu = summary["system_util_pct_est"]["avg"]
        mem_usage = summary["mem_usage_pct"]["avg"]
        used_heap_avg = summary.get("used_heap_8bit", {}).get("avg", 0.0)
        total_heap_avg = summary.get("total_heap_8bit", {}).get("avg", 0.0)
        if used_heap_avg > 0 and total_heap_avg > 0:
            mem_text = f"{mem_usage:.2f}%（约 {used_heap_avg / 1024 / 1024:.2f} MB / {total_heap_avg / 1024 / 1024:.2f} MB）"
        elif mem_usage > 0:
            mem_text = f"{mem_usage:.2f}%"
        else:
            mem_text = f"{summary['free_heap']['avg'] / 1024 / 1024:.2f} MB空闲"

        top_tasks = summary.get("top_tasks_by_cpu_norm", [])[:2]
        top1_text = ""
        top2_text = ""
        if len(top_tasks) >= 1:
            top1 = top_tasks[0]
            top1_text = f"{top1['name']} ({top1['avg_run_pct_norm']:.2f}%)"
        if len(top_tasks) >= 2:
            top2 = top_tasks[1]
            top2_text = f"{top2['name']} ({top2['avg_run_pct_norm']:.2f}%)"

        lines.append(f"| {name} | {cpu:.2f}% | {mem_text} | {top1_text} | {top2_text} | 低于 10%，内存占用低于 2% |")
    path.write_text("\n".join(lines), encoding="utf-8")


def capture_serial(port: str, baud: int, duration: int | None, raw_path: Path | None) -> list[str]:
    serial = load_pyserial()
    lines: list[str] = []
    start = time.time()
    with serial.Serial(port, baudrate=baud, timeout=1.0) as ser:  # pragma: no cover - requires hardware
        print(f"Capturing from {port} at {baud} baud...", file=sys.stderr)
        while True:
            try:
                raw = ser.readline().decode(errors="ignore")
            except Exception:
                continue
            if raw:
                lines.append(raw)
                if raw_path is not None:
                    raw_path.parent.mkdir(parents=True, exist_ok=True)
                    with raw_path.open("a", encoding="utf-8") as fp:
                        fp.write(raw)
                if duration is not None and (time.time() - start) >= duration:
                    break
    return lines


def main() -> int:
    parser = argparse.ArgumentParser(description="Parse ESP32 serial JSON stats into thesis-friendly summaries.")
    parser.add_argument("--input", type=Path, action="append", help="Path to a saved serial log file")
    parser.add_argument("--device-name", action="append", help="Name for each input log file, same order as --input")
    parser.add_argument("--serial", help="Serial port for live capture, e.g. COM3")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baud rate")
    parser.add_argument("--duration", type=int, help="Capture duration in seconds for live serial mode")
    parser.add_argument("--raw", type=Path, help="Optional path to save the captured raw serial log")
    parser.add_argument("--csv", type=Path, help="Write sample summary CSV to this path")
    parser.add_argument("--summary", type=Path, help="Write JSON summary to this path")
    parser.add_argument("--markdown", type=Path, help="Write markdown summary to this path")
    parser.add_argument("--thesis-table", type=Path, help="Write a thesis-style comparison table to this path")
    parser.add_argument("--print-top", type=int, default=5, help="Number of top tasks to print to console")
    args = parser.parse_args()

    if not args.input and not args.serial:
        parser.error("specify either --input or --serial")

    if args.input and args.serial:
        parser.error("choose only one of --input or --serial")

    if args.serial:
        lines = capture_serial(args.serial, args.baud, args.duration, args.raw)
    else:
        lines = []
        for input_path in args.input:
            lines.extend(input_path.read_text(encoding="utf-8", errors="ignore").splitlines())

    samples = extract_json_objects(lines)
    summary = summarize_samples(samples)
    rows = [sample_summary_row(sample, summary.get("core_count", 1)) for sample in samples]

    if args.csv:
        args.csv.parent.mkdir(parents=True, exist_ok=True)
        write_csv(rows, args.csv)

    if args.summary:
        args.summary.parent.mkdir(parents=True, exist_ok=True)
        args.summary.write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")

    if args.markdown:
        args.markdown.parent.mkdir(parents=True, exist_ok=True)
        write_markdown(summary, args.markdown)

    if args.thesis_table:
        args.thesis_table.parent.mkdir(parents=True, exist_ok=True)
        device_names = args.device_name or []
        if device_names and len(device_names) != len(args.input or []):
            parser.error("--device-name count must match --input count")
        devices = []
        if args.input:
            for index, input_path in enumerate(args.input):
                device_lines = input_path.read_text(encoding="utf-8", errors="ignore").splitlines()
                device_samples = extract_json_objects(device_lines)
                devices.append({
                    "name": device_names[index] if index < len(device_names) else input_path.stem,
                    "summary": summarize_samples(device_samples),
                })
        elif args.serial:
            devices.append({"name": args.serial, "summary": summary})
        write_thesis_table(devices, args.thesis_table)

    print(f"Parsed {summary.get('sample_count', 0)} samples")
    if summary.get("sample_count", 0):
        print(f"Estimated system utilization avg: {summary['system_util_pct_est']['avg']} %")
        print(f"Free heap avg: {summary['free_heap']['avg']} bytes")
        print("Top tasks by normalized CPU:")
        for item in summary.get("top_tasks_by_cpu_norm", [])[: args.print_top]:
            print(
                f"  - {item['name']}: avg {item['avg_run_pct_norm']} %, max {item['max_run_pct_norm']} %, min stack {item['min_stack_high_water']}"
            )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

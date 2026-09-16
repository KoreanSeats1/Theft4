#!/usr/bin/env python3
"""Summarize selected XML tables exported from an Apple Game Performance trace."""

import argparse
import json
import math
import statistics
import xml.etree.ElementTree as ET
from collections import Counter, defaultdict
from pathlib import Path


def percentile(values, fraction):
    if not values:
        return None
    ordered = sorted(values)
    position = fraction * (len(ordered) - 1)
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return ordered[lower]
    return ordered[lower] * (upper - position) + ordered[upper] * (position - lower)


def stats_ns(values):
    values = list(values)
    if not values:
        return {"count": 0}
    return {
        "count": len(values),
        "min_ms": min(values) / 1e6,
        "median_ms": statistics.median(values) / 1e6,
        "p90_ms": percentile(values, 0.90) / 1e6,
        "p95_ms": percentile(values, 0.95) / 1e6,
        "p99_ms": percentile(values, 0.99) / 1e6,
        "max_ms": max(values) / 1e6,
    }


def load_table(path):
    root = ET.parse(path).getroot()
    schema = root.find(".//schema")
    columns = [col.findtext("mnemonic") for col in schema.findall("col")]
    ids = {}
    for element in root.iter():
        identity = element.get("id")
        if identity:
            ids[identity] = {
                "tag": element.tag,
                "raw": (element.text or "").strip(),
                "fmt": element.get("fmt", ""),
            }

    rows = []
    for row_element in root.findall(".//row"):
        row = {}
        for column, value in zip(columns, list(row_element)):
            if value.tag == "sentinel":
                row[column] = None
                continue
            reference = value.get("ref")
            if reference:
                row[column] = ids[reference]
            else:
                row[column] = {
                    "tag": value.tag,
                    "raw": (value.text or "").strip(),
                    "fmt": value.get("fmt", ""),
                }
        rows.append(row)
    return schema.get("name"), rows


def number(value):
    if not value or not value["raw"]:
        return None
    try:
        return int(value["raw"])
    except ValueError:
        return None


def label(value):
    if not value:
        return ""
    return value["fmt"] or value["raw"]


def interval_union_ns(intervals):
    """Time covered by at least one interval, without double-counting overlap."""
    total = 0
    end = None
    for start, stop in sorted(intervals):
        total += max(0, stop - max(start, end if end is not None else start))
        end = max(stop, end if end is not None else stop)
    return total


def summarize_gpu(rows, submission_rows, encoder_rows):
    theft4 = [row for row in rows if "Theft4" in label(row.get("process"))]
    command_buffers = defaultdict(list)
    channels = Counter()
    for row in theft4:
        command_buffer = number(row.get("cmdbuffer-id"))
        start = number(row.get("start"))
        duration = number(row.get("duration"))
        if command_buffer is None or start is None or duration is None:
            continue
        command_buffers[command_buffer].append(row)
        channels[label(row.get("channel-name"))] += 1

    spans = []
    latencies = []
    encoder_counts = []
    for grouped_rows in command_buffers.values():
        starts = [number(row.get("start")) for row in grouped_rows]
        ends = [number(row.get("start")) + number(row.get("duration")) for row in grouped_rows]
        spans.append(max(ends) - min(starts))
        encoder_counts.append(len(grouped_rows))
        group_latencies = [number(row.get("start-latency")) for row in grouped_rows]
        group_latencies = [value for value in group_latencies if value is not None]
        if group_latencies:
            latencies.append(max(group_latencies))

    frames_by_buffer = {
        number(row.get("cmdbuffer-id")): number(row.get("frame-number"))
        for row in submission_rows
        if "Theft4" in label(row.get("process"))
        and label(row.get("event-type")) == "CommandBufferSubmission"
    }
    encoder_labels = {
        (number(row.get("cmdbuffer-id")), number(row.get("encoder-id"))):
            label(row.get("encoder-label"))
        for row in encoder_rows if "Theft4" in label(row.get("process"))
    }
    frame_intervals = defaultdict(list)
    operation_intervals = defaultdict(list)
    unmapped_buffers = 0
    for buffer, grouped_rows in command_buffers.items():
        frame = frames_by_buffer.get(buffer)
        if frame is None:
            unmapped_buffers += 1
        for row in grouped_rows:
            start = number(row.get("start"))
            interval = (start, start + number(row.get("duration")))
            if frame is not None:
                frame_intervals[frame].append(interval)
            operation = encoder_labels.get((buffer, number(row.get("encoder-id"))), "unmapped")
            operation_intervals[operation].append(interval)
    return {
        "theft4_gpu_interval_rows": len(theft4),
        "command_buffers_with_gpu_work": len(command_buffers),
        "gpu_command_buffer_span": stats_ns(spans),
        "cpu_to_gpu_latency": stats_ns(latencies),
        "gpu_intervals_per_command_buffer": {
            "median": statistics.median(encoder_counts) if encoder_counts else None,
            "p95": percentile(encoder_counts, 0.95),
            "max": max(encoder_counts) if encoder_counts else None,
        },
        "channel_interval_counts": dict(channels),
        "command_buffers_without_submission_mapping": unmapped_buffers,
        "gpu_frame_span": stats_ns(
            max(end for _, end in spans) - min(start for start, _ in spans)
            for spans in frame_intervals.values()),
        "gpu_frame_active_union": stats_ns(
            interval_union_ns(spans) for spans in frame_intervals.values()),
        "gpu_active_union_by_encoder_label_ms": {
            key: interval_union_ns(spans) / 1e6
            for key, spans in sorted(operation_intervals.items(),
                                     key=lambda item: interval_union_ns(item[1]), reverse=True)
        },
    }


def summarize_submissions(rows):
    theft4 = [row for row in rows if "Theft4" in label(row.get("process"))]
    event_types = Counter(label(row.get("event-type")) for row in theft4)
    submissions = [row for row in theft4 if label(row.get("event-type")) == "CommandBufferSubmission"]
    durations = [number(row.get("duration")) for row in submissions]
    encoder_times = [number(row.get("encoder-time")) for row in submissions]
    encoder_counts = [number(row.get("num-encoders")) for row in submissions]
    durations = [value for value in durations if value is not None]
    encoder_times = [value for value in encoder_times if value is not None]
    encoder_counts = [value for value in encoder_counts if value is not None]

    frames = defaultdict(list)
    for row in submissions:
        frame = number(row.get("frame-number"))
        if frame is not None:
            frames[frame].append(row)
    frame_spans = []
    frame_encoder_times = []
    submissions_per_frame = []
    frame_encoder_counts = []
    for frame_rows in frames.values():
        starts = [number(row.get("start")) for row in frame_rows]
        ends = [number(row.get("start")) + number(row.get("duration")) for row in frame_rows]
        frame_spans.append(max(ends) - min(starts))
        frame_encoder_times.append(sum(number(row.get("encoder-time")) or 0 for row in frame_rows))
        submissions_per_frame.append(len(frame_rows))
        frame_encoder_counts.append(sum(number(row.get("num-encoders")) or 0 for row in frame_rows))

    return {
        "theft4_rows": len(theft4),
        "event_types": dict(event_types),
        "submission_duration": stats_ns(durations),
        "cpu_encoder_time_per_submission": stats_ns(encoder_times),
        "encoders_per_submission": {
            "median": statistics.median(encoder_counts) if encoder_counts else None,
            "p95": percentile(encoder_counts, 0.95),
            "max": max(encoder_counts) if encoder_counts else None,
        },
        "frames": len(frames),
        "frame_submission_span": stats_ns(frame_spans),
        "cpu_encoder_time_per_frame": stats_ns(frame_encoder_times),
        "submissions_per_frame": {
            "median": statistics.median(submissions_per_frame) if submissions_per_frame else None,
            "p95": percentile(submissions_per_frame, 0.95),
            "max": max(submissions_per_frame) if submissions_per_frame else None,
        },
        "encoders_per_frame": {
            "median": statistics.median(frame_encoder_counts) if frame_encoder_counts else None,
            "p95": percentile(frame_encoder_counts, 0.95),
            "max": max(frame_encoder_counts) if frame_encoder_counts else None,
        },
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gpu", type=Path, required=True)
    parser.add_argument("--submissions", type=Path, required=True)
    parser.add_argument("--encoders", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    gpu_schema, gpu_rows = load_table(args.gpu)
    submission_schema, submission_rows = load_table(args.submissions)
    encoder_rows = []
    if args.encoders:
        encoder_schema, encoder_rows = load_table(args.encoders)
        if encoder_schema != "metal-application-encoders-list":
            raise ValueError(f"unexpected encoder schema {encoder_schema}")
    if gpu_schema != "metal-gpu-intervals":
        raise ValueError(f"unexpected GPU schema {gpu_schema}")
    if submission_schema != "metal-application-command-buffer-submissions":
        raise ValueError(f"unexpected submission schema {submission_schema}")
    result = {
        "gpu": summarize_gpu(gpu_rows, submission_rows, encoder_rows),
        "submissions": summarize_submissions(submission_rows),
        "caveat": (
            "Apple Game Performance rows. Span is first start to last end, including gaps; "
            "active union excludes gaps and overlapping channels are counted once. "
            "Encoder-label groups can overlap each other and must not be summed. "
            "Boundary frames may be partial. These are profiled samples, not an "
            "unprofiled full-intro performance result."
        ),
    }
    rendered = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.write_text(rendered)
    print(rendered, end="")


if __name__ == "__main__":
    main()

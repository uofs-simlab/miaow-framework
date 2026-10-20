#!/usr/bin/env python3
"""Export a real-time particle-swarm MP4 and static worker/convergence PDFs."""

import argparse
import json
import math
from pathlib import Path
import re
import subprocess

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.collections import LineCollection
from matplotlib.ticker import StrMethodFormatter
import numpy as np


# Use Matplotlib's bundled Computer Modern fonts for text and mathematical notation.
plt.rcParams.update({
    "font.family": "serif",
    "font.serif": ["cmr10"],
    "mathtext.fontset": "cm",
    "axes.formatter.use_mathtext": True,
})


COLOURS = ("tab:blue", "tab:orange")


def seconds(duration):
    """CAF durations use a numeric value followed by a selected time unit."""
    value, unit = re.fullmatch(r"([0-9.eE+-]+)(ns|us|ms|s|min|h)", duration).groups()
    return float(value) * {
        "ns": 1e-9, "us": 1e-6, "ms": 1e-3, "s": 1, "min": 60, "h": 3600
    }[unit]


def unpack(value):
    """Unwrap CAF config-value type annotations in structured job output."""
    if isinstance(value, dict):
        if "@value-type" in value:
            return unpack(value["value"])
        return {key: unpack(item) for key, item in value.items()}
    if isinstance(value, list):
        return [unpack(item) for item in value]
    return value


def load_job(directory):
    """Keep optimizer snapshots separate from individual evaluation completion times."""
    job = json.loads((directory / "job_record.json").read_text())
    trace = unpack(job["output"])
    updates = trace["updates"]

    # Plot successful evaluations that returned an objective value
    tasks = [task for task in job["tasks"]
             if task["result"]["status"] == "succeeded"
             and task["result"].get("output") is not None]
    starts = np.array([seconds(task["dispatch_offset_from_job_start"]) for task in tasks])
    ends = starts + np.array([seconds(task["run_time"]) for task in tasks])
    order = np.argsort(ends, kind="stable")
    values = np.array([task["result"]["output"] for task in tasks], dtype=float)
    duration = seconds(job["run_time"])
    return {
        "policy": trace["policy"], "bounds": trace["bounds"],
        "times": np.r_[0, [update["time"] for update in updates]],
        "positions": np.array([trace["initial_positions"]]
                              + [update["positions"] for update in updates]),
        "best_positions": np.array([[np.nan, np.nan]]
                                   + [update["best_position"] for update in updates]),
        "completion_times": ends[order],
        "best_values": np.minimum.accumulate(values[order]),
        "starts": starts, "ends": ends,
        "workers": np.array([task["worker_id"] for task in tasks]),
        "duration": duration,
        # The MP4 covers the search window; PDFs also include active work finishing later.
        "animation_duration": min(duration, trace.get("wall_time_limit_seconds", duration)),
    }


def make_contour_figure(runs):
    """Build the static contours and legends, plus the three changing artists per pane."""
    figure, axes = plt.subplots(1, 2, figsize=(10, 5))
    panels = []
    for axis, run, colour in zip(axes, runs, COLOURS):
        lower, upper = run["bounds"]
        coordinates = np.linspace(lower, upper, 250)
        x, y = np.meshgrid(coordinates, coordinates)
        objective = x*x + y*y + 10 * (2 - np.cos(2*np.pi*x) - np.cos(2*np.pi*y))
        axis.contourf(x, y, objective, levels=20, cmap="Greys", alpha=0.45)
        axis.plot(0, 0, "k+", markersize=10, label="Global minimum")
        particles = axis.scatter([], [], s=24, c=colour, label="Particle positions",
                                 animated=True)
        best = axis.scatter([], [], s=150, c=colour, marker="*", edgecolors="black",
                            linewidths=0.7, label="Best evaluated point", zorder=4,
                            animated=True)
        evaluations = axis.text(0.02, 0.02, "", transform=axis.transAxes, fontsize=10,
                                bbox={"facecolor": "white", "alpha": 0.8,
                                      "edgecolor": "none"}, animated=True)
        axis.set(xlim=(lower, upper), ylim=(lower, upper), aspect="equal",
                 xlabel=run["policy"].capitalize())
        axis.xaxis.label.set_size(12)
        axis.legend(loc="lower center", bbox_to_anchor=(0.5, 1.02),
                    ncols=3, fontsize=8, frameon=False)
        panels.append((particles, best, evaluations))
    figure.tight_layout()
    return figure, panels


def draw_contours(runs, panels, time):
    """Show the latest optimizer state and the count of evaluations completed by time."""
    for run, (particles, best, evaluations) in zip(runs, panels):
        index = np.searchsorted(run["times"], time, side="right") - 1
        particles.set_offsets(run["positions"][index])
        best.set_offsets(run["best_positions"][index:index+1])
        count = np.searchsorted(run["completion_times"], time, side="right")
        evaluations.set_text(f"{count:,} evaluations")


def save_animation(runs, output, fps):
    """Encode cached contour backgrounds and changing particles directly to MP4."""
    figure, panels = make_contour_figure(runs)
    figure.canvas.draw()
    background = figure.canvas.copy_from_bbox(figure.bbox)
    width, height = figure.canvas.get_width_height()
    duration = max(run["animation_duration"] for run in runs)
    frame_count = max(1, math.ceil(duration * fps))
    times = np.arange(frame_count) / fps

    # Reuse the rendered contours and redraw only particles, best points, and counts.
    # Feeding the canvas to FFmpeg avoids a full Matplotlib savefig() for every frame.
    command = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
               "-f", "rawvideo", "-pixel_format", "rgba",
               "-video_size", f"{width}x{height}", "-framerate", str(fps),
               "-i", "pipe:0", "-an", "-c:v", "libx264", "-pix_fmt", "yuv420p",
               "-movflags", "+faststart", str(output)]
    try:
        with subprocess.Popen(command, stdin=subprocess.PIPE) as encoder:
            for time in times:
                figure.canvas.restore_region(background)
                draw_contours(runs, panels, time)
                for panel in panels:
                    for artist in panel:
                        artist.axes.draw_artist(artist)
                encoder.stdin.write(figure.canvas.buffer_rgba())
            encoder.stdin.close()
            if encoder.wait() != 0:
                raise RuntimeError("FFmpeg could not export the contour animation")
    finally:
        plt.close(figure)


def save_worker_usage(runs, output):
    """Plot full dispatch-to-result intervals with synchronous and asynchronous rows."""
    figure, axes = plt.subplots(2, 1, figsize=(5, 3), sharex=True,
                                layout="constrained")
    duration = max(run["duration"] for run in runs)
    for axis, run, colour in zip(axes, runs, COLOURS):
        segments = np.stack([np.column_stack([run["starts"], run["workers"]]),
                             np.column_stack([run["ends"], run["workers"]])], axis=1)
        axis.add_collection(LineCollection(segments, colors=colour, linewidths=5))
        worker_count = int(run["workers"].max()) + 1 if len(run["workers"]) else 0
        axis.set(xlim=(0, duration), ylim=(-0.7, max(worker_count - 0.3, 0.7)),
                 yticks=np.arange(worker_count), ylabel="Worker ID")
        axis.set_title(run["policy"].capitalize(), loc="left", fontsize=11)
    axes[-1].set_xlabel("Elapsed time (s)")
    figure.savefig(output)
    plt.close(figure)


def save_convergence(runs, output):
    """Compare best completed objectives and evaluation counts on separate y-axes."""
    figure, objective_axis = plt.subplots(figsize=(5, 3), layout="constrained")
    evaluation_axis = objective_axis.twinx()
    lines = []
    for run, colour in zip(runs, COLOURS):
        count = len(run["completion_times"])
        times = np.r_[0, run["completion_times"], run["duration"]]
        # Extend the final values to job completion, including the active-work tail.
        best = run["best_values"]
        values = np.r_[np.nan, best, best[-1] if count else np.nan]
        evaluations = np.r_[0, np.arange(1, count + 1), count]
        lines += objective_axis.step(times, values, where="post", color=colour,
                                     label=run["policy"].capitalize())
        evaluation_axis.step(times, evaluations, where="post", color=colour,
                             linestyle="--")
    objective_axis.set(xlim=(0, max(run["duration"] for run in runs)),
                       xlabel="Elapsed time (s)", ylabel="Objective value")
    # Preserve exact zeros while showing the large range in objective values.
    objective_axis.set_yscale("symlog", linthresh=1e-10)
    objective_axis.autoscale_view(scalex=False)
    objective_axis.set_ylim(bottom=max(0, objective_axis.get_ylim()[0]))
    objective_axis.yaxis.get_major_locator().set_params(numticks=6)
    objective_axis.grid(alpha=0.2)
    evaluation_axis.set(ylabel="Completed evaluations", ylim=(0, None))
    evaluation_axis.yaxis.set_major_formatter(StrMethodFormatter("{x:,.0f}"))
    objective_axis.legend(handles=lines, loc="lower center", bbox_to_anchor=(0.5, 1.02),
                          ncols=2, fontsize=9, frameon=False)
    figure.savefig(output)
    plt.close(figure)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run_directory", type=Path, help="e.g. output/run_0")
    parser.add_argument("--output-directory", type=Path,
                        help="default: run_directory/figures")
    parser.add_argument("--fps", type=int, default=30)
    args = parser.parse_args()
    if args.fps <= 0:
        parser.error("--fps must be positive")
    output = args.output_directory or args.run_directory / "figures"
    output.mkdir(parents=True, exist_ok=True)
    runs = [load_job(args.run_directory / f"job_{index}") for index in range(2)]
    save_worker_usage(runs, output / "worker_usage.pdf")
    save_convergence(runs, output / "convergence.pdf")
    save_animation(runs, output / "contours.mp4", args.fps)
    print(f"Saved contours.mp4, worker_usage.pdf, and convergence.pdf in {output}")


if __name__ == "__main__":
    main()

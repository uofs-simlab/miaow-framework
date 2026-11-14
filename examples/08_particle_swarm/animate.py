#!/usr/bin/env python3
"""Export a real-time particle-swarm MP4, unit usage PDF, and convergence PDF."""

import argparse
import json
import math
from pathlib import Path
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


def load_job(directory):
    """Keep optimizer snapshots separate from individual evaluation completion times."""
    job = json.loads((directory / "job_record.json").read_text())
    trace = job["output"]
    updates = trace["updates"]

    # Join scientific observations to the profile by task ID
    profile = job["profile"]
    columns = {name: index for index, name in enumerate(profile["columns"])}
    rows = {row[columns["task_id"]]: row for row in profile["tasks"]}
    observations = {name: index for index, name in enumerate(trace["evaluation_columns"])}
    evaluations = [(rows[item[observations["task_id"]]], item[observations["objective"]])
                   for item in trace["evaluations"]
                   if rows[item[observations["task_id"]]][columns["status"]] == "succeeded"]
    starts = np.array([row[columns["dispatch_offset_ns"]] for row, _ in evaluations],
                      dtype=float) * 1e-9
    ends = np.array([row[columns["completion_offset_ns"]] for row, _ in evaluations],
                    dtype=float) * 1e-9
    order = np.argsort(ends, kind="stable")
    values = np.array([value for _, value in evaluations], dtype=float)
    duration = job["summary"]["run_time_seconds"]
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
        "unit_indices": np.array([row[columns["unit_id"]] for row, _ in evaluations]),
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


def save_unit_usage(runs, output):
    """Plot full dispatch-to-result intervals with synchronous and asynchronous rows."""
    max_unit_count = max((int(run["unit_indices"].max()) + 1 for run in runs
                          if len(run["unit_indices"])), default=0)
    figure, axes = plt.subplots(2, 1, figsize=(9, max(3, max_unit_count / 4)), sharex=True,
                                layout="constrained")
    duration = max(run["duration"] for run in runs)
    for axis, run, colour in zip(axes, runs, COLOURS):
        segments = np.stack([np.column_stack([run["starts"], run["unit_indices"]]),
                             np.column_stack([run["ends"], run["unit_indices"]])], axis=1)
        axis.add_collection(LineCollection(segments, colors=colour, linewidths=5))
        unit_count = int(run["unit_indices"].max()) + 1 if len(run["unit_indices"]) else 0
        axis.set(xlim=(0, duration), ylim=(-0.7, max(unit_count - 0.3, 0.7)),
                 yticks=np.arange(0, unit_count, max(1, math.ceil(unit_count / 8))),
                 ylabel="Simulation unit index")
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
    runs = [load_job(args.run_directory / f"job_{job_id}")
            for job_id in range(2)]
    save_unit_usage(runs, output / "unit_usage.pdf")
    save_convergence(runs, output / "convergence.pdf")
    save_animation(runs, output / "contours.mp4", args.fps)
    print(f"Saved contours.mp4, unit_usage.pdf, and convergence.pdf in {output}")


if __name__ == "__main__":
    main()

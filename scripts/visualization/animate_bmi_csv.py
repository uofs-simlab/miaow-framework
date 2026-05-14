#!/usr/bin/env python3
"""Create MP4 animations from MIAOW BMI CSV output."""

from __future__ import annotations

import argparse
import importlib
import logging
import os
from pathlib import Path
import shutil
import tempfile
from typing import Iterable, Sequence

default_mplconfig = Path.home() / ".matplotlib"
if "MPLCONFIGDIR" not in os.environ and not os.access(default_mplconfig, os.W_OK):
    fallback_mplconfig = Path(tempfile.gettempdir()) / "miaow_mplconfig"
    fallback_mplconfig.mkdir(parents=True, exist_ok=True)
    os.environ["MPLCONFIGDIR"] = str(fallback_mplconfig)

if "XDG_CACHE_HOME" not in os.environ:
    fallback_cache = Path(tempfile.gettempdir()) / "miaow_cache"
    fallback_cache.mkdir(parents=True, exist_ok=True)
    os.environ["XDG_CACHE_HOME"] = str(fallback_cache)

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib import animation
from matplotlib.artist import Artist
import numpy as np

LOGGER = logging.getLogger("animate_bmi_csv")


def resolve_ffmpeg_executable() -> tuple[str, str]:
    try:
        imageio_ffmpeg = importlib.import_module("imageio_ffmpeg")

        return imageio_ffmpeg.get_ffmpeg_exe(), "imageio-ffmpeg"
    except Exception:
        ffmpeg_path = shutil.which("ffmpeg")
        if ffmpeg_path:
            return ffmpeg_path, "system"
    raise RuntimeError(
        "No ffmpeg executable found. Install imageio-ffmpeg in the visualization venv "
        "or install system ffmpeg."
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Render MP4 animations from MIAOW BMI CSV output files "
            "(header starts with time)."
        )
    )
    parser.add_argument(
        "input_path",
        type=Path,
        help="Path to one BMI CSV or a directory containing BMI CSV files",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=None,
        help="Directory for MP4 output (default: input file/dir location)",
    )
    parser.add_argument(
        "--fps",
        type=int,
        default=12,
        help="MP4 frame rate",
    )
    parser.add_argument(
        "--stride",
        type=int,
        default=1,
        help="Keep every Nth timestep row (1 keeps all rows)",
    )
    parser.add_argument(
        "--dpi",
        type=int,
        default=140,
        help="Figure DPI used for rendering",
    )
    return parser.parse_args()


def load_bmi_csv(
    csv_path: Path,
    stride: int,
) -> tuple[np.ndarray, list[str], np.ndarray]:
    if stride <= 0:
        raise ValueError("--stride must be > 0")

    with csv_path.open("r", encoding="utf-8") as csv_file:
        header_line = csv_file.readline().strip()

    if not header_line:
        raise ValueError(f"Empty CSV file: {csv_path}")

    header = header_line.split(",")
    if len(header) < 2:
        raise ValueError(
            f"Invalid BMI CSV header in {csv_path}: expected at least two columns"
        )

    if header[0] != "time":
        raise ValueError(
            f"Invalid BMI CSV header in {csv_path}: first column must be 'time'"
        )

    value_column_names = header[1:]
    if any(column_name == "" for column_name in value_column_names):
        raise ValueError(f"Invalid BMI CSV header in {csv_path}: empty column name")

    matrix = np.loadtxt(
        csv_path,
        delimiter=",",
        skiprows=1,
        dtype=np.float64,
    )

    if matrix.ndim == 1:
        matrix = matrix.reshape(1, -1)

    if matrix.shape[1] != len(value_column_names) + 1:
        raise ValueError(
            f"CSV {csv_path} data columns do not match its BMI header"
        )

    matrix = matrix[::stride, :]

    times = matrix[:, 0]
    values = matrix[:, 1:]

    if times.size == 0:
        raise ValueError(f"CSV {csv_path} contains no data rows")

    return times, value_column_names, values


def compute_y_limits(values: np.ndarray) -> tuple[float, float]:
    y_min = float(np.min(values))
    y_max = float(np.max(values))

    if np.isclose(y_min, y_max):
        offset = 1.0 if np.isclose(y_min, 0.0) else abs(y_min) * 0.1
        return y_min - offset, y_max + offset

    margin = 0.05 * (y_max - y_min)
    return y_min - margin, y_max + margin


def render_animation(
    csv_path: Path,
    output_path: Path,
    times: np.ndarray,
    value_column_names: Sequence[str],
    values: np.ndarray,
    fps: int,
    dpi: int,
    ffmpeg_path: str,
) -> None:
    if fps <= 0:
        raise ValueError("--fps must be > 0")
    if dpi <= 0:
        raise ValueError("--dpi must be > 0")

    plt.rcParams["animation.ffmpeg_path"] = ffmpeg_path

    plt.rcParams.update(
        {
            "figure.figsize": (8.0, 4.5),
            "font.size": 10,
        }
    )

    figure, axis = plt.subplots()
    x_values = np.arange(values.shape[1], dtype=np.int64)

    y_lower, y_upper = compute_y_limits(values)
    if len(x_values) == 1:
        axis.set_xlim(float(x_values[0]) - 0.5, float(x_values[0]) + 0.5)
    else:
        axis.set_xlim(float(x_values[0]), float(x_values[-1]))
    axis.set_ylim(y_lower, y_upper)
    axis.set_xlabel("BMI output column")
    axis.set_ylabel("Value")
    axis.grid(True, linewidth=0.4, alpha=0.4)
    if len(value_column_names) <= 12:
        axis.set_xticks(x_values)
        axis.set_xticklabels(value_column_names, rotation=35, ha="right")

    line, = axis.plot(x_values, values[0, :], color="tab:blue", linewidth=1.5)
    title = axis.set_title(f"{csv_path.stem}: frame 1/{len(times)}")
    time_text = axis.text(
        0.02,
        0.95,
        f"time = {times[0]:.6g}",
        transform=axis.transAxes,
        verticalalignment="top",
        bbox={"facecolor": "white", "alpha": 0.7, "edgecolor": "none"},
    )

    def update(frame_index: int) -> Sequence[Artist]:
        line.set_ydata(values[frame_index, :])
        title.set_text(f"{csv_path.stem}: frame {frame_index + 1}/{len(times)}")
        time_text.set_text(f"time = {times[frame_index]:.6g}")
        return line, title, time_text

    animation_obj = animation.FuncAnimation(
        figure,
        update,
        frames=len(times),
        interval=1000 / fps,
        blit=False,
    )

    writer = animation.FFMpegWriter(
        fps=fps,
        codec="libx264",
        bitrate=1800,
        extra_args=["-pix_fmt", "yuv420p"],
    )

    output_path.parent.mkdir(parents=True, exist_ok=True)
    animation_obj.save(output_path, writer=writer, dpi=dpi)
    plt.close(figure)


def resolve_input_csvs(input_path: Path) -> list[Path]:
    if input_path.is_file():
        if input_path.suffix.lower() != ".csv":
            raise ValueError(f"Expected a .csv file, got: {input_path}")
        return [input_path]

    if input_path.is_dir():
        csv_paths = sorted(path for path in input_path.glob("*.csv") if path.is_file())
        if not csv_paths:
            raise ValueError(f"No .csv files found in directory: {input_path}")
        return csv_paths

    raise ValueError(f"Input path does not exist: {input_path}")


def render_many(
    csv_paths: Iterable[Path],
    output_dir: Path,
    fps: int,
    stride: int,
    dpi: int,
    ffmpeg_path: str,
) -> list[Path]:
    outputs: list[Path] = []

    for csv_path in csv_paths:
        times, value_column_names, values = load_bmi_csv(csv_path, stride)

        output_path = output_dir / f"{csv_path.stem}.mp4"
        LOGGER.info(
            "Rendering %s -> %s (%d frames, stride=%d, %d values)",
            csv_path,
            output_path,
            len(times),
            stride,
            values.shape[1],
        )
        render_animation(
            csv_path,
            output_path,
            times,
            value_column_names,
            values,
            fps,
            dpi,
            ffmpeg_path,
        )
        outputs.append(output_path)

    return outputs


def main() -> int:
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
    logging.getLogger("matplotlib").setLevel(logging.WARNING)
    args = parse_args()
    ffmpeg_path, ffmpeg_source = resolve_ffmpeg_executable()
    LOGGER.info("Using ffmpeg from %s: %s", ffmpeg_source, ffmpeg_path)

    input_path = args.input_path.resolve()
    csv_paths = resolve_input_csvs(input_path)

    default_output_dir = input_path.parent if input_path.is_file() else input_path
    output_dir = (args.output_dir or default_output_dir).resolve()

    outputs = render_many(
        csv_paths=csv_paths,
        output_dir=output_dir,
        fps=args.fps,
        stride=args.stride,
        dpi=args.dpi,
        ffmpeg_path=ffmpeg_path,
    )

    LOGGER.info("Done. Generated %d MP4 file(s)", len(outputs))
    for output_path in outputs:
        LOGGER.info("  %s", output_path)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

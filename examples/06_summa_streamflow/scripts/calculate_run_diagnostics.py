#!/usr/bin/env python3
"""Calculate streamflow diagnostics for the Bow at Banff example.

Usage:
    .venv/bin/python scripts/calculate_run_diagnostics.py \
        --job-dir output/bow_at_banff/run_0/job_0 --make-plot
"""

from __future__ import annotations

import argparse
import logging
import os
import tempfile
from pathlib import Path

import numpy as np
import pandas as pd
import xarray as xr


LOGGER = logging.getLogger(__name__)
CFS_PER_CMS = 35.3147


# Build the command-line interface for reproducible basin-local diagnostics
def process_command_line() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Calculate KGE' (modified Kling-Gupta efficiency) and streamflow "
            "diagnostics for a completed run."
        )
    )
    parser.add_argument("--job-dir", type=Path, required=True)
    parser.add_argument(
        "--obs-file",
        default=(
            "data/bow_at_banff/observations/"
            "obs_flow.CAN_05BB001.cfs.csv"
        ),
        help="Observation CSV relative to the example directory.",
    )
    parser.add_argument(
        "--sim-var",
        default="IRFroutedRunoff",
        help="mizuRoute streamflow variable to evaluate.",
    )
    parser.add_argument(
        "--segment-index",
        type=int,
        default=1,
        help="One-based mizuRoute segment index matching the observation location.",
    )
    parser.add_argument(
        "--obs-unit",
        choices=("cfs", "cms"),
        default="cfs",
        help="Observation streamflow unit.",
    )
    parser.add_argument(
        "--start-date",
        default="2003-10-01",
        help="First date included in the KGE' calculation.",
    )
    parser.add_argument(
        "--end-date",
        default="2005-09-30",
        help="Last date included in the KGE' calculation.",
    )
    parser.add_argument(
        "--make-plot",
        action="store_true",
        help="Write an observed-versus-simulated hydrograph PNG.",
    )
    return parser.parse_args()


# Calculate the modified Kling-Gupta efficiency
def calculate_modified_kge(obs: np.ndarray, sim: np.ndarray) -> float:
    obs_std = np.std(obs, ddof=1)
    sim_std = np.std(sim, ddof=1)
    obs_mean = np.mean(obs)
    sim_mean = np.mean(sim)

    correlation = np.corrcoef(sim, obs)[0, 1]
    relative_variability = (sim_std / sim_mean) / (obs_std / obs_mean)
    bias = sim_mean / obs_mean

    return float(
        1.0
        - np.sqrt(
            (correlation - 1.0) ** 2
            + (relative_variability - 1.0) ** 2
            + (bias - 1.0) ** 2
        )
    )


# Read observed streamflow and convert it to cubic metres per second
def read_observations(obs_file: Path, obs_unit: str) -> pd.DataFrame:
    obs = pd.read_csv(
        obs_file,
        index_col=0,
        usecols=[0, 1],
        na_values=["-99.0", "-999.0", "-9999.0", "NA"],
    )
    obs.columns = ["obs"]

    # Parse known observation date formats without inference warnings
    obs_dates = obs.index.astype(str)
    parsed_dates = pd.to_datetime(obs_dates, format="%m/%d/%y", errors="coerce")
    if parsed_dates.isna().any():
        parsed_dates = pd.to_datetime(obs_dates, format="%Y-%m-%d", errors="coerce")
    if parsed_dates.isna().any():
        raise ValueError(f"Could not parse all observation dates in {obs_file}")

    obs.index = parsed_dates

    if obs_unit == "cfs":
        obs["obs"] /= CFS_PER_CMS

    return obs


# Read the selected mizuRoute segment as a streamflow time series
def read_simulation(
    sim_file: Path,
    sim_var: str,
    segment_index: int,
) -> pd.DataFrame:
    if segment_index < 1:
        raise ValueError("--segment-index must be one or greater")

    with xr.open_dataset(sim_file) as dataset:
        if sim_var not in dataset:
            raise KeyError(f"{sim_var} was not found in {sim_file}")

        streamflow = dataset[sim_var]
        segment_count = streamflow.shape[1]
        if segment_index > segment_count:
            raise ValueError(
                f"--segment-index {segment_index} exceeds {segment_count} segments"
            )

        sim = pd.DataFrame(
            {"streamflow_cms": streamflow[:, segment_index - 1].values},
            index=pd.to_datetime(dataset["time"].values),
        )

    sim.index.name = "time"
    return sim


# Merge observations and simulations over the evaluation window
def merge_evaluation_window(
    obs: pd.DataFrame,
    sim: pd.DataFrame,
    start_date: str,
    end_date: str,
) -> pd.DataFrame:
    renamed_sim = sim.rename(columns={"streamflow_cms": "sim"})
    merged = pd.concat([obs, renamed_sim], axis=1).dropna()
    merged = merged.loc[start_date:end_date]

    if merged.empty:
        raise ValueError("No overlapping observed and simulated values were found")

    return merged


# Write the hydrograph image
def write_hydrograph(merged: pd.DataFrame, kge: float, output_file: Path) -> None:
    # Use writable plotting caches outside the project tree
    mpl_config_dir = Path(tempfile.gettempdir()) / "miaow-summa-streamflow-matplotlib"
    mpl_config_dir.mkdir(parents=True, exist_ok=True)
    os.environ.setdefault("MPLCONFIGDIR", str(mpl_config_dir))
    os.environ.setdefault("XDG_CACHE_HOME", str(mpl_config_dir))

    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fig, axis = plt.subplots(figsize=(10, 4))
    axis.plot(merged.index, merged["obs"], "k-", label="Observed")
    axis.plot(merged.index, merged["sim"], "r:", label="Simulated")
    axis.set_title(
        f"Observed vs simulated streamflow "
        f"(modified Kling-Gupta efficiency, KGE' = {kge:.3f})"
    )
    axis.set_xlabel("Time")
    axis.set_ylabel("Streamflow (m^3/s)")
    axis.legend()
    axis.grid(alpha=0.3)
    fig.tight_layout()
    fig.savefig(output_file, dpi=300, bbox_inches="tight")
    plt.close(fig)


# Run diagnostics and write reproducible output artefacts
def main() -> None:
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
    logging.getLogger("matplotlib").setLevel(logging.WARNING)
    args = process_command_line()

    example_dir = Path(__file__).resolve().parents[1]
    job_dir = args.job_dir.resolve()
    streamflow_files = list((job_dir / "results").glob("streamflow_*.nc"))
    if len(streamflow_files) != 1:
        raise ValueError(f"Expected one streamflow file in {job_dir / 'results'}")
    sim_file = streamflow_files[0]
    obs_file = Path(args.obs_file)
    if not obs_file.is_absolute():
        obs_file = example_dir / obs_file
    output_dir = job_dir / "results" / (
        f"diagnostics_{args.start_date}_to_{args.end_date}"
    )

    output_dir.mkdir(parents=True, exist_ok=True)

    obs = read_observations(obs_file, args.obs_unit)
    sim = read_simulation(sim_file, args.sim_var, args.segment_index)
    merged = merge_evaluation_window(obs, sim, args.start_date, args.end_date)
    kge = calculate_modified_kge(merged["obs"].values, merged["sim"].values)

    # Save simulated streamflow over the configured evaluation window
    sim.loc[args.start_date:args.end_date].to_csv(
        output_dir / "streamflow_simulated.csv"
    )
    (output_dir / "KGE.txt").write_text(
        f"{kge:.6f}\t# KGE' (modified Kling-Gupta efficiency)\n",
        encoding="utf-8",
    )

    if args.make_plot:
        write_hydrograph(merged, kge, output_dir / "obs_vs_sim.png")

    LOGGER.info("KGE' (modified Kling-Gupta efficiency) = %.6f", kge)


if __name__ == "__main__":
    main()

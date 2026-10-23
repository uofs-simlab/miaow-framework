"""Evaluate routed Bow River streamflow and plot it against observations."""

import csv
from datetime import date
from pathlib import Path

import numpy as np
from netCDF4 import Dataset, num2date

# Bow River at Banff output paths and streamflow evaluation settings
EXAMPLE = Path(__file__).resolve().parent
DATA_DIRECTORY = EXAMPLE / "data/bow"
OUTPUT_DIRECTORY = EXAMPLE / "output/bow"
OBSERVATIONS_FILE = "obs/obs_flow.CAN_05BB001.cfs.csv"
OBSERVATIONS_COLUMN = "CAN_05BB001"
OBSERVATIONS_UNITS = "cfs"
OUTLET_REACH_ID = 710285850
EVALUATION_START = "2003-10-01"
EVALUATION_END = "2005-09-30"


def read_streamflow():
    path, = (OUTPUT_DIRECTORY / "miaow/routing").glob("*.nc")
    with Dataset(path) as output:
        reaches = np.asarray(output["reachID"][:])
        time_variable = output["time"]
        dates = num2date(time_variable[:], time_variable.units,
                        only_use_cftime_datetimes=False)

        flows = output["IRFroutedRunoff"][:]
        if np.ma.getmaskarray(flows).any() or not np.isfinite(flows).all():
            raise ValueError("Missing or non-finite routed streamflow")

        return reaches, dates, np.asarray(flows)


def modified_kge(observed, simulated):
    # KGE' uses the ratio of coefficients of variation (Kling et al., 2012)
    r = float(np.corrcoef(observed, simulated)[0, 1])
    beta = float(simulated.mean() / observed.mean())
    gamma = float((simulated.std(ddof=1) / simulated.mean()) /
                  (observed.std(ddof=1) / observed.mean()))

    return float(1 - np.linalg.norm([r - 1, beta - 1, gamma - 1]))


def plot_streamflow(dates, observed, simulated, path):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    fig, ax = plt.subplots()
    ax.plot(dates, observed, label="Observed")
    ax.plot(dates, simulated, label="SUMMA")
    ax.set(xlabel="Date", ylabel="Discharge (m³/s)")
    ax.legend()
    fig.tight_layout()
    fig.savefig(path)
    plt.close(fig)


def evaluate():
    # Identify the gauged outlet by reach ID, independently of storage order
    reaches, dates, flows = read_streamflow()
    outlet, = np.flatnonzero(reaches == OUTLET_REACH_ID)

    observations = {}
    divisor = {"cfs": 35.3147, "m3/s": 1}[OBSERVATIONS_UNITS]
    with (DATA_DIRECTORY / OBSERVATIONS_FILE).open() as source:
        for row in csv.DictReader(source):
            value = float(row[OBSERVATIONS_COLUMN])
            if np.isfinite(value) and value >= 0:
                # Preserve the source data's cfs-to-m3/s conversion where needed
                observations[date.fromisoformat(row["Date"])] = value / divisor

    evaluation_start = date.fromisoformat(EVALUATION_START)
    evaluation_end = date.fromisoformat(EVALUATION_END)
    paired = [i for i, value in enumerate(dates)
              if evaluation_start <= value.date() <= evaluation_end
              and value.date() in observations]
    if len(paired) < 2:
        raise ValueError("Insufficient observed discharge in the evaluation window")

    observed = np.array([observations[dates[i].date()] for i in paired])
    simulated = flows[paired, outlet]

    kge = modified_kge(observed, simulated)

    results = OUTPUT_DIRECTORY / "miaow/results"
    results.mkdir(exist_ok=True)

    plot_dates = [dates[i] for i in paired]
    plot_streamflow(plot_dates, observed, simulated, results / "streamflow.png")

    print(f"KGE': {kge}")


if __name__ == "__main__":
    evaluate()

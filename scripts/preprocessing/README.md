# MIAOW BMI forcing preprocessing

This folder contains small helper scripts for regenerating example input
fixtures. The active example helper is the SUMMA gauge forcing converter.

## SUMMA gauge forcing

`stage_summa_gauge_forcing_csvs.py` converts the upstream SUMMA
`test_ngen/gauge_01073000/forcing/ngen_forcing_from_summa.nc` file into the
per-catchment CSV files consumed by `examples/04_summa`.

Assume commands are run from the repository root after SUMMA has been fetched
or supplied under `third_party/summa`:

```bash
python3 scripts/preprocessing/stage_summa_gauge_forcing_csvs.py
```

The script writes:

- `examples/04_summa/data/forcings/cat-11223_forcing.csv`
- `examples/04_summa/data/forcings/cat-11224_forcing.csv`
- `examples/04_summa/data/forcings/cat-11371_forcing.csv`
- `examples/04_summa/data/forcings/cat-11410_forcing.csv`
- `examples/04_summa/data/forcings/cat-11509_forcing.csv`

The converter requires the Python `netCDF4` package in the active environment.

## Legacy SYMFLUENCE helper

`stage_symfluence_noah_owp_inputs.py` and
`example_symfluence_bow_02b_smoke.yaml` are retained as a reference for the
previous Noah-OWP example workflow. They are not used by the active example
suite.

Use Python 3.12 for that legacy environment. SYMFLUENCE `0.8.2` supports
`>=3.11,<3.14`, while newer system Python versions may be outside that range.

```bash
/opt/homebrew/bin/python3.12 -m venv scripts/preprocessing/.venv
scripts/preprocessing/.venv/bin/pip install -r scripts/preprocessing/requirements.txt
```

SYMFLUENCE expects these host libraries to be available:

- `GDAL`
- `NetCDF-C`
- `HDF5`
- `PROJ`
- `GEOS`

# MIAOW BMI forcing preprocessing

This folder contains small helper scripts for regenerating example input
fixtures. The active example helper is the SUMMA gauge forcing converter.

## SUMMA gauge forcing

`stage_summa_gauge_forcing_csvs.py` converts the upstream SUMMA
`test_ngen/gauge_01073000/forcing/ngen_forcing_from_summa.nc` file into the
per-catchment CSV files consumed by `examples/04_summa`.

Assume commands are run from the repository root after SUMMA has been fetched
or supplied under `third_party/summa`.

Create the preprocessing environment with:

```bash
python3 -m venv scripts/preprocessing/.venv
scripts/preprocessing/.venv/bin/pip install -r scripts/preprocessing/requirements.txt
```

Regenerate the forcing CSVs with:

```bash
scripts/preprocessing/.venv/bin/python \
  scripts/preprocessing/stage_summa_gauge_forcing_csvs.py
```

The script writes:

- `examples/04_summa/data/forcings/cat-11223_forcing.csv`
- `examples/04_summa/data/forcings/cat-11224_forcing.csv`
- `examples/04_summa/data/forcings/cat-11371_forcing.csv`
- `examples/04_summa/data/forcings/cat-11410_forcing.csv`
- `examples/04_summa/data/forcings/cat-11509_forcing.csv`

The converter requires the Python `netCDF4` package in the active environment.

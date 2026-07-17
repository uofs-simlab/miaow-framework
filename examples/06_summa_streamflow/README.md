# SUMMA streamflow

This optional example will run one SUMMA BMI task per Bow-at-Banff grouped response unit (GRU), with MIAOW managing forcing, parallel execution, and per-GRU runoff output. Python preparation and post-processing will assemble the daily runoff input, run mizuRoute, and compare routed streamflow.

## Status

The example scaffold, spatial input staging, forcing conversion, parallel SUMMA execution, basin-wide daily runoff validation, routing-input assembly, mizuRoute execution, and observed-versus-simulated diagnostic are complete. The staging script derives the GRU count from the native SUMMA attributes and generates an identity-coupled MIAOW realization. The forcing script writes all 52 per-GRU CSV files with BMI-compatible headers and preserves the native values and timestamps exactly. All 52 SUMMA tasks completed, all 57,044 native daily values agree with the version-matched SUMMA reference, all 56,992 retained routing-input values agree with the version-matched routing input, and the routed output passes the provisional version-matched tolerance. The diagnostic reproduces the version-matched KGE' (modified Kling-Gupta efficiency) of `0.785070`. The final Hydrolearn comparison is next.

## Reference

The local version-matched reference is stored under `_runs/06_summa_streamflow_reference_092013ac`. It was generated with SUMMA commit `092013ac926924b109f32664ae7ba37b685cec49` and mizuRoute commit `104ef346f14bb387f4c29dbb742dbfefbe2a78e6`.

The frozen routing input is `_runs/06_summa_streamflow_reference_092013ac/reference/092013ac/SUMMA/run1_day.nc`, and the routed streamflow reference is `_runs/06_summa_streamflow_reference_092013ac/reference/092013ac/mizuRoute/run1.mizuRoute.nc`.

## Prepare the spatial inputs

Create the example-local Python environment:

```bash
cd examples/06_summa_streamflow
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements.txt
```

The ignored `_staging/` directory contains local utilities for reconstructing the generated example inputs. Stage the frozen native SUMMA settings into the location used by the example:

```bash
reference=../../_runs/06_summa_streamflow_reference_092013ac
.venv/bin/python _staging/prepare_spatial_inputs.py \
    --source-summa-settings "$reference/model/settings/SUMMA" \
    --output-dir data/bow_at_banff
```

The output directory must be absent or empty. When `--output-dir` is omitted, the script stages into the current working directory.

## Prepare forcing

Generate forcing for all GRUs in mapping order:

```bash
reference=../../_runs/06_summa_streamflow_reference_092013ac
forcing_dir="$reference/model/forcing/4_SUMMA_input"
.venv/bin/python _staging/prepare_forcing.py \
    --source-forcing "$forcing_dir/CAN_05BB001_era5_distributed_2002_2009.nc" \
    --mapping data/bow_at_banff/gru_mapping.csv \
    --output-dir data/bow_at_banff/forcings
```

Pass `--selected-gru 710285850` to generate only the first GRU in native attribute order. Each generated CSV contains 26,328 period-ending hourly rows from `2002-10-01 00:00:00` through `2005-10-01 23:00:00`. Its eight forcing headers exactly match the SUMMA BMI input names, so the realization does not require `variables_names_map`. Generating all 52 files took approximately 85 seconds and produced 200,963,725 bytes in the measured development run.

## Build

Configure only this optional example from the repository root:

```bash
cmake -S . -B build-summa-streamflow \
    -DMIAOW_EXAMPLES=06_summa_streamflow
cmake --build build-summa-streamflow --target miaow_summa_streamflow
```

Run SUMMA through MIAOW and then route the completed job:

```bash
cd examples/06_summa_streamflow
.venv/bin/python run_workflow.py
```

Each invocation creates the next MIAOW `run_<id>` and uses four workers by default. The top-level runner invokes assembly and routing as separate commands, so the assembly command can be removed when MIAOW writes the consolidated runoff directly. Pass `--clear-runs` to remove all existing `output/bow_at_banff/run_*` directories before starting a new `run_0`, or pass `--max-workers N` to change MIAOW concurrency. Run `./miaow_summa_streamflow` directly when only the distributed SUMMA stage is needed.

## Validated run

The completed run is stored under `output/bow_at_banff/run_0/job_0`. The structured `job_record.json` and human-readable `run_summary.txt` preserve the 52 successful task results and timing diagnostics. The run took 147.408322 seconds with measured concurrency 3.867153, and each task completed 26,328 BMI updates.

Every result contains 26,328 finite hourly values at exact 3,600-second spacing, and the task feature IDs match the 52 mapped GRUs in mapping order. The frozen native timestep files do not contain routed runoff, so numerical validation uses the native daily statistics convention: the first 25 MIAOW values, 1,095 groups of 24 values, and the final 23 values. Across 52 GRUs and 57,044 daily values, no value fails `rtol = 1e-6` and `atol = 1e-10 m s-1`; the maximum absolute error is `3.1552644853064242e-15 m s-1`, and the maximum nonzero relative error is `3.935630382911176e-08`. The largest difference occurs for GRU `710299920` on 2005-06-19. The first 1,096 timestamps agree exactly, while each discarded partial final native record has a 13-microsecond encoding offset.

## Assemble the routing input

Convert a completed MIAOW-managed distributed SUMMA job into daily mizuRoute input:

```bash
job_dir=output/bow_at_banff/run_0/job_0
.venv/bin/python scripts/assemble_routing_input.py \
    --job-dir "$job_dir" \
    --simulation-start "2002-10-01 00:00:00" \
    --output "$job_dir/results/runoff_2002-10-01_to_2005-09-30.nc" \
    --reference-routing-input \
        ../../_runs/06_summa_streamflow_reference_092013ac/reference/092013ac/SUMMA/run1_day.nc
```

The script derives the GRU count, IDs, task order, SUMMA timestep, complete daily record count, and trailing partial-period length from the job and result time coordinate. It contains no Bow GRU IDs or fixed Bow dimensions. The optional reference argument validates the generated schema, coordinates, and runoff values.

For the completed Bow job, the script produced `results/runoff_2002-10-01_to_2005-09-30.nc` with 1,096 daily records for 52 GRUs and discarded the 23-value trailing partial day. The runoff ranges from `3.5140727938456001e-10` through `2.7003832474999999e-07 m s-1`. All 56,992 values pass `rtol = 1e-6` and `atol = 1e-10 m s-1`; the maximum absolute error is `3.1552644853064242e-15 m s-1`, and the maximum relative error is `3.9356303620376075e-08`. A repeat conversion has exactly equal coordinates and runoff values. The Hydrolearn time and `gruId` coordinates, runoff dimensions, and runoff data type match; its `gru` coordinate uses GRU IDs as `int64`, while the generated version-matched file uses one-based positions as `int32`.

## Route assembled runoff

Stage the frozen mizuRoute settings, route an existing daily runoff file, and merge the yearly outputs:

```bash
job_dir=output/bow_at_banff/run_0/job_0
.venv/bin/python scripts/run_routing.py \
    --runoff-input "$job_dir/results/runoff_2002-10-01_to_2005-09-30.nc" \
    --source-settings \
        ../../_runs/06_summa_streamflow_reference_092013ac/model/settings/mizuRoute \
    --mizuroute-executable "$(command -v mizuRoute.exe)"
```

The routing script reads only the assembled runoff contract; it does not read the MIAOW job record or per-GRU results. It writes the dated streamflow file beside the runoff file under `results/` and keeps the dated mizuRoute directory beside `results/`. New routing products must not already exist. The directory contains the staged control, unchanged topology and parameter files, four native yearly outputs, and the complete mizuRoute and `ncrcat` log. The topology and parameter SHA-256 checksums remain `034c51a291fa10ba71c6cc2c48ff39012df360d8b235f678e5e9a82e12e4cdf3` and `84721f7383a3d6718dd41e458436562f99501f0e88a0127d2f62b5341633ab15`. The run used mizuRoute `v1.2.3` at Git hash `104ef346f14bb387f4c29dbb742dbfefbe2a78e6`; mizuRoute and `ncrcat` both returned zero.

The merged `results/streamflow_2002-10-01_to_2005-09-30.nc` contains 1,096 daily times, 52 segments, 52 basins, and `time`, `reachID`, `basinID`, `basRunoff`, `dlayRunoff`, and `IRFroutedRunoff`. Its structure, coordinates, masks, and attributes match the frozen version-matched output exactly. Maximum absolute errors are `1.4210854715202004e-14 m s-1` for `basRunoff`, `9.5367431640625e-07 m3 s-1` for `dlayRunoff`, and `1.52587890625e-05 m3 s-1` for `IRFroutedRunoff`; no value fails the provisional tolerances. A clean repeat reproduces every merged field exactly. The log contains the same array-temporary and absent fill-value warnings as the frozen native run; all supplied runoff values are finite, and routing succeeds.

## Stage observations

Copy the frozen Bow at Banff observations into the example without modifying them:

```bash
.venv/bin/python _staging/prepare_observations.py \
    --source-observations \
        ../../_runs/06_summa_streamflow_reference_092013ac/obs/obs_flow.CAN_05BB001.cfs.csv \
    --output \
        data/bow_at_banff/observations/obs_flow.CAN_05BB001.cfs.csv
```

The staged file has SHA-256 `f6737dec16b7a1afc320e46a479783b6f6ee58bf2c3319597c21069af5faed7a`, identical to the frozen source.

## Generate streamflow diagnostics

Evaluate one-based mizuRoute segment index `1` from 2003-10-01 through 2005-09-30 and generate the observed-versus-simulated plot:

```bash
.venv/bin/python scripts/calculate_run_diagnostics.py \
    --job-dir output/bow_at_banff/run_0/job_0 \
    --make-plot
```

The diagnostic writes `streamflow_simulated.csv`, `KGE.txt`, and `obs_vs_sim.png` under `output/bow_at_banff/run_0/job_0/results/diagnostics_2003-10-01_to_2005-09-30`. The metric is KGE' because its variability term is the simulated-to-observed coefficient-of-variation ratio, rather than the standard-deviation ratio used by the original KGE. The generated KGE' is `0.785070`, exactly matching the frozen version-matched diagnostic. All 731 dates agree, and no simulated value fails `rtol = 1e-6` and `atol = 1e-10 m3 s-1`; the maximum absolute difference is `5.0000000015870683e-06 m3 s-1`, and the maximum relative difference is `8.0284934383652925e-08`. The largest difference occurs on 2004-08-11.

## Next step

Complete the direct comparison against the Hydrolearn `v3.3.0` routed streamflow and verify that a deliberate perturbation causes the comparison script to fail. Both filename dates are inclusive.

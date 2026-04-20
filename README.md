# MIAOW: Model-Independent Actor Orchestration Workflows for Scientific Computing

[![CI](https://github.com/uofs-simlab/miaow-framework/actions/workflows/ci.yml/badge.svg)](https://github.com/uofs-simlab/miaow-framework/actions/workflows/ci.yml)

<p align="center">
  <img src="docs/images/miaow_logo.svg" alt="MIAOW logo" width="360">
</p>

Managing concurrent numerical simulation tasks with dynamic workloads and heterogeneous hardware can feel like herding cats. **MIAOW** embraces that reality by using actor-based asynchronous orchestration so tasks run when they are ready, maximizing utilization across available resources.

MIAOW targets the [C++17 standard](https://en.cppreference.com/w/cpp/17.html), uses the [C++ Actor Framework (CAF)](https://www.actor-framework.org/), and integrates with the [Basic Model Interface (BMI)](https://bmi.csdms.io/en/stable/).


## Quick start

From the repository root, using [CMake](https://cmake.org/) (requires C++ and Fortran compilers):

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Run examples (reproducible from repo root):

```bash
./examples/00_hello/miaow_hello
./examples/01_heat/miaow_heat \
  --output_root examples/01_heat/output
./examples/02_soil_freeze_thaw/miaow_soil_freeze_thaw \
  --output_root examples/02_soil_freeze_thaw/output \
  --max_workers 2
./examples/03_fortran_heat/miaow_fortran_heat \
  --output_root examples/03_fortran_heat/output \
  --max_workers 2
```

For CLI-driven examples, the following runtime options are accepted:
`--max_workers`, `--task_request_timeout`, `--job_request_timeout`,
and `--output_root`.

## Orchestration model

MIAOW executes simulation workflows with the following actor hierarchy:

```text
Runtime (shared for process lifetime)
└─ orchestrator_actor (shared, one per Runtime)
   └─ job_actor (one per submitted job)
      └─ simulation_unit_actor (many per job, up to max_workers)
```

Typical workflow:

1. Caller invokes `Runtime::submit_job(tasks)`.
2. Runtime assigns a `job_id` and sends `(job_id, tasks)` to `orchestrator_actor`.
3. `orchestrator_actor` spawns one `job_actor` for that job.
4. `job_actor` greedily dispatches tasks to available `simulation_unit_actor` workers.
5. Completed task results are aggregated into one `JobResult`.
6. Runtime fulfils the caller's `JobHandle` future.

 Note that multiple
`submit_job(...)` calls can be running concurrently.

## Public API summary

Key types in `include/miaow.hpp`:

- `SimulationUnit`: abstract model-agnostic execution interface
- `SimulationUnitRegistry`: registers units and returns `SimulationUnitId`
- `Task`: carries `simulation_unit_id_`, positional `args_`, and runtime-set `task_output_dir_`
- `TaskResult`: per-task status, message, artifact paths, and runtime
- `Runtime`: submits jobs and returns non-blocking `JobHandle`
- `JobResult`: aggregate of `TaskResult` values plus diagnostics

For BMI models, use `miaow::BmiSimulationUnit<BmiModelType>` from
`include/miaow_bmi.hpp`.

## Code structure

```text
include/
  miaow.hpp              # public runtime/task API
  miaow_bmi.hpp          # BMI simulation-unit wrapper/BMI-facing contracts
  miaow_cli.hpp          # runtime CLI option parsing helpers for examples

src/
  core/
    runtime.cpp          # Runtime submit path and output-tree setup
    actors.cpp           # orchestrator/job/worker actor behaviour
    diagnostics.cpp      # diagnostics formatting helpers
    internal.hpp         # core-private actor declarations and JobState

  bmi/
    execute.cpp          # BMI lifecycle execution bridge for public wrapper
    dynamic.cpp          # DynamicBmiModel adapter over iso_c_fortran_bmi plugins
    forcing.cpp          # forcing config parsing + BMI input validation
    workflow.cpp         # BMI update loop/per-step output writing
    internal.hpp         # BMI-private forcing/run helper declarations

  io/
    metadata.cpp         # runtime metadata output writing
    csv.hpp              # header-only CSV header and numeric-column extraction
    utils.hpp            # header-only text/string-parsing helpers

examples/
  00_hello/
  01_heat/
  02_soil_freeze_thaw/
  03_fortran_heat/
```

## Namespace layout

- Public API: `miaow`
- Core internals: `miaow::core`
- BMI internals: `miaow::bmi`
- IO helpers: `miaow::io`
- External BMI C++ API alias in `miaow_bmi.hpp`: `miaow::bmi_api = ::bmi`

## Basic Model Interface (BMI) integration

`BmiSimulationUnit<BmiModelType>` wraps a complete BMI lifecycle per task:

1. `Initialize(config_path)`
2. zero or more `Update()` calls
3. `Finalize()`

Optional forcing is supported through a second task argument (`args[1]`) that
points to a forcing config. In forcing mode, one configured CSV column is
applied to one BMI input variable before each `Update()` step.

Each example specifies task inputs in its `main.cpp`. 

### Reading forcing data

Forcing configs are `key=value` text files:

| Key | Purpose |
| --- | --- |
| `source_type` | Forcing backend (currently only `csv`) |
| `source_path` | Path to the forcing file (relative paths resolve against the forcing config) |
| `csv_column` | Column header read from the CSV |
| `bmi_input_var` | BMI input variable set before each `Update()` |
| `time_step_size` | Forcing step length; must equal `GetTimeStep()` |
| `time_units` | Must equal `GetTimeUnits()` |

Each row maps one value from `csv_column` to `bmi_input_var` via `SetValue`.
Exhausting the forcing data before the model's `end_time` fails the task with a
`Forcing alignment error`; forcing file rows beyond `end_time` are silently ignored. In
`examples/02_soil_freeze_thaw`, three tasks exercise matched and mismatched
combinations of model end time and forcing series length: a matched 1024-day
run, a 1024-day run whose 1-day forcing is exhausted early (deliberate
failure), and a 1-day run that stops well before its 1024-day forcing ends.


### Dynamically linking BMI models

The `miaow::DynamicBmiModel` class adapts a dynamically loaded BMI plugin to MIAOW's 
C++ BMI interface. The plugin path is supplied at runtime and is passed directly
to the platform dynamic loader. The plugin must export a C-callable `register_bmi`
symbol that creates a model handle compatible with the linked 
[NOAA-OWP procedural BMI bridge](https://github.com/NOAA-OWP/iso_c_fortran_bmi),
which provides ISO C bindings for BMI-compatible Fortran models. Dynamic BMI
registration uses the same `BmiSimulationUnit` wrapper as statically linked C++ BMI
models. For example, if `plugin_path` is the path to the model's shared library, one
can register the simulation unit in MIAOW as follows:
```cpp
const miaow::SimulationUnitId my_dynamic_bmi_model =
    registry.add<miaow::BmiSimulationUnit<miaow::DynamicBmiModel>>(
        plugin_path
    );
```
This functionality is currently implemented in the `03_fortran_heat` example.

## Scheduling behaviour

Each `job_actor` maintains:

- a pending-task cursor (`next_task_to_dispatch_`)
- a queue of idle workers (`available_workers_`)

When a worker finishes, it is immediately returned to the idle queue and the
next pending task is dispatched. This greedy policy keeps workers occupied while
remaining simple and predictable.

## Diagnostics and artifacts

`Runtime::submit_job(...)` returns a non-blocking `JobHandle`; `get()` yields a
`JobResult` with per-task results and runtime diagnostics. If `RuntimeOptions::output_root_` (or CLI `--output_root`) is set, MIAOW writes:

```text
<output_root>/
└─ run_<run_id>/
   └─ job_<job_id>/
      ├─ job_metadata.json
      └─ task_<task_id>/
```

Each task receives `Task::task_output_dir_` for artifact output. In BMI runs,
one CSV file per BMI output variable is written into `task_output_dir_`.

## Build options

Top-level CMake options:

| Flag | Default | Description |
| --- | --- | --- |
| `-DBUILD_EXAMPLES` | `ON` | Build `miaow_hello`, `miaow_heat`, and `miaow_soil_freeze_thaw` |
| `-DMIAOW_USE_SYSTEM_CAF` | `ON` | Prefer a system-installed CAF via `find_package` |
| `-DMIAOW_FETCH_CAF` | `ON` | Fetch CAF with `FetchContent` if not found |
| `-DMIAOW_CAF_VERSION` | `1.1.0` | Minimum CAF version when using system CAF |
| `-DMIAOW_CAF_GIT_TAG` | `1.1.0` | CAF tag/commit for `FetchContent` |
| `-DMIAOW_ISO_C_FORTRAN_BMI_ROOT` | empty | Path to a local `NOAA-OWP/iso_c_fortran_bmi` checkout |

For [Conan](https://conan.io/)-based CI-style setup:

```bash
conan install . --output-folder=build/conan --build=missing
cmake -S . -B build -DMIAOW_FETCH_CAF=OFF
cmake --build build --config Release
```

### Configuring model examples independently

When configuring from an example directory or another project, the following flags are relevant:

| Flag | Default | Description |
| --- | --- | --- |
| `-DMIAOW_ROOT` | `<example_dir>/../..` | Path to the `miaow-framework` source tree |
| `-DMODEL_ROOT` | `${MIAOW_ROOT}/third_party/<model>` | Path to the model source tree |
| `-DEXECUTABLE_TARGET` | example default | Override executable target name |
| `-DMODEL_TARGET` | example model default | Override model-library target name |

The helper `cmake/miaow_bmi_model.cmake` provides `miaow_add_bmi_model(...)`
and `miaow_add_workflow_test(...)` for model integration and workflow tests.
Model examples keep their MIAOW-specific source and input files under
`examples/<example_name>/`. External BMI model source is resolved separately:
`MODEL_ROOT` takes precedence; otherwise, examples use
`${MIAOW_ROOT}/third_party/<MODEL_LOCAL_SUBDIR>`. If that directory is missing
and the example declares a Git repository and tag, CMake fetches the model into
that same `third_party/<MODEL_LOCAL_SUBDIR>` path. These local checkouts are
ignored by Git.

## Examples

1. `examples/00_hello`: minimal orchestration example with asynchronous job submission and diagnostics printing
2. `examples/01_heat`: Basic BMI integration using the [CSDMS heat equation example](https://github.com/csdms/bmi-example-cxx)
3. `examples/02_soil_freeze_thaw`: many-task [SoilFreezeThaw](https://github.com/NOAA-OWP/SoilFreezeThaw) workflow with per-task forcing, including matched, short, and long forcing cases
4. `examples/03_fortran_heat`: dynamic BMI plugin integration using the [CSDMS Fortran heat example](https://github.com/csdms/bmi-example-fortran)

Each example follows a common layout:

```text
examples/<example_name>/
  configs/   # model configuration files
  forcings/  # optional forcing data files
```

## Limitations

- Distributed runs are not currently supported.
- Target workloads are embarrassingly parallel tasks without inter-task communication.

## Authors and funding
MIAOW was developed at the University of Saskatchewan by Tristan Montoya and Raymond Spiteri, with funding support from the [Cooperative Institute for Research to Operations in Hydrology (CIROH)](https://ciroh.ua.edu/).

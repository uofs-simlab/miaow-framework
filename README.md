# MIAOW: Model-Independent Actor Orchestration Workflows for Scientific Computing

[![CI](https://github.com/uofs-simlab/miaow-framework/actions/workflows/ci.yml/badge.svg)](https://github.com/uofs-simlab/miaow-framework/actions/workflows/ci.yml)
<a href="https://tjbmontoya.com/miaow-framework/">
  <img alt="Documentation" src="https://img.shields.io/badge/docs-latest-blue.svg">
</a>

<p align="center">
  <img src="docs/images/miaow_logo.svg" alt="MIAOW logo" width="360">
</p>

Managing concurrent numerical simulation tasks with dynamic workloads and
heterogeneous hardware can feel like herding cats. **MIAOW** embraces that
reality by using actor-based asynchronous orchestration so tasks run when they
are ready, maximizing utilization across available resources.

MIAOW targets the [C++17 standard](https://en.cppreference.com/w/cpp/17.html),
uses the [C++ Actor Framework (CAF)](https://www.actor-framework.org/) as its
actor orchestration backend, and employs the
[Basic Model Interface (BMI)](https://bmi.csdms.io/en/stable/) for Earth
system model integration.


## Quick start

From the repository root, using [CMake](https://cmake.org/) (requires C++ and
Fortran compilers plus
[Boost.Process](https://www.boost.org/doc/libs/release/libs/process/) and
[Boost.JSON](https://www.boost.org/doc/libs/release/libs/json/)):

```sh
cmake -S . -B build
cmake --build build
```

Run each example from its own `examples/<example_name>` directory:

```sh
cd examples/00_hello
./miaow_hello
cd ../01_heat
./miaow_heat
cd ../02_soil_freeze_thaw
./miaow_soil_freeze_thaw --output_root ./output --max_workers 2
cd ../03_dynamic_bmi
./miaow_dynamic_bmi --output_root ./output --max_workers 2
cd ../04_noah_owp
./miaow_noah_owp --realization data/realization_config.json --output_root ./output
```

## Documentation

Generated API documentation uses [Doxygen](https://www.doxygen.nl/) for C++
docstring extraction and [Sphinx](https://www.sphinx-doc.org/) with
[Breathe](https://www.breathe-doc.org/) for HTML rendering. Doxygen must be
installed separately and available on `PATH`.

You can build the docs locally from a dedicated Python environment:

```sh
python3 -m venv docs/.venv
docs/.venv/bin/pip install -r docs/requirements.txt
rm -rf docs/_build/html docs/_build/doctrees
docs/.venv/bin/sphinx-build -b html -d docs/_build/doctrees docs docs/_build/html
```

The generated HTML entry point is `docs/_build/html/index.html`. Currently, the
documentation is deployed from GitHub Actions to
[https://tjbmontoya.com/miaow-framework/](https://tjbmontoya.com/miaow-framework/).

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
5. Completed task results are wrapped into one `JobRecord`.
6. Runtime fulfils the caller's `JobHandle` future.

Multiple `submit_job(...)` calls can be running concurrently.
Tasks with unknown `SimulationUnitId` values fail individually; other tasks in
the same job continue through the normal scheduler.

## Scheduling behaviour

Each `job_actor` maintains:

- a pending-task cursor (`next_task_to_dispatch_`)
- a queue of idle workers (`available_workers_`)

When a worker finishes, it is immediately returned to the idle queue and the
next pending task is dispatched. This greedy policy keeps workers occupied while
remaining simple and predictable.

## Public API summary

Key types in `include/miaow.hpp`:

- `SimulationUnit`: abstract model-agnostic execution interface
- `SimulationUnitRegistry`: registers units and returns `SimulationUnitId`
- `TaskMetadata`: string labels preserved for reporting and postprocessing
- `Task`: carries `simulation_unit_id_`, positional `args_`, descriptive
  `metadata_`, and runtime-set output fields
- `TaskResult`: simulation-unit status, message, and artifact paths
- `TaskRecord`: runtime task ID, simulation unit ID, metadata, result, and runtime
- `Runtime`: submits jobs and returns non-blocking `JobHandle`
- `JobRecord`: run ID, job ID, task records, and job runtime

## Code structure

```text
include/
  miaow.hpp              # public runtime/task API
  miaow_bmi.hpp          # BMI simulation-unit wrapper and realization contracts
  miaow_io.hpp           # public I/O, diagnostics, and CLI API

src/
  core/
    runtime.cpp          # Runtime submit path and output tree setup
    actors.cpp           # orchestrator/job/worker actor behaviour
    internal.hpp         # core-private actor declarations and JobState

  bmi/
    execute.cpp          # BMI pipeline lifecycle execution
    dynamic.cpp          # DynamicBmiModel adapter for C++/Fortran plugins
    forcing.cpp          # streaming external BMI forcing CSV source
    io.cpp               # BMI config, artifact, and output CSV helpers
    pipeline.cpp         # ordered BMI module pipeline and coupling application
    realization.cpp      # BMI realization parsing, task, and registration helpers
    subprocess/
      caller.cpp         # parent-side BMI subprocess launcher
      runner.cpp         # BMI subprocess executable entry point
    internal.hpp         # BMI-private subprocess, forcing, and output types

  io/
    cli.cpp              # runtime command-line option parsing
    csv.cpp              # CSV parsing plus shared forcing helper utilities
    diagnostics.cpp      # diagnostics formatting and printing helpers
    job_record.cpp       # runtime job record output writing
    internal.hpp         # IO-private declarations and helpers

examples/
  00_hello/
  01_heat/
  02_soil_freeze_thaw/
  03_dynamic_bmi/
  04_noah_owp/
```

## Namespace layout

- Public API: `miaow`
- Core internals: `miaow::core`
- BMI internals: `miaow::bmi`
- Internal I/O helpers: `miaow::io`
- External [BMI C++ API](https://github.com/csdms/bmi-cxx/tree/master) alias:
  `miaow::bmi_api = ::bmi`

## Build options

Top-level CMake options:

| Flag | Default | Description |
| --- | --- | --- |
| `-DBUILD_EXAMPLES` | `ON` | Build all example programs |
| `-DMIAOW_BOOST_PROVIDER` | `system` | Boost provider: `system`, `fetch`, or `auto` |
| `-DMIAOW_BOOST_VERSION` | `1.86.0` | Boost version used by the `FetchContent` fallback |
| `-DMIAOW_CAF_PROVIDER` | `auto` | CAF provider: `system`, `fetch`, or `auto` |
| `-DMIAOW_CAF_VERSION` | `1.1.0` | CAF version used by `find_package` and `FetchContent` |
| `-DMIAOW_DEFAULT_MODEL_BASE_DIR` | `${MIAOW_ROOT}/third_party` | Default base directory where external model sources are resolved/fetched |
| `-DMIAOW_ISO_C_FORTRAN_BMI_ROOT` | empty | Optional explicit path to a valid local `NOAA-OWP/iso_c_fortran_bmi` checkout; when empty, CMake looks for `iso_c_fortran_bmi` in `${MIAOW_DEFAULT_MODEL_BASE_DIR}/` and fetches into that location if missing |

Provider values are interpreted as follows:

- `system`: require `find_package(...)` to provide the dependency
- `fetch`: skip `find_package(...)` and use `FetchContent`
- `auto`: try `find_package(...)`, then use `FetchContent` if not found

For [Conan](https://conan.io/)-based CI-style setup:

```sh
conan install . --output-folder=build/conan --build=missing
cmake -S . -B build \
  -DMIAOW_BOOST_PROVIDER=system \
  -DMIAOW_CAF_PROVIDER=system
cmake --build build --config Release
```

## Basic Model Interface (BMI) integration

Use `BmiSimulationUnit<BmiModelType>` from `include/miaow_bmi.hpp` to run BMI-compatible 
models. The simulation unit creates the task-level model or pipeline, executes the
BMI lifecycle, writes artifacts, and returns a `TaskResult`.

There are three common ways to use it:

- `BmiSimulationUnit<YourModel>` runs one linked BMI model per task, where
  `YourModel` is a subtype of `miaow::bmi_api::Bmi` defined in the code.
- `BmiSimulationUnit<DynamicBmiModel>` runs one BMI model loaded from a dynamic
  plugin per task.
- `BmiSimulationUnit<BmiPipeline>` runs a complete pipeline described by the
  task config, which may be obtained from partitioning of a NextGen-style realization file.

Each BMI task starts from one typed `BmiPipelineConfig`. Task arguments are
`caf::config_value` entries, so the typed config is converted before being
stored in the task:

```cpp
task.args_[0] = miaow::bmi_pipeline_config_to_value(pipeline_config);
```

During execution, MIAOW writes that `BmiPipelineConfig` beside the task results
as `<task_output_name>.json`, which serves as the top-level config file used for
the BMI pipeline lifecycle:

1. `Initialize(<task_output_name>.json)`
2. zero or more `Update()` calls
3. `Finalize()`

Within the `Initialize(...)`, `Update()`, and `Finalize()` calls, the pipeline
calls the corresponding BMI methods for each module in the pipeline, in order (for
single-model runs, the pipeline consists of just one module).

### Executing a static BMI model

Use `BmiSimulationUnit<YourModel>` when the BMI model class is linked into the
executable. The simulation unit constructs one model object per task. The task
config comes from `make_bmi_model_config(...)`, which records the model
initialization file plus optional forcing and variable mapping.

```cpp
miaow::SimulationUnitRegistry registry;
const miaow::SimulationUnitId heat =
    registry.add<miaow::BmiSimulationUnit<BmiHeat>>();

const miaow::BmiPipelineConfig pipeline_config =
    miaow::make_bmi_model_config(heat_config);
const std::vector<miaow::Task> tasks{
    {heat, miaow::bmi_pipeline_config_to_value(pipeline_config)}};
```

At execution time, MIAOW wraps the model in `BmiPipeline` so the same lifecycle,
forcing, output writing, and artifact tracking are used for static and dynamic
BMI work.

### Executing a dynamic BMI model

Use `BmiSimulationUnit<DynamicBmiModel>` when the task should run one BMI model
loaded from a plugin library. `BmiModuleConfig` describes how to load the plugin.
The task config still comes from `make_bmi_model_config(...)` because the task
runs one model.

```cpp
const miaow::BmiModuleConfig module_config(
    "bmi_fortran", miaow::DynamicBmiBackend::fortran, plugin_path);

miaow::SimulationUnitRegistry registry;
const miaow::SimulationUnitId model =
    registry.add<miaow::BmiSimulationUnit<miaow::DynamicBmiModel>>(module_config);

const miaow::BmiPipelineConfig pipeline_config =
    miaow::make_bmi_model_config(init_config);
const std::vector<miaow::Task> tasks{
    {model, miaow::bmi_pipeline_config_to_value(pipeline_config)}};
```

Use the C++ backend for plugins that export `bmi_model_create()` and
`bmi_model_destroy(...)`. Use the Fortran backend for plugins that export
`register_bmi(void**)` for the linked procedural BMI bridge.

### Executing a dynamic BMI pipeline

Use `BmiSimulationUnit<BmiPipeline>` when the task's `BmiPipelineConfig`
describes the model modules directly. This is the path used by realization
workflows and by pipelines with more than one model module.

```cpp
miaow::PartitionedBmiRealization realization(realization_path);

miaow::SimulationUnitRegistry registry;
const miaow::SimulationUnitId pipeline =
    registry.add<miaow::BmiSimulationUnit<miaow::BmiPipeline>>(
        miaow::BmiExecutionMode::subprocess);

const std::vector<miaow::Task> tasks =
    miaow::build_bmi_realization_tasks(realization, pipeline);
```

`PartitionedBmiRealization` reads a NextGen-style realization and produces one
`BmiPipelineConfig` per feature. `build_bmi_realization_tasks(...)` turns those
feature configs into MIAOW tasks for the registered pipeline unit.

Subprocess mode runs one BMI lifecycle in a child process, which is useful for
BMI models that are not safe to run concurrently in one process. The Noah
example uses subprocess mode.

For hand-built dynamic pipelines, construct a `BmiPipelineConfig` from a vector
of `BmiModuleConfig` entries. Each module config carries the module name,
backend, plugin path, initialization config, and optional variable map.

### Reading forcing data

Forcing is represented in the pipeline config as `global.forcing`, then
normalized into an internal BMI forcing module. The forcing module is always
appended after the realization module; when no forcing is configured, it is
initialized with an empty config and acts as a no-op. With configured forcing,
the module reads a CSV file and exposes data columns as BMI outputs.
`BmiPipeline` derives explicit scalar couplings from each module's
`variables_names_map`, applies those couplings immediately before the target
model updates, and advances the forcing source later in the same ordered update
loop.

The BMI pipeline config forcing object uses the following fields:

| Key | Purpose |
| --- | --- |
| `provider` | `CsvFile` for resolved task-local forcing |
| `file` | Path to the CSV forcing file |

Each BMI module may include `variables_names_map` in its `params` object. The
map is interpreted as `BMI input variable -> CSV header`.

If `variables_names_map` is omitted, MIAOW derives a model-authoritative
identity map from `GetInputVarNames()` and expects same-named CSV headers.
Explicit map entries override that identity behaviour completely.

MIAOW's external forcing reader supports CSV only. The file must define a
`time` or `Time` column plus all required numeric data columns. Supported
timestamp formats are `YYYY-MM-DD HH:MM:SS` and `YYYY/MM/DD HH:MM:SS`.
MIAOW treats the first forcing row as the model-start forcing values. Later
forcing rows are streamed as the model advances; each row-to-row timestamp
delta must match the receiving BMI model timestep after unit normalization.

Exhausting the forcing data before the model's `end_time` fails the task with a
`Forcing alignment error`; forcing rows beyond `end_time` are silently ignored.
In `examples/02_soil_freeze_thaw`, three tasks exercise matched and mismatched
combinations of model end time and forcing series length: a matched 512-day
run, a 512-day run whose 1-day forcing is exhausted early (deliberate
failure), and a 1-day run that stops well before its 512-day forcing ends.

In global NextGen-style realization files, `CsvPerFeature` forcing enables
MIAOW-managed CSV forcing for each feature. Partitioning resolves that global
forcing entry to per-task `CsvFile` forcing.

## Diagnostics and artifacts

`Runtime::submit_job(...)` returns a non-blocking `JobHandle`; `get()` yields a
`JobRecord` with per-task records and job runtime. An output write failure for
`job_record.json` is reported by `JobHandle::get()` as an exception and does
not change task statuses. If
`RuntimeOptions::output_root_` (or CLI `--output_root`) is set, MIAOW writes:

```text
<output_root>/
└─ run_<run_id>/
   └─ job_<job_id>/
      ├─ job_record.json
      ├─ <feature_id or task_<task_id>>.json
      └─ results/
         └─ <feature_id or task_<task_id>>.csv
```

Persisted run directories start at `run_0`. When an output root already
contains run directories, the next run uses the largest existing run ID plus
one so as not to overwrite any existing output.

Each task receives `Task::task_output_dir_` for the job `results/` directory
and `Task::task_output_name_` for task-specific artifact filenames. BMI tasks
write the resolved BMI pipeline config in the job directory and one CSV file
per task under `results/`, with BMI output variables as columns. They report
job-relative filenames such as `cat-552.json` and `results/cat-552.csv` in
`TaskResult::artifact_paths_`. `job_record.json` records those paths under each
task record's `result` field and records each task's descriptive
`Task::metadata_`, but it does not embed artifact file contents. If a task has
non-empty `metadata_["feature_id"]`, that value becomes the output file stem;
otherwise the runtime uses `task_<task_id>`. Output stems must be unique within
one job and cannot contain path separators. Subprocess BMI tasks also write
task-specific `bmi_subprocess_*_input.json`,
`bmi_subprocess_*_result.json`, `bmi_subprocess_*_stdout.log`, and
`bmi_subprocess_*_stderr.log` files beside `job_record.json`. When no task
output directory is assigned, these subprocess control files use a temporary
directory instead.

## Examples

Run examples from their own `examples/<example_name>` directory:

- `examples/00_hello`: minimal orchestration and diagnostics output
- `examples/01_heat`: BMI integration using the
  [CSDMS heat example](https://github.com/csdms/bmi-example-cxx)
- `examples/02_soil_freeze_thaw`: many-task
  [SoilFreezeThaw](https://github.com/NOAA-OWP/SoilFreezeThaw) workflow with
  per-task forcing, including matched, short, and long forcing cases
- `examples/03_dynamic_bmi`: dynamic C++ and Fortran BMI plugin integration
  using the CSDMS heat examples
- `examples/04_noah_owp`: [Noah-OWP-Modular](https://github.com/NOAA-OWP/noah-owp-modular)
  run using subprocess pipeline execution and one MIAOW task per staged
  catchment feature described by a NextGen-style realization. Noah-OWP runs in
  a subprocess because the model is not thread safe. The plugin is compiled
  with `NGEN_ACTIVE`,
  `NGEN_OUTPUT_ACTIVE`, and `NGEN_FORCING_ACTIVE` defined so Noah-OWP receives
  forcing through MIAOW's BMI `SetValue` path and writes outputs through
  MIAOW's BMI `GetValue` pipeline when `--output_root` is set.

The Noah-OWP example provides a very small smoke test configuration with three
catchments from the Bow River basin, consisting of the following files produced using
[SYMFLUENCE](https://github.com/symfluence-org/SYMFLUENCE):
`data/configs/cat-*.input`, `data/forcings/*_forcing.csv`,
`data/parameters/*`, `data/catchments.geojson`, and
`data/realization_config.json`. Regenerate those files manually from the
repository root with:

```sh
./scripts/preprocessing/.venv/bin/python \
  ./scripts/preprocessing/stage_symfluence_noah_owp_inputs.py \
  --config ./scripts/preprocessing/example_symfluence_bow_02b_smoke.yaml
```

Each example directory follows a common layout:

```text
examples/<example_name>/
  data/             # example input data (tracked in repo)
    configs/        # configuration files
    forcings/       # forcing data files (when needed)
    parameters/     # parameter tables (when needed)
  extern/           # model-backed examples: symlinks to model sources (created by CMake)
  output/           # runtime/model output data (generated, not tracked)
```

### Configuring model examples independently

When configuring from an example directory or another project, these flags are
relevant:

| Flag | Default | Description |
| --- | --- | --- |
| `-DMIAOW_ROOT` | `<example_dir>/../..` | Path to the `miaow-framework` source tree |
| `-DMIAOW_DEFAULT_MODEL_BASE_DIR` | `${MIAOW_ROOT}/third_party` | Default base directory where external model sources are resolved/fetched |
| `-DEXECUTABLE_TARGET` | example default | Override executable target name |

The helper `cmake/miaow_bmi_model.cmake` provides `miaow_add_bmi_model(...)`
and `miaow_add_workflow_test(...)` for model integration and workflow tests.
Model examples keep their MIAOW-specific source and input files under
`examples/<example_name>/`. External BMI model source is resolved separately:
each model-backed example uses its corresponding `MIAOW_*_MODEL_ROOT` override
when set, otherwise it resolves to
`${MIAOW_DEFAULT_MODEL_BASE_DIR}/<model-subdir>`. If a required source
directory is missing and the example declares a Git repository and tag, CMake
fetches the model from GitHub into the resolved location under
`MIAOW_DEFAULT_MODEL_BASE_DIR`.

### Example command line options

All examples accept the following runtime command-line options:

| Option | Default | Description |
| --- | --- | --- |
| `--max_workers` | `1` | Maximum number of worker actors used for each job |
| `--task_request_timeout` | `5` | Timeout for task requests sent to worker actors (seconds) |
| `--job_request_timeout` | `300` | Timeout for job result requests (seconds) |
| `--output_root` | `""` | Root directory path for runtime-managed output folders (disabled by default) |
| `--realization` | `data/realization_config.json` | Noah realization path |

Public CLI parsers throw `std::runtime_error` for unrecognized arguments,
missing option values, and invalid option values. Example executables catch
these errors, print the message, and exit with status `1`.

## Limitations

- Distributed-memory parallelism is not currently supported.
- Target workloads are embarrassingly parallel tasks without inter-task communication.
- Windows is not supported and there is no plan to add such support.

## Authors and funding
MIAOW was developed at the University of Saskatchewan by Tristan Montoya and Raymond Spiteri, with funding support from the [Cooperative Institute for Research to Operations in Hydrology (CIROH)](https://ciroh.ua.edu/).

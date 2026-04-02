# MIAOW: Model-independent actor orchestration workflows for scientific computing

[![CI](https://github.com/uofs-simlab/miaow-framework/actions/workflows/ci.yml/badge.svg)](https://github.com/uofs-simlab/miaow-framework/actions/workflows/ci.yml)

Managing concurrent numerical simulation tasks with dynamic workloads and heterogeneous hardware can feel like herding cats. **MIAOW** embraces that reality by using actor-based asynchronous orchestration so tasks run when they are ready, maximizing utilization across available resources.

MIAOW uses the [C++ Actor Framework (CAF)](https://www.actor-framework.org/) and interfaces with the [Basic Model Interface (BMI)](https://bmi.csdms.io/en/stable/).

## Quick start

From the repository root:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Run examples (reproducible from repo root):

```bash
./examples/00_hello/miaow_hello
./examples/01_heat/miaow_heat --config examples/01_heat/config_400x400.txt
ctest --test-dir build -R miaow_example_02_soil_freeze_thaw --output-on-failure
```

## Orchestration model

MIAOW executes simulation workflows with this actor hierarchy:

```text
Runtime (shared for process lifetime)
└─ orchestrator_actor (shared, one per Runtime)
   └─ job_actor (one per submitted job)
      └─ simulation_unit_actor (many per job, up to max_workers)
```

Typical flow:

1. Caller invokes `Runtime::submit_job(tasks)`.
2. Runtime assigns a `job_id` and sends `(job_id, tasks)` to `orchestrator_actor`.
3. `orchestrator_actor` spawns one `job_actor` for that job.
4. `job_actor` greedily dispatches tasks to available `simulation_unit_actor` workers.
5. Completed task results are aggregated into one `JobResult`.
6. Runtime fulfils the caller's `JobHandle` future.

Each `Runtime` instance keeps one shared orchestrator, so multiple
`submit_job(...)` calls can be in flight concurrently.

## Public API summary

Key types in `include/miaow.hpp`:

- `SimulationUnit`: abstract model-agnostic execution interface
- `SimulationUnitRegistry`: registers units and returns `SimulationUnitId`
- `Task`: carries `simulation_unit_id_`, positional `args_`, and runtime-set `task_output_dir_`
- `TaskResult`: per-task status, message, artifacts, and runtime
- `Runtime`: submits jobs and returns non-blocking `JobHandle`
- `JobResult`: aggregate of `TaskResult` values plus diagnostics

For BMI models, use `miaow::BmiSimulationUnit<BmiModelType>` from
`include/miaow_bmi.hpp`.

## Code structure

```text
include/
  miaow.hpp              # public runtime/task API
  miaow_bmi.hpp          # BMI simulation-unit wrapper + BMI-facing contracts
  miaow_interface.hpp    # CLI parsing helpers for workflow examples

src/
  core/
    runtime.cpp          # Runtime submit path and output-tree setup
    actors.cpp           # orchestrator/job/worker actor behaviour
    diagnostics.cpp      # diagnostics formatting helpers
    internal.hpp         # core-private actor declarations and JobState

  bmi/
    forcing.cpp          # forcing config parsing + BMI input validation
    workflow.cpp         # BMI update loop + per-step output writing

  io/
    csv.hpp/cpp          # CSV header and numeric-column extraction
    utils.hpp/cpp        # small text helpers (for example trim)

examples/
  00_hello/
  01_heat/
  02_soil_freeze_thaw/
```

## Namespace layout

- Public API: `miaow`
- Core internals: `miaow::core`
- BMI internals: `miaow::bmi`
- IO helpers: `miaow::io`
- External BMI C++ API alias in `miaow_bmi.hpp`: `miaow::bmi_api = ::bmi`

## BMI integration

`BmiSimulationUnit<BmiModelType>` wraps a complete BMI lifecycle per task:

1. `Initialize(config_path)`
2. zero or more `Update()` calls
3. `Finalize()`

Optional forcing is supported through a second task argument (`args[1]`) that
points to a forcing config. In forcing mode, one configured CSV column is
applied to one BMI input variable before each `Update()` step.

## Scheduling behaviour

Each `job_actor` maintains:

- a pending-task cursor (`next_task_to_dispatch_`)
- a queue of idle workers (`available_workers_`)

When a worker finishes, it is immediately returned to the idle queue and the
next pending task is dispatched. This greedy policy keeps workers occupied while
remaining simple and predictable.

## Diagnostics and artifacts

`Runtime::submit_job(...)` returns a non-blocking `JobHandle`; `get()` yields a
`JobResult` with per-task results and runtime diagnostics.

If `RuntimeOptions::output_root_` (or CLI `--output_root`) is set, MIAOW writes:

```text
<output_root>/
└─ run_<run_id>/
   └─ job_<job_id>/
      ├─ job_manifest.json
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

For Conan-based CI-style setup:

```bash
conan install . --output-folder=build/conan --build=missing
cmake -S . -B build -DMIAOW_FETCH_CAF=OFF
cmake --build build --config Release
```

### Configuring model examples independently

When configuring from an example directory or another project, these flags are relevant:

| Flag | Default | Description |
| --- | --- | --- |
| `-DMIAOW_ROOT` | `<example_dir>/../..` | Path to the `miaow-framework` source tree |
| `-DMODEL_ROOT` | model-specific | Path to the BMI model source tree |
| `-DEXECUTABLE_TARGET` | example default | Override executable target name |
| `-DMODEL_TARGET` | example model default | Override model-library target name |

The helper `cmake/miaow_bmi_model.cmake` provides `miaow_add_bmi_model(...)`
and `miaow_add_workflow_test(...)` for model integration and workflow tests.

## Examples

1. `examples/00_hello`: minimal orchestration example with asynchronous job submission and diagnostics printing.
2. `examples/01_heat`: BMI heat example integration (`csdms/bmi-example-cxx`).
3. `examples/02_soil_freeze_thaw`: SoilFreezeThaw BMI integration (`NOAA-OWP/SoilFreezeThaw`).

## Limitations

- Distributed runs are not currently supported.
- Target workloads are embarrassingly parallel tasks without inter-task communication.
- Artifact path sanitization is not yet enforced.

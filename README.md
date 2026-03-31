# MIAOW: Model-Independent Actor Orchestration Workflows for Scientific Computing

[![CI](https://github.com/uofs-simlab/miaow-framework/actions/workflows/ci.yml/badge.svg)](https://github.com/uofs-simlab/miaow-framework/actions/workflows/ci.yml)

Managing concurrent numerical simulation tasks with dynamic workloads and heterogeneous hardware can feel like herding cats. **MIAOW** embraces that reality by using actor-based asynchronous orchestration so tasks run when they are ready, maximizing utilization across available resources.

MIAOW uses the [C++ Actor Framework (CAF)](https://www.actor-framework.org/) and interfaces with the [Basic Model Interface (BMI)](https://bmi.csdms.io/en/stable/).

## Orchestration structure

MIAOW executes simulation workflows using the actor hierarchy proposed by [Klenk and Spiteri (2024)](https://doi.org/10.1007/s10586-024-04318-5):

```text
Runtime (shared for process lifetime)
└─ orchestrator_actor (shared, one per Runtime)
   └─ job_actor (one per submitted job)
      └─ simulation_unit_actor (many per job, up to user-specified max_workers)
```

The event sequence for a typical job is as follows:

1. Caller calls `Runtime::submit_job(tasks)`.
2. Runtime creates `job_id` and stores (`job_id -> promise`) in `pending_jobs_`.
3. Runtime sends (`job_id`, `tasks`) to `orchestrator_actor`.
4. `orchestrator_actor` spawns one `job_actor` for that submission.
5. `job_actor` dispatches task(s) to `simulation_unit_actor` workers.
6. `simulation_unit_actor` workers return per-task result data to `job_actor`.
7. `job_actor` aggregates and sends `JobResult` to `orchestrator_actor`.
8. `orchestrator_actor` finds `job_id` in `pending_jobs_`, fulfils promise, and erases the entry.
9. Caller-held `JobHandle` future becomes ready.
10. Caller calls `job.get()` and receives `JobResult`.

Each `Runtime` instance keeps one shared `orchestrator_actor`, so multiple `submit_job(...)` calls can be in progress concurrently.

`SimulationUnit` is a model-agnostic abstract type. Each model/solver implements `execute(task)` in a derived class. For BMI-based models, MIAOW provides a templated `BmiSimulationUnit<BmiModelType>` class, where `BmiModelType` derives from `bmi::Bmi` and implements the interface defined by [BMI C++ specification](https://github.com/csdms/bmi-cxx). This keeps orchestration-facing behaviour shared while allowing different concrete BMI model types.

MIAOW supports registering multiple simulation units and selecting one per task. `SimulationUnitRegistry` provides `add<T>(...)`, which returns a `SimulationUnitId`. Each task carries `simulation_unit_id_`, so one job can mix task types:

```cpp
miaow::SimulationUnitRegistry simulation_unit_registry;
miaow::SimulationUnitId hello = simulation_unit_registry.add<HelloSimulationUnit>();
miaow::SimulationUnitId heat = simulation_unit_registry.add<miaow::BmiSimulationUnit<BmiHeat>>();

miaow::Runtime runtime(simulation_unit_registry); // uses default RuntimeOptions

std::vector<miaow::Task> tasks{
    miaow::Task{hello, "hello"},
    miaow::Task{heat, "config_400x400.txt"},
};
```

## Task scheduling

MIAOW employs a simple greedy algorithm for task scheduling. Each `job_actor` coordinates work using two queues:

- A task queue: represented by `next_task_to_dispatch_` which is incremented as tasks are dispatched to workers
- A worker queue: `available_workers_`, which stores idle worker IDs that index the `workers_` vector of `simulation_unit_actor` handles

Each dispatch step pops one idle worker ID from `available_workers_`, uses it to select a `simulation_unit_actor` from `workers_`, and assigns the current pending task from `next_task_to_dispatch_`. When that worker finishes, the worker ID is returned to `available_workers_`, and `job_actor` immediately dispatches the next pending task from `next_task_to_dispatch_` if one exists. A job finishes when all tasks have been dispatched and no tasks are still running.

## Limitations

- Distributed runs are not currently supported. Jobs execute within a single runtime instance on a single machine.
- Only embarrassingly parallel workloads are supported for now, where tasks are independent and do not require inter-task communication.
- Artifact path sanitization is not yet enforced in this minimal release.

## Diagnostics

`Runtime::submit_job(...)` returns a non-blocking `JobHandle`. Calling `get()` on the handle yields a `JobResult` containing one `TaskResult` per task plus timing diagnostics. Each `TaskResult` includes `status_`, `message_`, `artifact_paths_`, and `run_time_s_`. You can submit multiple jobs before waiting on any one result:

```cpp
// Submit non-blocking jobs
miaow::JobHandle job = runtime.submit_job(tasks);
miaow::JobHandle another_job = runtime.submit_job(other_tasks);

// Wait for completion and get results
miaow::JobResult job_result = job.get();
miaow::JobResult another_job_result = another_job.get();

for (const miaow::TaskResult& task_result : job_result.task_results_) {
    std::cout << task_result.message_ << "\n";
}

// Print the diagnostics
miaow::print_diagnostics(job_result.diagnostics_);
```

The diagnostics block includes the following information:

- `job_id`
- `total_time` (in seconds)
- `concurrency`, computed as `sum(task_times) / total_time`
- `task_times` listed by task ID (in seconds)

## Artifact layout

File output is disabled by default. To enable it, set `output_root` in
`RuntimeOptions` or pass `--output_root <path>` on the command line. When
enabled, MIAOW creates the following directory tree:

```text
<output_root>/
└─ run_<run_id>/
   └─ job_<job_id>/
      ├─ job_manifest.json
      └─ task_<task_id>/
```

Each submitted task receives a runtime-injected `Task::task_output_dir_` set
to the corresponding `task_<task_id>` directory. Simulation units can write
files there and report relative paths through `TaskResult::artifact_paths_`.
When `output_root` is empty, `task_output_dir_` is also empty and no
directories or files are created.

`job_manifest.json` is a per-job metadata record written when a job reaches a
final `JobResult`. The file includes:

- top-level identifiers: `run_id`, `job_id`
- diagnostics: `total_time_s`, `task_times_s`
- per-task entries in `tasks`: `task_id`, `simulation_unit_id`, `status`
  (`"succeeded"` or `"failed"`), `message`, `run_time_s`, `artifact_paths`

`BmiSimulationUnit` writes one CSV file per BMI output variable into
`task_output_dir_` when output is enabled, with one row per completed timestep.
Each file is named `<var_name>.csv` and has a header row `time,val_0,...,val_N`
followed by one data row per `Update()` call.

## Build instructions

The following commands can be used to build `libmiaow.a` with [CMake](https://cmake.org/):

```bash
cmake -S . -B build
cmake --build build
```

For convenience, we have also provided a build script `scripts/build.sh`. In either case, the library will be built in the `build` directory. Additionally, if the `BUILD_EXAMPLES` flag is set to `ON` (the default), the example programs will be placed directly in their respective subdirectories within the top-level `examples/` directory (e.g., `examples/00_hello/miaow_hello`). A complete list of project-specific CMake flags is provided below:

| Flag | Default | Description |
| --- | --- | --- |
| `-DBUILD_EXAMPLES` | `ON` | Build the `miaow_hello`, `miaow_heat`, and `miaow_soil_freeze_thaw` examples |
| <span style="white-space:nowrap">`-DMIAOW_USE_SYSTEM_CAF`</span> | `ON` | Prefer a system-installed CAF via `find_package` |
| `-DMIAOW_FETCH_CAF` | `ON` | Fall back to fetching CAF with CMake `FetchContent` if not found by `find_package` |
| `-DMIAOW_CAF_VERSION` | `1.1.0` | Minimum CAF version for system `find_package` |
| `-DMIAOW_CAF_GIT_TAG` | `1.1.0` | CAF git tag or commit for CMake `FetchContent` |

When configuring a model example independently (from inside `examples/01_heat` or `examples/02_soil_freeze_thaw` or within your own project), use the following flags:

| Flag | Default | Description |
| --- | --- | --- |
| `-DMIAOW_ROOT` | `<example_dir>/../..` | Path to the `miaow-framework` source tree to link against; this default only works when the configured example directory is inside `miaow-framework/examples/` |
| `-DMODEL_ROOT` | model-specific local default | Path to the model source tree for that example (`bmi-example-cxx` for `01_heat`, `SoilFreezeThaw` for `02_soil_freeze_thaw`); if this path is missing, the example helper fetches the pinned upstream repository automatically |
| `-DEXECUTABLE_TARGET` | example default (`miaow_hello`, `miaow_heat`, or `miaow_soil_freeze_thaw`) | Override the executable target name when configuring an example directory independently |
| `-DMODEL_TARGET` | example model default (`miaow_heat_model` or `miaow_soil_freeze_thaw_model`) | Override the model-library target name for model examples configured independently |

The model examples use the reusable helper `cmake/miaow_bmi_model.cmake` so BMI model
linking can be scripted with a short wrapper `CMakeLists.txt` around
`miaow_add_bmi_model(...)`. This process can be used when integrating MIAOW with new 
BMI-compatible models other than the ones used in our demonstrations.
The same module also provides `miaow_add_workflow_test(...)` to keep example CTest
definitions concise.
Model resolution order is local-first: if `MODEL_ROOT` exists, MIAOW uses it directly;
if `MODEL_ROOT` is missing, the helper clones the pinned upstream repository.
By default, fetched model sources are placed in `<example_dir>/model/`.

## Tests

Run the example-based [CTest](https://cmake.org/cmake/help/book/mastering-cmake/chapter/Testing%20With%20CMake%20and%20CTest.html) suite with:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Examples

We describe below how to run the example programs, noting that in all cases, it is assumed that we begin within the respective `examples/<xx_example_name>` subdirectory. 

1. `examples/00_hello`: A basic orchestration example using CAF that submits two async two-task jobs, echoes task strings, prints per-task `TaskResult` status/message, and prints per-job diagnostics. Run it as `./miaow_hello`.

2. `examples/01_heat`: Uses the CSDMS BMI heat example from [csdms/bmi-example-cxx](https://github.com/csdms/bmi-example-cxx), pinned to commit `cd6347165021e607204820420eb8af14df26fa59` (loaded from `MODEL_ROOT` or fetched automatically if missing). The MIAOW wrapper is `examples/01_heat/main.cpp`. Run it as `./miaow_heat --config config_400x400.txt` (or with multiple config files such as `./miaow_heat --config config_400x400.txt config_800x800.txt --max_workers 2`) to print per-task job results to `stdout`.

3. `examples/02_soil_freeze_thaw`: Uses [NOAA-OWP/SoilFreezeThaw](https://github.com/NOAA-OWP/SoilFreezeThaw), pinned to commit `d7a4bdbbe773d79c5d5b81a1255e5288cec01e5b` (loaded from `MODEL_ROOT` or fetched automatically if missing). The MIAOW wrapper is `examples/02_soil_freeze_thaw/main.cpp`. Run it as `./miaow_soil_freeze_thaw --config model/soil_freeze_thaw-src/configs/laramie_config_standalone.txt`.

## License 
This code ~~is~~ **will probably be** released under the [MIT license](https://opensource.org/license/mit).

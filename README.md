# MIAOW: Model-Independent Actor Orchestration Workflows for Scientific Computing

[![CI](https://github.com/uofs-simlab/miaow-framework/actions/workflows/ci.yml/badge.svg)](https://github.com/uofs-simlab/miaow-framework/actions/workflows/ci.yml)

Managing concurrent numerical simulation tasks with dynamic workloads and heterogeneous hardware can feel like herding cats. **MIAOW** embraces that reality by using actor-based asynchronous orchestration so tasks run when they are ready, maximizing utilization across available resources.

MIAOW uses the [C++ Actor Framework (CAF)](https://www.actor-framework.org/) and interfaces with the [Basic Model Interface (BMI)](https://bmi.csdms.io/en/stable/).

## Orchestration structure

MIAOW executes a job through the following request/result flow, based on the architecture proposed by [Klenk and Spiteri (2024)](https://doi.org/10.1007/s10586-024-04318-5):

```text
Actor hierarchy (ownership/lifetime):
  Runtime (shared for process lifetime)
  └─ orchestrator_actor (shared, one per Runtime)
     └─ job_actor (one per submitted job)
        └─ simulation_unit_actor (many per job, up to max_workers)

Request flow:
  caller -> Runtime::submit_job(tasks)
  Runtime -> pending_jobs_ : store (job_id -> promise)
  Runtime -> orchestrator_actor : (job_id, tasks)
  orchestrator_actor -> job_actor : start job
  job_actor -> simulation_unit_actor workers : Task(s)

Result flow:
  simulation_unit_actor workers -> job_actor : per-task result data
  job_actor -> orchestrator_actor : JobResult
  orchestrator_actor -> pending_jobs_ : find job_id, fulfil promise, erase entry
  JobHandle future -> caller : ready; job.get() returns JobResult
```

Each `Runtime` instance keeps one shared `orchestrator_actor`, so multiple `submit_job(...)` calls can be in progress concurrently.

`SimulationUnit` is a model-agnostic abstract type. Each model/solver implements `execute(task)` in a derived class. For BMI-based models, MIAOW provides a templated `BmiSimulationUnit<BmiModelType>` class, where `BmiModelType` derives from `bmi::Bmi` and implements the interface defined by [BMI C++ specification](https://github.com/csdms/bmi-cxx). This keeps orchestration-facing behavior shared while allowing different concrete BMI model types.

## Task scheduling

MIAOW employs a simple greedy algorithm for task scheduling. Each `job_actor` coordinates work using two queues:

- A task queue: represented by `next_task_to_dispatch_` which is incremented as tasks are dispatched to workers
- A worker queue: `available_workers_`, which stores idle worker IDs that index the `workers_` vector of `simulation_unit_actor` handles

Each dispatch step pops one idle worker ID from `available_workers_`, uses it to select a `simulation_unit_actor` from `workers_`, and assigns the current pending task from `next_task_to_dispatch_`. When that worker finishes, the worker ID is returned to `available_workers_`, and `job_actor` immediately dispatches the next pending task from `next_task_to_dispatch_` if one exists. A job finishes when all tasks have been dispatched and no tasks are still running.

## Limitations

- Distributed runs are not currently supported. Jobs execute within a single runtime instance on a single machine.
- Only embarrassingly parallel workloads are supported for now, where tasks are independent and do not require inter-task communication.

## Diagnostics

`Runtime::submit_job(...)` returns a non-blocking `JobHandle`. Calling `get()` on the handle yields a `JobResult` containing simulation results (currently a placeholder vector of `std::string`) and timing diagnostics. You can submit multiple jobs before waiting on any one result:

```cpp
// Submit non-blocking jobs
miaow::JobHandle job = runtime.submit_job(tasks);
miaow::JobHandle another_job = runtime.submit_job(other_tasks);

// Wait for completion and get results
miaow::JobResult job_result = job.get();
miaow::JobResult another_job_result = another_job.get();

// Print the diagnostics
miaow::print_diagnostics(job_result.diagnostics);
```

The diagnostics block includes the following information:

- `job_id`
- `total_time` (in seconds)
- `concurrency`, computed as `sum(task_times) / total_time`
- `task_times` listed by task ID (in seconds)
 
## Build instructions

The following commands can be used to build `libmiaow.a` with [CMake](https://cmake.org/):

```bash
cmake -S . -B build
cmake --build build
```

For convenience, we have also provided a build script `scripts/build.sh`. In either case, the library will be built in the `build` directory. Additionally, if the `BUILD_EXAMPLES` flag is set to `ON` (the default), the example programs will be placed directly in their respective subdirectories within the top-level `examples/` directory (e.g., `examples/00_hello/miaow_hello`). A complete list of project-specific CMake flags is provided below:

| Flag | Default | Description |
| --- | --- | --- |
| `-DBUILD_EXAMPLES` | `ON` | Build the `miaow_hello` and `miaow_heat` examples |
| <span style="white-space:nowrap">`-DMIAOW_USE_SYSTEM_CAF`</span> | `ON` | Prefer a system-installed CAF via `find_package` |
| `-DMIAOW_FETCH_CAF` | `ON` | Fall back to fetching CAF with CMake `FetchContent` if not found by `find_package` |
| `-DMIAOW_CAF_VERSION` | `1.1.0` | Minimum CAF version for system `find_package` |
| `-DMIAOW_CAF_GIT_TAG` | `1.1.0` | CAF git tag or commit for CMake `FetchContent` |

## Tests

Run the example-based [CTest](https://cmake.org/cmake/help/book/mastering-cmake/chapter/Testing%20With%20CMake%20and%20CTest.html) suite with:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Examples

We describe below how to run the example programs, noting that in all cases, it is assumed that we begin within the respective `examples/<xx_example_name>` subdirectory. 

1. `examples/00_hello`: A basic orchestration example using CAF that submits two async two-task jobs, echoes task strings, and prints per-job diagnostics. Run it as `./miaow_hello`.

2. `examples/01_heat`: Adapted from the heat equation example for the Basic Model Interface (BMI) provided by the [Community Surface Dynamics Modeling System (CSDMS)](https://csdms.colorado.edu/wiki/Main_Page). It can be run as `./miaow_heat --config config_400x400.txt` (or with multiple config files such as `./miaow_heat --config config_400x400.txt config_800x800.txt --max_workers 2`) and prints per-task job results to `stdout`.

## License 
This code ~~is~~ **will probably be** released under the [MIT license](https://opensource.org/license/mit).

Overview
========

Orchestration model
-------------------

MIAOW executes simulation workflows with the following actor hierarchy, which is
inspired by `SUMMA-Actors <https://github.com/uofs-simlab/Summa-Actors>`_
(`Klenk and Spiteri, 2024 <https://doi.org/10.1007/s10586-024-04318-5>`_),
but differs in that simulation units write their own outputs rather than using
a centralized file access actor:

.. code-block:: text

   Runtime (shared for process lifetime)
   `-- orchestrator_actor (shared, one per Runtime)
       `-- job_actor (one per submitted job)
           `-- simulation_unit_actor (many per job, up to max_workers)

A typical workflow includes the following steps:

1. Caller invokes ``Runtime::submit_job(tasks)``.
2. Runtime assigns a ``job_id`` and sends ``(job_id, tasks)`` to
   ``orchestrator_actor``.
3. ``orchestrator_actor`` spawns one ``job_actor`` for that job.
4. ``job_actor`` greedily dispatches tasks to available
   ``simulation_unit_actor`` workers.
5. Each worker resolves ``SimulationUnitId``. In-process tasks construct a fresh
   ``SimulationUnit`` instance, while subprocess tasks use worker-owned
   subprocess state.
6. Completed task results are wrapped into one ``JobRecord``.
7. Runtime fulfils the caller's ``JobHandle`` future.

Multiple ``submit_job(...)`` calls can be running concurrently. Tasks with
unknown ``SimulationUnitId`` values fail individually; other tasks in the same
job continue through the normal scheduler.

Subprocess actor system
-----------------------

``ExecutionMode::single_task_subprocess`` and
``ExecutionMode::persistent_subprocess`` keep the parent scheduler unchanged
while moving BMI task lifecycles into subprocesses. The parent process still
owns ``Runtime``, ``orchestrator_actor``, ``job_actor``, and the normal
``simulation_unit_actor`` workers. For each task, the parent worker resolves the
registered ``SimulationUnitId``, constructs a fresh ``SimulationUnit`` and
passes it an ``ExecutionContext``. BMI pipeline units use that context to launch
or reuse the resolved subprocess executable and exchange the task with CAF
remote actor messaging.

The resolved executable is the ``Task::subprocess_path_`` field on the task
being executed when non-empty, otherwise the compile-time default
``miaow_subprocess`` runner path.

The current subprocess implementation is POSIX-specific: the parent passes an
inherited file descriptor to the child process for the endpoint exchange.

The subprocess path has two CAF actor systems:

.. code-block:: text

   Parent process
   `-- Runtime actor_system
       `-- simulation_unit_actor
           `-- launches or reuses subprocess executable

   Subprocess
   `-- subprocess actor_system
       `-- BMI worker actor

A single-task subprocess run follows this sequence:

1. The parent creates a one-way POSIX pipe, which is an anonymous byte buffer
   managed by the operating system kernel. The pipe is exposed through two
   `file descriptors <https://en.wikipedia.org/wiki/File_descriptor>`_: a read
   end and a write end. Note that these just describe the two ends of the pipe and
   are not "files" in the sense of the filesystem. The parent launches the subprocess
   with ``--connection-fd`` naming the write end as it appears inside the subprocess.
   The parent keeps the read end.
2. The subprocess initializes its own CAF actor system, spawns the BMI worker
   actor, and publishes that worker on ``127.0.0.1`` through the CAF middleman.
3. The subprocess writes one newline-delimited JSON endpoint message to the
   inherited write descriptor. The message contains the host and TCP port where
   the BMI worker actor is published, and the parent reads those bytes from the
   pipe's read end.
4. The parent reads the endpoint, connects to the published worker with
   ``remote_actor(...)``, and sends the serialized ``Task`` over CAF.
5. The BMI worker validates the task, constructs the ``BmiPipeline`` inside the
   subprocess, runs the BMI lifecycle, and returns one ``TaskResult``.
6. The parent stops the subprocess and lets the normal ``job_actor``
   aggregation path record the task result.

Persistent subprocess mode follows the same endpoint exchange for the first
persistent task handled by a worker. The subprocess worker backend is the same
as task-scoped mode; only the parent-owned lifetime changes. Later persistent
tasks with the same resolved subprocess path reuse that worker's subprocess and
remote actor. Changing ``Task::subprocess_path_`` stops the current worker
process and starts one with the new path. Launch, process, or CAF request
failures fail the active task with ``Subprocess error`` and clear the worker
process state; the next persistent task launches a replacement. Each task still
constructs and finalizes a fresh ``BmiPipeline`` inside the subprocess.
Persistent subprocesses are scoped to one submitted job, and each job worker
owns at most one active subprocess, so ``RuntimeOptions::max_workers_`` bounds
their concurrency.

This design isolates BMI model construction and execution from the parent
process without changing job scheduling or ``JobRecord`` aggregation. Subprocess
stdout, stderr, and endpoint diagnostics are described in :doc:`bmi`.

Scheduling behaviour
--------------------

Each ``job_actor`` maintains:

- a pending-task cursor, ``next_task_to_dispatch_``
- a queue of idle workers, ``available_workers_``

When a worker finishes, it is immediately returned to the idle queue and the
next pending task is dispatched. This greedy policy keeps workers occupied
while remaining simple and predictable.

Each task constructs a fresh ``SimulationUnit`` instance and calls
``execute(...)`` with an ``ExecutionContext``. Persistent subprocess state is
owned by the worker actor and is exposed to simulation units through the
context, not through ``Task`` or ``SimulationUnit`` storage. BMI units construct
and finalize fresh BMI model lifecycles per task.

Public API summary
------------------

These are the key types in ``include/miaow.hpp``:

- ``SimulationUnit``: abstract model-agnostic execution interface
- ``ExecutionContext``: runtime-local worker services passed to
  ``SimulationUnit::execute(...)``
- ``SimulationUnitRegistry``: registers units and returns ``SimulationUnitId``
- ``TaskMetadata``: string labels preserved for reporting and postprocessing
- ``Task``: carries ``simulation_unit_id_``, positional ``args_``,
  descriptive ``metadata_``, and runtime-set output fields
- ``TaskResult``: simulation-unit status, message, and artifact paths
- ``TaskRecord``: runtime task ID, simulation unit ID, metadata, result, and
  runtime
- ``Runtime``: submits jobs and returns non-blocking ``JobHandle``
- ``JobRecord``: run ID, job ID, task records, and job runtime

Limitations
-----------

- Distributed-memory parallelism is not currently supported.
- Target workloads are embarrassingly parallel tasks without inter-task
  communication.
- Windows is not supported, and there is no plan to add such support.

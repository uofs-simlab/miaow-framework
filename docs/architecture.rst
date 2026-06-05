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
5. Completed task results are wrapped into one ``JobRecord``.
6. Runtime fulfils the caller's ``JobHandle`` future.

Multiple ``submit_job(...)`` calls can be running concurrently. Tasks with
unknown ``SimulationUnitId`` values fail individually; other tasks in the same
job continue through the normal scheduler.

Subprocess actor system
-----------------------

``ExecutionMode::single_task_subprocess`` keeps the parent scheduler unchanged
while moving one BMI task lifecycle into a fresh subprocess. The parent process
still owns ``Runtime``, ``orchestrator_actor``, ``job_actor``, and the normal
``simulation_unit_actor`` workers. When a BMI pipeline task selects subprocess
execution, the parent worker launches the configured subprocess executable and
uses CAF remote actor messaging for the task exchange.

The current subprocess implementation is POSIX-specific: the parent passes an
inherited file descriptor to the child process for the endpoint exchange.

The subprocess path has two CAF actor systems:

.. code-block:: text

   Parent process
   `-- Runtime actor_system
       `-- simulation_unit_actor
           `-- launches subprocess executable for one task

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
   subprocess, runs the BMI lifecycle, returns one ``TaskResult``, and quits.
6. The parent waits for subprocess exit, then lets the normal ``job_actor``
   aggregation path record the task result.

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

Public API summary
------------------

These are the key types in ``include/miaow.hpp``:

- ``SimulationUnit``: abstract model-agnostic execution interface
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

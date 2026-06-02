Architecture
============

Orchestration model
-------------------

MIAOW executes simulation workflows with the following actor hierarchy:

.. code-block:: text

   Runtime (shared for process lifetime)
   `-- orchestrator_actor (shared, one per Runtime)
       `-- job_actor (one per submitted job)
           `-- simulation_unit_actor (many per job, up to max_workers)

A typical workflow has these steps:

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
- Windows is not supported and there is no plan to add such support.

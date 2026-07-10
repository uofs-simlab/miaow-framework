Architecture
============

Orchestration model
-------------------

MIAOW executes simulation workflows with the following actor hierarchy, which is
inspired by `SUMMA-Actors <https://github.com/uofs-simlab/Summa-Actors>`_
(`Klenk and Spiteri, 2024 <https://doi.org/10.1007/s10586-024-04318-5>`_):

.. image:: images/orchestration_overview_tikz.png
   :alt: MIAOW orchestration actor hierarchy
   :align: center
   :width: 95%

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

.. _subprocess-execution:

Subprocess execution
--------------------

``ExecutionMode::single_task_subprocess`` and
``ExecutionMode::persistent_subprocess`` keep the parent scheduler unchanged
while isolating BMI model execution from the parent process. The parent process
still owns ``Runtime``, ``orchestrator_actor``, ``job_actor``, and the normal
``simulation_unit_actor`` workers.

For subprocess tasks, the parent worker constructs the registered
``SimulationUnit`` and passes it an ``ExecutionContext``.
``DynamicBmiSimulationUnit`` uses that context to launch or reuse the resolved
subprocess executable, send the serialized ``Task`` to a subprocess worker
actor, and receive one ``TaskResult``. An empty ``Task::subprocess_path_`` uses
the compile-time ``miaow_subprocess`` runner path; a non-empty value selects the
subprocess executable for that task.

Single-task subprocess mode stops the subprocess after each task. Persistent
subprocess mode reuses one subprocess per job worker for tasks with the same
resolved subprocess path. Each task still constructs and finalizes a fresh
``BmiPipeline`` inside the subprocess. Persistent subprocesses are scoped to one
submitted job, and ``RuntimeOptions::max_workers_`` bounds their concurrency.

Scheduling behaviour
--------------------

.. image:: images/task_scheduling_tikz.png
   :alt: MIAOW task scheduling queue and worker dispatch
   :align: center
   :width: 35%

Each ``job_actor`` maintains:

- a pending-task cursor, ``next_task_to_dispatch_``
- a queue of idle workers, ``available_workers_``

When a worker finishes, it is immediately returned to the idle queue and the
next pending task is dispatched. Each task constructs a fresh
``SimulationUnit`` instance and calls ``execute(...)`` with an
``ExecutionContext``. Persistent subprocess state is owned by the worker actor
and is exposed to simulation units through the context, not through ``Task`` or
``SimulationUnit`` storage. BMI units construct and finalize fresh BMI model
lifecycles per task.

Limitations
-----------

- Distributed-memory parallelism is not currently supported.
- Target workloads are embarrassingly parallel tasks without inter-task
  communication.
- Windows is not supported, and there is no plan to add such support.

Architecture
============

Orchestration model
-------------------

MIAOW executes simulation workflows through an actor hierarchy inspired by `SUMMA-Actors <https://github.com/uofs-simlab/Summa-Actors>`_ (`Klenk and Spiteri, 2024 <https://doi.org/10.1007/s10586-024-04318-5>`_), as illustrated in the figure below:

.. image:: images/orchestration_overview_tikz.png
   :alt: MIAOW orchestration actor hierarchy
   :align: center
   :width: 90%

A typical workflow includes the following steps:

1. Caller invokes ``Runtime::submit_job(tasks)``.
2. Runtime assigns a ``job_id`` and sends ``(job_id, tasks)`` to ``orchestrator_actor``.
3. ``orchestrator_actor`` spawns one ``job_actor`` for that job.
4. ``job_actor`` greedily dispatches tasks to available ``simulation_unit_actor`` workers.
5. Each worker resolves ``SimulationUnitId``. In-process tasks construct a fresh ``SimulationUnit`` instance, while subprocess tasks use worker-owned subprocess state.
6. Completed task results are wrapped into one ``JobRecord``.
7. Runtime fulfils the caller's ``JobHandle`` future.

Multiple ``submit_job(...)`` calls can be running concurrently.

.. _subprocess-execution:

Subprocess execution
--------------------

``ExecutionMode::single_task_subprocess`` and ``ExecutionMode::persistent_subprocess`` isolate model execution from the parent process. The parent process still owns ``Runtime``, ``orchestrator_actor``, ``job_actor``, and the normal ``simulation_unit_actor`` workers, but defer the actual model execution to a subprocess with its own actor system.

Single-task subprocess mode stops the subprocess after each task. Persistent subprocess mode reuses one subprocess per job worker for tasks with the same resolved subprocess path.

Scheduling behaviour
--------------------

MIAOW uses a simple greedy scheduling strategy to dispatch tasks to available workers, as illustrated in the figure below:

.. image:: images/task_scheduling_tikz.png
   :alt: MIAOW task scheduling queue and worker dispatch
   :align: center
   :width: 45%

Each ``job_actor`` maintains:

- a pending-task cursor, ``next_task_to_dispatch_``
- a queue of idle workers, ``available_workers_``

When a worker finishes, it is immediately returned to the idle queue and the next pending task is dispatched. Each task constructs a fresh ``SimulationUnit`` instance and calls the corresponding ``execute(...)`` method, which takes a ``Task`` and ``ExecutionContext`` and returns a ``TaskResult``. The ``SimulationUnit`` instance is destroyed after the task completes, although tasks run in a subprocess may reuse the subprocess for later tasks.

Limitations
-----------

- Distributed-memory parallelism is not currently supported.
- Target workloads are embarrassingly parallel tasks without inter-task communication.
- Windows is not currently supported.

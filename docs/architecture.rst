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
5. Each worker resolves ``SimulationUnitId``. In-process tasks construct a fresh ``SimulationUnit`` instance, whereas subprocess tasks use worker-owned subprocess state.
6. Completed task results are wrapped into one ``JobRecord``.
7. Runtime fulfils the caller's ``JobHandle`` future.

Each job creates at most ``RuntimeOptions::max_workers_`` workers, or one worker per task when the job contains fewer tasks than ``RuntimeOptions::max_workers_``. This limit applies per job rather than across the entire ``Runtime``, so concurrent jobs can collectively run more than ``max_workers_`` tasks.

.. _subprocess-execution:

Subprocess execution
--------------------

The ``ExecutionMode::single_task_subprocess`` and ``ExecutionMode::persistent_subprocess`` execution modes isolate model execution from the parent process. When using these modes, the parent process still owns ``Runtime``, ``orchestrator_actor``, ``job_actor``, and the normal ``simulation_unit_actor`` workers, but defers the actual model execution to a subprocess with its own actor system. In ``single_task_subprocess`` mode, the subprocess is stopped after each task, whereas ``persistent_subprocess`` mode reuses one subprocess per job worker for tasks with the same resolved subprocess path. A subprocess task that fails or exceeds ``RuntimeOptions::task_request_timeout_`` preserves its subprocess logs and terminates the subprocess; the worker launches a fresh subprocess for its next task.

Scheduling behaviour
--------------------

MIAOW uses a simple greedy scheduling strategy to dispatch tasks to available workers, as illustrated in the figure below, where the orange cat icons represent tasks:

.. image:: images/task_scheduling_tikz.png
   :alt: MIAOW task scheduling queue and worker dispatch
   :align: center
   :width: 45%

Each ``job_actor`` maintains the following state to manage task dispatch:

- a pending-task cursor, ``next_task_to_dispatch_``
- a queue of idle workers, ``available_workers_``

When a worker finishes, it is immediately returned to the idle queue and the next pending task is dispatched. Each task constructs a fresh ``SimulationUnit`` instance and calls the corresponding ``execute(...)`` method, which takes a ``Task`` and ``ExecutionContext`` and returns a ``TaskResult``. The ``SimulationUnit`` instance is destroyed after the task completes, although tasks run in a subprocess may reuse the subprocess for later tasks.

Limitations
-----------

- Although subprocess execution is currently supported using remote actors, MIAOW does not currently support distributed-memory parallelism across multiple nodes. This is a planned feature for future releases.
- MIAOW currently targets embarrassingly parallel workflows. A future release will support more complex workflows involving task dependencies.
- Microsoft Windows is not currently supported, and there are no immediate plans to support it. MIAOW has been tested on Linux and macOS, and future releases will continue to support these platforms.

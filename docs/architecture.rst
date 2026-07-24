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

1. Caller invokes ``Runtime::submit_job(task)``, ``Runtime::submit_job(tasks)``, or ``Runtime::submit_job(tasks, std::move(job_controller))``. In the third overload, ``job_controller`` has type ``std::unique_ptr<JobController>``, and ``Runtime`` takes ownership of the controller. The controller's role is to generate additional tasks after each task completes, and it can inspect all completed task records to determine what new tasks to generate.
2. Runtime assigns a ``job_id`` and sends the tasks and job controller to ``orchestrator_actor``.
3. ``orchestrator_actor`` spawns one ``job_actor`` for that job. The job actor owns the controller and task vector and creates enough reusable ``simulation_unit_actor`` workers for the initial tasks, up to ``RuntimeOptions::max_workers_``.
4. The ``job_actor`` greedily dispatches tasks to available ``simulation_unit_actor`` workers.
5. Each worker resolves ``Task::simulation_unit_id_``. For an in-process task, the worker constructs a fresh ``SimulationUnit`` instance; for a subprocess task, it uses worker-owned subprocess state.
6. After each task execution returns success or failure, ``job_actor`` creates its ``TaskRecord``, passes all completed task records to its controller, appends returned tasks to its task vector, adds workers if needed, and returns to step 4. This continues until no tasks are waiting or running.
7. Completed ``TaskRecord`` values are sorted by ``TaskId`` and stored in one ``JobRecord``.
8. Runtime fulfils the caller's ``JobHandle`` future.

When a controller generates new tasks, the job actor counts entries in its task vector that do not yet have a corresponding completed task record and adds workers only if this count exceeds the existing worker count. Workers are not removed while the job is running, so the job retains the largest worker count that has been required at any point so far, capped by ``RuntimeOptions::max_workers_``, until the job completes or shuts down. Idle ``simulation_unit_actor`` workers do not occupy CAF scheduler threads, so retaining them does not reduce scheduler capacity. Persistent subprocesses remain alive and retain their process resources while their workers are idle, but MIAOW does not reserve dedicated CPU cores for them.

.. _subprocess-execution:

Subprocess execution
--------------------

The ``ExecutionMode::single_task_subprocess`` and ``ExecutionMode::persistent_subprocess`` execution modes isolate model execution from the parent process. When using these modes, the parent process still owns ``Runtime``, ``orchestrator_actor``, ``job_actor``, and the normal ``simulation_unit_actor`` workers, but defers the actual model execution to a subprocess with its own actor system. In ``single_task_subprocess`` mode, the subprocess is stopped after each task, whereas ``persistent_subprocess`` mode reuses one subprocess per job worker for tasks with the same resolved subprocess path. When a subprocess task fails, including when its remote request exceeds ``RuntimeOptions::task_request_timeout_``, MIAOW preserves the subprocess logs and terminates the process; the worker launches a fresh subprocess for its next task.

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

When a worker finishes, the job actor returns it to the idle queue, creates its ``TaskRecord``, and passes all completed task records to the job controller. After appending any tasks returned by the controller's ``generate_new_tasks()`` method, the job actor dispatches pending tasks to available workers. For each dispatched task, the worker constructs a fresh ``SimulationUnit`` instance and calls its ``execute(...)`` method with a ``Task`` and ``ExecutionContext``. The ``SimulationUnit`` instance is destroyed after the task completes, although the worker may retain a persistent subprocess for reuse across tasks.

Task results that arrive while ``generate_new_tasks()`` is running wait in the job actor's mailbox and are processed after the current call returns. Each controller invocation therefore sees only the task records processed so far, and results from workers that finish concurrently may be processed in either order. Because shutdown requests and the job-timeout message use the same mailbox, a long-running or blocking controller delays their processing. Generated tasks receive consecutive ``TaskId`` values, undergo runtime-field assignment and duplicate-name validation, and enter the same greedy scheduling queue as the initial tasks.

If ``generate_new_tasks()`` or preparation of its returned tasks throws, the job actor stops dispatching pending tasks, requests worker shutdown, and does not call the controller again. Active tasks are allowed to finish before ``JobHandle::get()`` reports the job-level error. The job produces no final ``JobRecord`` or ``job_record.json``.

Limitations
-----------

- Although subprocess execution is currently supported using remote actors, MIAOW does not currently support distributed-memory parallelism across multiple nodes. This is a planned feature for future releases.
- Microsoft Windows is not currently supported, and there are no immediate plans to support it. MIAOW has been tested on Linux and macOS, and future releases will continue to support these platforms.

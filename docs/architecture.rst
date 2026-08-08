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
5. Each worker resolves ``Task::simulation_unit_id_``, constructs a new ``SimulationUnit``, executes the task, and returns a ``TaskResult`` to the job actor.
6. After each task execution returns a successful or failed ``TaskResult``, the ``job_actor`` creates its ``TaskRecord``, passes all completed task records to its controller, appends tasks returned by the controller's ``generate_new_tasks()`` method to its task vector, adds workers if needed, and returns to step 4. This continues until no tasks are waiting or running.
7. Completed ``TaskRecord`` values are sorted by ``TaskId`` and stored in one ``JobRecord``.
8. Runtime fulfils the caller's ``JobHandle`` future.

The following figure illustrates the task scheduling process managed by the ``job_actor`` in steps 4 through 6 of the above workflow, where the orange cat icons represent tasks:

.. image:: images/task_scheduling_tikz.png
   :alt: MIAOW task scheduling queue and worker dispatch
   :align: center
   :width: 67%

When the controller generates new tasks, the job actor counts entries in its task vector that do not yet have a corresponding completed task record and adds workers only if this count exceeds the existing worker count. Workers are not removed while the job is running, so the job retains the largest worker count that has been required at any point so far, capped by ``RuntimeOptions::max_workers_``, until the job completes or shuts down. Idle ``simulation_unit_actor`` workers do not occupy CAF scheduler threads, so retaining them does not reduce scheduler capacity. Persistent subprocesses remain alive and retain their process resources while their workers are idle, but MIAOW does not reserve dedicated CPU cores for them.

Simulation unit execution
-------------------------

``Task::simulation_unit_id_`` selects the complete execution implementation. ``SimulationUnitRegistry::add<SimulationUnitType>(...)`` registers a unit constructed and executed in the parent process, while ``SimulationUnitRegistry::add_subprocess(executable, lifetime)`` registers an internal ``SimulationUnit`` implementation that delegates each task to the supplied executable. Tasks contain no separate execution-location or subprocess-deployment fields.

Every ``simulation_unit_actor`` constructs the selected polymorphic ``SimulationUnit`` and calls ``execute(task)``. Ordinary (in-process) units registered using ``SimulationUnitRegistry::add<SimulationUnitType>(...)`` return an immediate ``TaskResult``. A subprocess unit returns a delegated CAF response while its actor-owned state starts or reuses the child, sends the serialized ``Task``, receives ``TaskResult``, and completes required cleanup without blocking a parent scheduler thread. The child executable implements the ``Task`` to ``TaskResult`` contract through ``run_subprocess_worker()`` and does not receive the parent registry.

``SubprocessLifetime::single_task`` starts one child for a task and stops and reaps it before delivering the result. ``SubprocessLifetime::persistent`` retains a child only after a successful task. The same idle ``simulation_unit_actor`` reuses that child only when the next task selects a persistent registration with exactly the same executable string and the child actor remains available. A failure, timeout, single-task registration, different executable, child termination, or worker shutdown prevents reuse. Failures and task timeouts preserve subprocess diagnostics and stop and reap the child before completing the delegated response.

Limitations
-----------

- Although subprocess execution is currently supported using remote actors, MIAOW does not currently support distributed-memory parallelism across multiple nodes. This is a planned feature for future releases.
- Microsoft Windows is not currently supported, and there are no immediate plans to support it. MIAOW has been tested on Linux and macOS, and future releases will continue to support these platforms.

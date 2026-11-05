Architecture
============

Orchestration model
-------------------

MIAOW organizes simulation workflows into jobs. A ``Job`` groups simulation units, tasks to execute on them, and a controller that can add work in response to results.

A **simulation unit** is the basic unit of computation orchestrated by MIAOW, such as a whole model or a spatial subdomain. A ``Task`` requests an operation on a particular unit and supplies its arguments. Each unit owns an executor: a C++ object implementing the ``TaskExecutor`` interface that carries out those operations. The executor can retain state between tasks.

``Runtime`` coordinates jobs through an actor hierarchy inspired by `SUMMA-Actors <https://github.com/uofs-simlab/Summa-Actors>`_ (`Klenk and Spiteri, 2024 <https://doi.org/10.1007/s10586-024-04318-5>`_). The orchestrator creates a job actor for each submitted job. Each job actor manages its ``Job`` object and creates actors to manage its ``SimulationUnit`` objects. The job actor dispatches tasks and calls the controller; the unit actors execute the tasks.

A unit using ``SubprocessTaskExecutor`` runs its task code in a separate process on the same host. Its unit actor and executor remain in the parent process alongside ``Runtime``. The hierarchy below includes this subprocess arrangement.

.. mermaid:: diagrams/orchestration.mmd
   :alt: MIAOW orchestration actor hierarchy
   :align: center

Job execution
-------------

A caller declares units, assigns tasks to them, and submits the job to ``Runtime``. The job actor then schedules those tasks and collects their results.

Simulation units
^^^^^^^^^^^^^^^^

``job.add_unit(feature_id, executor_id)`` declares a simulation unit and returns a ``SimulationUnitHandle`` that can be used only with that job. The feature ID labels the scientific unit; the executor ID selects its registered executor. Every task targets a unit in its owning job. For example, with an executor registered as ``executor`` that accepts an operation named ``increment`` and an integer argument, we can create a job with two units and submit tasks to them as follows:

.. code-block:: cpp

   miaow::Job job;
   const miaow::SimulationUnitHandle a = job.add_unit("A", executor);
   const miaow::SimulationUnitHandle b = job.add_unit("B", executor);
   job.add_task(a, "increment", 2);
   job.add_task(b, "increment", 1);
   job.add_task(a, "increment", 3);
   miaow::JobHandle result = runtime.submit_job(std::move(job));

.. _task-execution:

Task dispatch and executors
^^^^^^^^^^^^^^^^^^^^^^^^^^^

The job scans queued tasks in submission order and selects the first whose target unit is idle. It skips tasks for busy units so that tasks for other units can proceed. ``RuntimeOptions::max_workers_`` limits concurrent tasks in each job, independently of unit count. An idle unit does not reserve a thread.

When dispatching a unit's first task, the job creates its actor. The unit constructs one ``TaskExecutor`` before executing that task and reuses the executor for subsequent tasks. Operations on a unit run serially in the order they are received in the actor's mailbox.

A unit's ``TaskExecutorId`` selects an executor registered through ``TaskExecutorRegistry``. The registration determines where its task code runs:

.. list-table::
   :header-rows: 1
   :widths: 40 35 25

   * - Registration
     - Task executor
     - Task code runs in
   * - ``add<MyTaskExecutor>(...)``
     - ``MyTaskExecutor``
     - Parent process
   * - ``add_subprocess(executable)``
     - ``SubprocessTaskExecutor``
     - Child process

The executor object remains in the parent process in both cases. ``SubprocessTaskExecutor`` forwards tasks to a child process, as described under :ref:`subprocess-execution`.

Each BMI ``"run"`` task creates a fresh model and performs initialize, update, and finalize within that task. Individual BMI operations are not supported yet, so the BMI model does not persist across tasks. See :doc:`bmi` for the model lifecycle.

Job lifecycle
^^^^^^^^^^^^^

After submission, the job actor calls the controller's ``initialize()`` hook and begins dispatching tasks. Each executor returns a ``TaskResult``. The job actor adds the task's identity and timing to create a ``TaskRecord``, then passes new records to the controller according to its callback policy. The controller can use those results to add more tasks.

The sequence below shows one job with one unit. Open arrowheads are asynchronous CAF messages; dashed arrows are replies or termination notifications; filled arrowheads are local calls, actor creation, or labelled OS signals.

.. mermaid:: diagrams/job_execution.mmd
   :alt: Job submission, lazy unit creation, repeated tasks, and completion
   :align: center

When no tasks are queued or running and the controller adds no more tasks, the job actor tells each simulation-unit actor to shut down and waits for all of them to terminate. It then calls ``JobController::finalize()`` and returns a ``JobRecord`` through the caller's ``JobHandle`` future.

Job controllers
---------------

A ``JobController`` determines how the workflow proceeds after tasks return results. For example, an optimization controller can submit tasks for a batch of candidate parameters and use their results to choose the next batch. The job actor handles dispatch and unit lifetimes for the work the controller requests.

Lifecycle hooks
^^^^^^^^^^^^^^^

The following controller hooks run serially inside the job actor:

- ``initialize(Job&)`` runs once before initial dispatch, and may declare units and add tasks alongside caller-provided work. If the job has no tasks after initialization, it calls ``finalize()`` and returns an empty task-record list.

- ``callback(Job&, completed_tasks, failed_units)`` receives new task records and unit failures. The controller may append tasks or declare, remove, or replace units. The job waits for the callback to return before dispatching any tasks it added.

- ``finalize()`` runs once when creating the final job record, after units have shut down. It returns structured job output and also runs for empty jobs and job timeouts. Hooks should return promptly.

Calling ``Job::fail(reason)`` or throwing from a controller hook causes ``JobHandle::get()`` to throw instead of returning a ``JobRecord``. Runtime shutdown also produces no final job record.

Callback policies
^^^^^^^^^^^^^^^^^

Each ``JobController`` selects a ``TaskGenerationPolicy`` through its constructor:

- ``asynchronous`` (default): call ``callback()`` after a task succeeds or fails, or a unit fails.
- ``batch_synchronous``: call ``callback()`` after every task in the current batch has a success or failure record and no task from that batch is still running. Caller-provided tasks and tasks added during initialization form the first batch; tasks added by each callback form the next batch.

Both policies pass new task records in completion order through ``std::span<const TaskRecord>``. The controller must copy any records it needs after the callback returns. An asynchronous callback receives the newly completed task's record or the records for tasks affected by a unit failure; a batch callback receives all records from that batch.

.. _subprocess-execution:

Subprocess execution
--------------------

Subprocess execution gives each unit a separate address space. Models that use process-global state can therefore run concurrently without sharing that state. MIAOW can also stop a timed-out task by terminating its child process.

Registering an executable with ``TaskExecutorRegistry::add_subprocess()`` selects ``SubprocessTaskExecutor`` for units using that registration. Each such unit launches its own child for its first task and reuses it for later tasks. The executor sends each ``Task`` to a worker actor in the child, where a handler performs the computation and returns a ``TaskResult``. The unit actor forwards that result to the job actor.

``SubprocessState`` manages the child's launch, communication, and termination inside the unit actor. The unit actor can process messages while waiting for the child; it does not block a parent scheduler thread for the duration of the computation.

Registration and reuse
^^^^^^^^^^^^^^^^^^^^^^

Before the first task can execute, the parent needs the child worker's actor handle to send it messages. The child registers that handle with ``subprocess_registration_actor``, which routes it to the unit that launched the child. The unit then sends the waiting task. Later tasks reuse the same child and handler, invoked serially once per task.

.. mermaid:: diagrams/subprocess_registration.mmd
   :alt: Subprocess launch, worker registration, and first task response
   :align: center

.. _custom-subprocess-executables:

Custom subprocess executables
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

An executable supplies the computation that runs in the child. MIAOW provides a BMI worker (see :doc:`bmi`); a custom executable can handle other operations. In the parent program, register its path, declare a unit, and append a task:

.. code-block:: cpp

   miaow::TaskExecutorRegistry registry;
   const miaow::TaskExecutorId custom_executor_id =
       registry.add_subprocess("/path/to/my_worker");

   miaow::Job job;
   const miaow::SimulationUnitHandle unit = job.add_unit("custom", custom_executor_id);
   job.add_task(unit, "run", "input");

The executable links against MIAOW and supplies a synchronous callable accepting ``const Task&`` and returning ``TaskResult``. ``run_subprocess_worker()`` registers the worker with the parent and invokes this callable for each task it receives:

.. code-block:: cpp

   #include <miaow.hpp>

   miaow::TaskResult execute_task(const miaow::Task&) {
       return {miaow::TaskStatus::succeeded, "custom task completed"};
   }

   int main(int argc, char* argv[]) {
       return miaow::run_subprocess_worker(argc, argv, execute_task);
   }

Pass ``argc`` and ``argv`` unchanged to ``run_subprocess_worker()``; they carry parent-registration settings. Task data and custom configuration arrive through ``Task`` messages. The child does not receive the parent registry. An exception from the handler becomes a failed ``TaskResult``.

Cleanup
^^^^^^^

When the child returns a failed ``TaskResult``, the unit keeps the child available for later tasks and retains its diagnostic files. If child startup fails, the child terminates unexpectedly, or a task times out, the unit stops and reports ``UnitFailure``. Unit removal and job completion also stop the child.

The sequence shows shutdown of a ``SubprocessTaskExecutor`` unit that has launched a child. ``SubprocessState`` terminates the child and collects its exit status before the unit actor quits. A task timeout or unexpected child termination follows the same cleanup steps.

.. mermaid:: diagrams/subprocess_cleanup.mmd
   :alt: Child termination, non-blocking reaping, and unit termination
   :align: center

Failures and shutdown
---------------------

Task and unit failures
^^^^^^^^^^^^^^^^^^^^^^

An operation failure produces a failed ``TaskResult``; the unit remains available for later tasks. A ``UnitFailure`` means that the unit can no longer execute tasks. Failure to construct an executor, unexpected actor termination, and task timeout can cause a unit failure. For a ``SubprocessTaskExecutor``, failure to start the child or unexpected child termination also causes a unit failure.

``UnitFailure`` includes the unit handle and a message describing the cause. Its running and queued tasks each receive one failed record. An idle unit can fail without producing a task record.

The callback receives the affected records and the ``UnitFailure`` together. Asynchronous policy reports the failure without waiting for other tasks; batch policy waits until no tasks from the batch remain queued or running. The default controller fails the job. A custom controller may continue other units, remove the failed unit, or construct a replacement. MIAOW does not recreate failed units or retry their tasks automatically.

Timeouts
^^^^^^^^

A task timeout makes its unit unavailable and is reported to the controller as a ``UnitFailure``. At a job timeout, the job actor marks all unfinished tasks as failed and tells each simulation-unit actor to shut down. The controller cannot extend this deadline. Task records created because of the job timeout are not passed to ``callback()``; ``finalize()`` can return output based on earlier callbacks.

For a ``SubprocessTaskExecutor``, a task timeout terminates the child and waits for it to exit before reporting ``UnitFailure``. Tasks executing in the parent process cannot be interrupted by a task or job timeout. Their actors stop after ``execute()`` returns, so the job result may arrive after the configured deadline.

Runtime shutdown
^^^^^^^^^^^^^^^^

Destroying ``Runtime`` ends execution of its active jobs. Runtime tells the orchestrator to shut down; the orchestrator forwards that request to each job actor and exits. Each job actor tells its unit actors to shut down and waits for them to terminate. Actor-system destruction waits for the remaining actors. Jobs interrupted this way produce no final ``JobRecord``.

Termination arrows to the CAF actor system below represent lifecycle events. The system waits for all actors to terminate before its destruction completes.

.. mermaid:: diagrams/runtime_shutdown.mmd
   :alt: Concurrent orchestrator shutdown and job cleanup
   :align: center

Results and records
-------------------

Task records
^^^^^^^^^^^^

The job actor combines each ``TaskResult`` with the task's ID, operation, metadata, executor ID, and timing information to create a ``TaskRecord``. A ``TaskId`` identifies a task within its job. The final ``JobRecord`` contains these records sorted by ``TaskId``.

``TaskRecord::unit_`` identifies the task's target unit, including for tasks that never dispatch. Unit identity is independent of the concurrent-task limit. The saved ``task_executor_id`` identifies the executor registration used by that unit.

Operations on a unit share its feature label, with unique task IDs in artifact stems such as ``A_task_0`` and ``A_task_2``.

Task timing
^^^^^^^^^^^

``TaskRecord::dispatch_offset_from_job_start_`` stores the elapsed seconds from job start to dispatch, using the same monotonic clock as ``run_time_``. Dispatch precedes the unit actor beginning execution. A non-null offset identifies a dispatched task, independently of its scheduler thread or subprocess PID.

``TaskRecord::run_time_`` measures the interval from dispatch until the job actor records success or failure. It includes scheduling, communication, and any subprocess startup or cleanup that occurs before the result is recorded. Adding it to the dispatch offset gives the time relative to job start when that result was recorded.

Tasks that reach the job timeout before dispatch have a null dispatch offset and zero runtime. Dispatched tasks retain their offset and elapsed runtime up to the timeout. Unavailable offsets are ``std::nullopt`` (``null`` in JSON).

Structured output
^^^^^^^^^^^^^^^^^

``TaskResult::output_`` carries optional structured output as a ``caf::config_value``. It supports scalar values and nested lists and dictionaries, and remains available through the ``TaskRecord`` values passed to the controller and retained by the final ``JobRecord``. Controllers validate the structure and value types defined by their task executors. Large time series, grids, and model state belong in files referenced by ``TaskResult::artifact_paths_``.

``JobController::finalize()`` returns optional structured output in the same format, with null as the default. Runtime retains this value in ``JobRecord::output_``.

When ``RuntimeOptions::output_root_`` is configured, Runtime writes the completed ``JobRecord``, including task and job output, to ``job_record.json``. Finite double values retain enough digits to round-trip; non-finite numerical values are written as JSON ``null``.

Each completed record includes ``miaow_version``, captured when the library is built. This uses Git's description relative to a reachable version tag, such as ``v0.3.0-5-gabc1234-dirty``: the tag, number of additional commits, abbreviated commit hash, and ``-dirty`` for tracked edits. An exact clean tag reports only the tag; a history without a matching tag reports an abbreviated hash.

Builds without usable Git metadata retain the configured numeric version. Rebuilding refreshes the version without requiring a new CMake configuration.

.. note::

   CAF has a binary-deserialization limitation for null entries in ``caf::config_value`` lists and dictionaries, which also applies to structured task and job output. A null value for the entire output (``caf::config_value{}``) is supported.

Limitations
-----------

- Subprocess actors currently run on the same host. Distributed-memory parallelism across multiple nodes is planned.
- MIAOW has been tested on Linux and macOS. Microsoft Windows is not currently supported, and there are no immediate plans to support it.

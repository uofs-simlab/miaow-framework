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

``job.add_unit(feature_id, executor_id)`` declares a simulation unit and returns a zero-based ``SimulationUnitId``. Task and unit IDs are local to their job; callers must use them with the job that assigned them. IDs remain valid across job moves and additions, and unit IDs are never reused within a job. The feature ID labels the scientific unit; the executor ID selects its registered executor.

Unit labels must be nonempty, contain no path separators, and be unique among units accepting work. ``add_unit()`` checks these rules before admitting the unit.

For example, with an executor registered as ``executor`` that accepts an operation named ``increment`` and an integer argument, we can create a job with two units and submit tasks to them as follows:

.. code-block:: cpp

   miaow::Job job;
   const miaow::SimulationUnitId a = job.add_unit("A", executor);
   const miaow::SimulationUnitId b = job.add_unit("B", executor);
   job.add_task(a, "increment", 2);
   job.add_task(b, "increment", 1);
   job.add_task(a, "increment", 3);
   miaow::JobHandle result = runtime.submit_job(std::move(job));

.. _task-execution:

Task dispatch and executors
^^^^^^^^^^^^^^^^^^^^^^^^^^^

The job scans queued tasks in submission order and selects the first whose prerequisites have succeeded and whose target unit is idle. A ready task can overtake an earlier blocked task, including on the same unit. Submission order gives ready tasks priority; required ordering must be expressed through explicit dependencies. ``RuntimeOptions::max_workers()`` limits concurrent tasks in each job, independently of unit count. An idle unit does not reserve a thread.

Set the worker limit through the ``RuntimeOptions`` constructor or ``set_max_workers()``; both reject zero with ``std::invalid_argument``. Read it through ``max_workers()``. The default limit is one.

The worker limit does not limit the number of resident executors or models. Activated units retain their executors while idle.

When dispatching a unit's first task, the job creates its actor. The unit constructs one ``TaskExecutor`` before executing that task and reuses the executor for subsequent tasks. Declaring a unit does not create its actor or initialize a BMI model. BMI initialization occurs when the executor handles an ``initialize`` or ``run`` task. The job allows at most one active task per unit, and operations on that unit execute serially.

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

The unit's executor object remains in the parent process in both cases. ``SubprocessTaskExecutor`` forwards tasks to a child process, as described under :ref:`subprocess-execution`.

Each BMI ``"run"`` task creates a fresh model and performs initialize, update, and finalize within that task. Static and dynamic BMI executors also support individual operations on a model retained between tasks, using their common ``BmiTaskExecutor`` base. The bundled BMI subprocess worker retains a ``DynamicBmiTaskExecutor`` in the child and supports the same operations. See :doc:`bmi` for the model lifecycle.

Task dependencies and output references
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

``Job::add_task()`` returns a zero-based ``TaskId`` that indexes its definition, result, and timing. Controller callbacks receive these same IDs. ``JobId`` identifies a submitted job within its runtime.

``Task`` and ``job.add_task()`` accept no dependencies, one ``TaskId``, or a ``std::vector<TaskId>`` before the operation name:

.. code-block:: cpp

   // No dependencies
   const miaow::TaskId source_update = job.add_task(source, "update");

   // One dependency: the source update
   const miaow::TaskId source_get_value =
       job.add_task(source, source_update, "get_value", source_variable);

   // Two dependencies: the preceding receiver task and the source read
   job.add_task(receiver,
                std::vector<miaow::TaskId>{previous_receiver_task, source_get_value},
                "set_value", receiver_variable, miaow::output_of(source_get_value));

Prerequisites must already have been added to the job and must succeed before the dependent task dispatches. ``Job::add_task()`` rejects IDs that do not name earlier tasks with ``std::invalid_argument``. Requiring prerequisites to be added first prevents cycles. IDs carry no job identity, so callers are responsible for keeping references within their owning job.

Sharing a unit adds no implicit dependencies. For example, if an earlier ``update`` task is waiting on another unit, a later ``get_current_time`` task without dependencies can run first on the same model. To query the time after that update, declare the update's task ID as the query's prerequisite. Each unit still executes only one task at a time.

``output_of(task_id)`` marks an operation argument to receive the producer's complete ``TaskResult::output_`` at dispatch. It returns a ``TaskOutputReference`` that the task constructor records as a producer ID and argument position. Plain integer arguments are literal values. The producer must also appear in the dependency list; ``add_task()`` rejects undeclared producers and invalid argument positions.

The job copies referenced outputs into task arguments before dispatch. Executors and subprocess workers receive resolved values. Outputs remain available for consumers added later in the job.

A failed prerequisite causes its dependents to fail without dispatch, propagating through the branch. Controllers receive these completion IDs under their callback policy and can add recovery or finalization tasks without the failed prerequisite. A blocked finalizer has not attempted model cleanup.

Tasks may be added before submission or from ``initialize()`` and ``callback()``. Arguments and dependencies are fixed when each task is added; forward references and graph rewiring are unsupported.

.. mermaid:: diagrams/task_dependencies.mmd
   :name: task-dispatch-diagram
   :alt: Job actor event handling, dependency checks, task dispatch, and completion
   :align: center

Job lifecycle
^^^^^^^^^^^^^

After submission, the job actor calls ``initialize()``, prepares accepted tasks, and dispatches ready work. It stores each returned ``TaskResult`` by task ID and delivers completion IDs under the controller's callback policy.

The sequence below shows one job with one unit. Open arrowheads are asynchronous CAF messages; dashed arrows are replies or termination notifications; filled arrowheads are local calls, actor creation, or labelled OS signals:

.. mermaid:: diagrams/job_execution.mmd
   :alt: Job submission, lazy unit creation, repeated tasks, and completion
   :align: center

When no tasks are queued or running and the controller adds no more tasks, the job actor tells each simulation-unit actor to shut down and waits for all of them to terminate. It then calls ``JobController::finalize()`` and returns a ``JobRecord`` through the caller's ``JobHandle`` future.

Job controllers
---------------

Lifecycle hooks
^^^^^^^^^^^^^^^

The following controller hooks run serially inside the job actor:

- ``initialize(Job&)`` runs once before initial dispatch, and may declare units and add tasks alongside caller-provided work. If the job has no tasks after initialization, it calls ``finalize()`` and returns empty result and timing vectors.

- ``callback(Job&, completed_task_ids, failed_units)`` receives new completion IDs and unit failures. The controller may append tasks or declare, remove, or replace units. The job waits for the callback to return before dispatching any tasks it added.

- ``finalize()`` runs once when creating the final job record, after units have shut down. It returns structured job output and also runs for empty jobs and job timeouts.

Blocking a hook delays dispatch, result handling, and job timeouts.

Calling ``Job::fail(reason)`` or throwing from a controller hook causes ``JobHandle::get()`` to throw instead of returning a ``JobRecord``. Runtime shutdown also produces no final job record.

Callback policies
^^^^^^^^^^^^^^^^^

Each ``JobController`` selects a ``TaskGenerationPolicy`` through its constructor:

- ``asynchronous`` (default): call ``callback()`` after a task succeeds or fails, or a unit fails.
- ``batch_synchronous``: call ``callback()`` after every task in the current batch has a success or failure result and no task from that batch is still running. Caller-provided tasks and tasks added during initialization form the first batch; tasks added by each callback form the next batch.

Both policies deliver each completion ID once, in completion order, through ``std::span<const TaskId> completed_task_ids``. Unit loss can deliver several IDs in one callback. The span is borrowed for the call.

Lookups work as soon as tasks and units are added. Task additions can invalidate definition and result references; unit additions can invalidate label references. Whole-job timeouts finalize unfinished results without invoking another callback.

.. _subprocess-execution:

Subprocess execution
--------------------

Subprocess execution gives each unit a separate address space. Models that use process-global state can therefore run concurrently without sharing that state. MIAOW can also stop a timed-out task by terminating its child process.

Registering an executable with ``TaskExecutorRegistry::add_subprocess()`` selects ``SubprocessTaskExecutor`` for units using that registration. Each such unit launches its own child for its first task and reuses it for later tasks. The executor sends each ``Task`` to a worker actor in the child, where a handler performs the computation and returns a ``TaskResult``. The unit actor forwards that result to the job actor.

Each child writes stdout and stderr to ``unit_<unit_id>_stdout.log`` and ``unit_<unit_id>_stderr.log``, using the job's unit ID. The same files capture output across all tasks executed by that child.

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
   const miaow::SimulationUnitId unit = job.add_unit("custom", custom_executor_id);
   job.add_task(unit, "run", "input");

The executable links against MIAOW and supplies a callable accepting ``const Task&`` and returning ``TaskResult`` or ``caf::result<TaskResult>``. Plain ``TaskResult`` callbacks convert implicitly to the worker's result type. ``run_subprocess_worker()`` registers the worker with the parent and invokes this callable serially for each task it receives:

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

``SubprocessState`` requests orderly CAF worker exit, allowing the active synchronous handler to return before ``run_subprocess_worker()`` returns and child-owned resources are destroyed. ``RuntimeOptions::subprocess_shutdown_grace_period_`` defaults to 5 seconds and covers the active call, resource cleanup, and process exit. Set it to a non-positive duration for immediate forced termination. A child without a registered worker is forcibly terminated because it cannot receive the exit request.

The sequence shows shutdown of a ``SubprocessTaskExecutor`` unit that has launched a child. ``SubprocessState`` polls for process exit without blocking a scheduler thread. If the fixed grace deadline expires, it sends ``SIGKILL`` and continues polling. Repeated shutdown requests do not extend the deadline. The unit actor quits only after the child is reaped. A task timeout or unexpected child termination follows the same cleanup steps.

.. mermaid:: diagrams/subprocess_cleanup.mmd
   :alt: Orderly child shutdown, forced termination after a grace period, and non-blocking reaping
   :align: center

Failures and shutdown
---------------------

Task and unit failures
^^^^^^^^^^^^^^^^^^^^^^

An operation failure produces a failed ``TaskResult``; the unit remains available for later tasks. A ``UnitFailure`` means that the unit can no longer execute tasks. Failure to construct an executor, unexpected actor termination, and task timeout can cause a unit failure. For a ``SubprocessTaskExecutor``, failure to start the child or unexpected child termination also causes a unit failure.

``UnitFailure`` includes ``unit_id_`` and a message describing the cause. Its running and queued tasks each receive one failed record. An idle unit can fail without producing a task record.

The callback receives the affected records and the ``UnitFailure`` together. Asynchronous policy reports the failure without waiting for other tasks; batch policy waits until no tasks from the batch remain queued or running. The default controller fails the job. A custom controller may continue other units, remove the failed unit, or construct a replacement. MIAOW does not recreate failed units or retry their tasks automatically. Replacement BMI units start without an initialized model.

Timeouts
^^^^^^^^

A task timeout makes its unit unavailable and is reported to the controller as a ``UnitFailure``. At a job timeout, the job actor marks all unfinished tasks as failed and tells each simulation-unit actor to shut down. The controller cannot extend this deadline. Task records created because of the job timeout are not passed to ``callback()``; ``finalize()`` can return output based on earlier callbacks.

For a ``SubprocessTaskExecutor``, a task timeout starts child shutdown and waits for process exit and reaping before reporting ``UnitFailure``. Finishing during the shutdown grace period does not change the task's failure. Job results can arrive after their configured deadline while subprocess cleanup completes. Tasks executing in the parent process cannot be interrupted by a task or job timeout. Their actors stop after ``execute()`` returns, so their cleanup has no enforced time limit.

Runtime shutdown
^^^^^^^^^^^^^^^^

Destroying ``Runtime`` ends execution of its active jobs. Runtime tells the orchestrator to shut down; the orchestrator forwards that request to each job actor and exits. Each job actor tells its unit actors to shut down and waits for them to terminate. Actor-system destruction waits for the remaining actors. Jobs interrupted this way produce no final ``JobRecord``.

Termination arrows to the CAF actor system below represent lifecycle events. The system waits for all actors to terminate before its destruction completes.

.. mermaid:: diagrams/runtime_shutdown.mmd
   :alt: Concurrent orchestrator shutdown and job cleanup
   :align: center

Results and records
-------------------

Indexed task results
^^^^^^^^^^^^^^^^^^^^

A ``TaskId`` indexes a task definition and its optional result. An empty result represents unfinished work. The first accepted terminal result is retained; late responses cannot replace it. The final ``JobRecord::task_results_`` contains a result for every accepted task, in task-ID order.

``Job::task(task_id)``, ``Job::result(task_id)``, and ``Job::timing(task_id)`` return the definition, optional result, and timing offsets.

Invalid task IDs in these lookups throw ``std::out_of_range``. Borrowed task and result references may be invalidated by task additions.

``Job::task(task_id).unit_id_`` identifies the target, including when the task never dispatches. ``Job::unit_label(unit_id)`` returns its label after removal. Replacements may reuse a label but receive a new unit ID. Removal rejects new work immediately, drains accepted tasks, and then releases execution resources.

``Task::task_output_dir_`` is the job's ``results`` directory, or empty when file output is disabled. It is populated only in dispatched requests.

Task timing
^^^^^^^^^^^

``TaskTiming`` stores dispatch and first terminal-result offsets from job start in signed 64-bit nanoseconds, using ``std::chrono::steady_clock``. Timing collection is always enabled. ``Job::timing(task_id)`` returns a ``TaskTiming`` value; ``JobRecord::task_timings_`` indexes timings by task ID. An offset of ``-1`` denotes an event that has not occurred. Nanoseconds specify storage units, not measurement accuracy.

Subtract dispatch from completion only when both offsets are available. This duration includes scheduling, communication, and subprocess startup or cleanup before the result is recorded. A task that fails before dispatch has a completion offset but no duration. A timeout can be recorded before an in-process worker finishes cleanup; that worker continues to occupy capacity until termination.

The profile exports offsets as integer nanoseconds, with JSON ``null`` for unavailable events. Overall job runtime is reported in seconds and includes initialization, execution, unit cleanup, and controller finalization. Its measurement ends before file export.

Structured output
^^^^^^^^^^^^^^^^^

``TaskResult::output_`` carries optional structured output as a ``caf::config_value``. It supports scalar values and nested lists and dictionaries, and remains available through ``Job::result(task_id)`` during execution and ``JobRecord::task_results_`` after completion.

``JobController::finalize()`` supplies ``JobRecord::output_``, which is null by default.

When ``RuntimeOptions::output_root_`` is configured, the job actor writes one ``job_record.json`` during finalization. An empty output root disables report files while retaining timing collection. A write failure propagates through ``JobHandle::get()``. Finite double values retain enough digits to round-trip; non-finite numerical values are written as JSON ``null``.

The file contains runtime and job IDs, the library version, a summary of task counts and job runtime, controller ``output``, sparse ``failures`` and ``artifacts`` arrays, and a ``profile``. Raw task outputs remain in ``JobRecord::task_results_``. Controller output uses plain JSON values.

``miaow::to_json()`` in ``miaow_io.hpp`` converts a ``caf::config_value`` to ``nlohmann::json``. Lists and dictionaries become JSON arrays and objects. Durations become strings of signed nanoseconds followed by ``ns``; URIs become strings.

``profile.units`` lists each unit's ID, label, and executor ID, including removed units. ``profile.operations`` lists distinct operation names in first-occurrence order. ``profile.columns`` names the fields in each ``profile.tasks`` row: task ID, unit ID, operation ID, status, dispatch offset, and completion offset. Rows follow task-ID order. Jobs with no tasks retain the column schema and any declared units.

Each completed record includes ``miaow_version``, captured when the library is built. This uses Git's description relative to a reachable version tag, such as ``v0.3.0-5-gabc1234-dirty``: the tag, number of additional commits, abbreviated commit hash, and ``-dirty`` for tracked edits. An exact clean tag reports only the tag; a history without a matching tag reports an abbreviated hash.

Builds without usable Git metadata retain the configured numeric version. Rebuilding refreshes the version without requiring a new CMake configuration.

.. note::

   CAF has a binary-deserialization limitation for null entries in ``caf::config_value`` lists and dictionaries, which also applies to structured task and job output. A null value for the entire output (``caf::config_value{}``) is supported.

Limitations
-----------

- Subprocess actors currently run on the same host. Distributed-memory parallelism across multiple nodes is planned.
- MIAOW has been tested on Linux and macOS. Microsoft Windows is not currently supported, and there are no immediate plans to support it.

BMI integration
===============

MIAOW runs models implementing the Basic Model Interface (BMI) either in the parent process or in a unit-owned subprocess. See :doc:`examples` for complete programs and :doc:`api` for configuration fields and function signatures.

BMI task lifecycle
------------------

``make_bmi_task(unit, config)`` creates a task targeting the given unit. It stores ``"run"`` in ``Task::operation_`` and the model configuration in ``Task::args_``. Add the task to its owning job with ``job.add_task(...)``. Each execution performs one complete model run:

1. Construct or load a fresh BMI model and call ``Initialize(init_config_)``.
2. Call ``Update()`` until the model reaches its end time.
3. Call ``Finalize()`` and release the model before reporting success.

Models manage their own forcing and scientific output. If a BMI call fails, the runner attempts to finalize a successfully initialized model before releasing it. Results report the number of completed updates.

A unit retains its executor across operations. Each ``run`` operation constructs a fresh model; model state does not persist between complete runs. Static BMI executors also support individual operations on a retained model, as described below.

Static BMI model
----------------

Register the concrete C++ model type, declare a unit, and add its run operation:

.. code-block:: cpp

   miaow::TaskExecutorRegistry registry;
   const miaow::TaskExecutorId heat =
       registry.add<miaow::StaticBmiTaskExecutor<BmiHeat>>();
   miaow::Runtime runtime(registry);
   miaow::Job job;
   const miaow::SimulationUnitHandle unit = job.add_unit("heat", heat);
   const miaow::StaticBmiConfig config{.init_config_ = heat_config};
   job.add_task(miaow::make_bmi_task(unit, config));

   // Submit the job
   miaow::JobHandle job_handle = runtime.submit_job(std::move(job));

   // Wait for completion and retrieve the job record
   const miaow::JobRecord record = job_handle.get();

For concurrent independent runs, declare one unit per configuration using the same executor registration. Repeated tasks targeting one unit execute sequentially and reuse its executor.

Individual static BMI operations
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

``StaticBmiTaskExecutor`` accepts ``initialize`` with a configuration-path string, followed by argument-free ``update``, ``get_current_time``, and ``finalize`` tasks. Each task invokes one BMI call. ``get_current_time`` returns a scalar double in ``TaskResult::output_``, in the model's native time units. The executor retains the initialized model until finalization and can initialize a fresh model afterward. ``run`` and a second ``initialize`` are rejected while a model is initialized.

The heat example demonstrates controller-generated operations with ``./miaow_heat --step`` from ``examples/01_heat``. It initializes one model, queries its current time, performs two updates with a query after each, and explicitly finalizes. The default invocation still performs a complete run.

BMI call exceptions produce failed task results. After an update or query throws, the retained model accepts only finalization. The executor releases the model after a finalization attempt, including a failed attempt. Controllers should explicitly submit ``finalize`` to receive its result; executor destruction attempts finalization if an initialized model remains, but cannot report those cleanup errors as task results.

Dynamic BMI plugin
------------------

Register ``DynamicBmiTaskExecutor`` for in-process plugin execution, or register the bundled executable for subprocess isolation. The configuration selects a C++ or Fortran plugin and its initialization file:

.. code-block:: cpp

   miaow::TaskExecutorRegistry registry;
   const miaow::TaskExecutorId executor =
       registry.add_subprocess(miaow::bmi_subprocess_executable_path());
   miaow::Runtime runtime(registry);
   miaow::Job job;
   const miaow::SimulationUnitHandle unit = job.add_unit("model", executor);
   const miaow::DynamicBmiConfig config{
       .backend_ = miaow::DynamicBmiBackend::fortran,
       .plugin_path_ = plugin_path,
       .init_config_ = init_config,
   };
   job.add_task(miaow::make_bmi_task(unit, config));

   // Submit the job
   miaow::JobHandle job_handle = runtime.submit_job(std::move(job));

   // Wait for completion and retrieve the job record
   const miaow::JobRecord record = job_handle.get();

A subprocess-backed unit keeps its child alive between operations until removal or job completion. Each BMI operation still creates and finalizes its own model. The Julia/Trixi example places one simulation in a dedicated unit subprocess.

Custom executables can implement the same :ref:`task contract <custom-subprocess-executables>`.

Dynamic C++ plugins must export the ``bmi_model_create()`` and ``bmi_model_destroy(...)`` symbols as C linkage. Dynamic Fortran plugins must export ``register_bmi(void**)`` for the linked `NOAA-OWP iso_c_fortran_bmi bridge <https://github.com/NOAA-OWP/iso_c_fortran_bmi>`_. See ``examples/03_dynamic_bmi`` for examples of concrete C++ and Fortran plugin workflows.

Model timekeeping
-----------------

``time_step_tolerance_seconds_`` defines the absolute tolerance for the final-time comparison, with a default of ``1e-4`` seconds. It must be finite, non-negative, and less than half the model time step. MIAOW converts the tolerance to the units reported by ``GetTimeUnits()``, supporting seconds, minutes, hours, and days with common abbreviations and singular or plural names.

The runner calls ``Update()`` repeatedly and fails the task if an update passes the end time beyond this tolerance. A model already at its end time receives initialization and finalization with no updates.

Diagnostics and artifacts
-------------------------

When ``RuntimeOptions::output_root_`` or CLI ``--output_root`` is set, MIAOW writes a ``job_record.json`` under ``<output_root>/run_<run_id>/job_<job_submission_id>/``. Records include unit and executor identifiers, operations, status, messages, metadata, structured output, timings, and artifact paths. Subprocess stdout and stderr logs may also be retained there.

Scientific output files and their locations are controlled by the model's initialization configuration. BMI tasks produce no additional scientific output or resolved configuration files.

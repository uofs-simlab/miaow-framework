BMI integration
===============

MIAOW runs models implementing the Basic Model Interface (BMI) either in the parent process or in a unit-owned subprocess. See :doc:`examples` for complete programs and :doc:`api` for configuration fields and function signatures.

BMI task lifecycle
------------------

``job.add_task(unit, "run", config)`` adds a complete-run task targeting the given unit. The configuration is a ``caf::config_value::dictionary``, stored as one argument in ``Task::args_``. Standalone tasks can be constructed with ``Task{unit, "run", config}``. Each execution performs one complete model run:

1. Construct or load a fresh BMI model and call ``Initialize(init_config)``.
2. Call ``Update()`` until the model reaches its end time.
3. Call ``Finalize()`` and release the model before reporting success.

Models manage their own forcing and scientific output. If a BMI call fails, the runner attempts to finalize a successfully initialized model before releasing it. Results report the number of completed updates.

A unit retains its executor across operations. Each ``run`` operation constructs a fresh model; model state does not persist between complete runs. Static and dynamic BMI executors, including the bundled subprocess worker, also support individual operations on a retained model, as described below.

Configuration dictionaries use these fields. Fields with defaults may be omitted; supplied values must have the expected type and satisfy the model's requirements.

.. list-table::
   :header-rows: 1
   :widths: 30 25 15 30

   * - Field
     - Used by
     - Default
     - Value
   * - ``init_config``
     - ``initialize`` and ``run``
     - Empty string
     - Model initialization-file path string
   * - ``time_step_tolerance_seconds``
     - ``run``
     - ``1e-4``
     - Absolute clock tolerance as a double, in seconds
   * - ``backend``
     - Dynamic ``initialize`` and ``run``
     - ``"cpp"``
     - ``"cpp"`` or ``"fortran"``
   * - ``plugin_path``
     - Dynamic ``initialize`` and ``run``
     - Required
     - Plugin library path string

When ``init_config`` is omitted, the model receives an empty initialization path. Models that require an initialization file must still be given its path. Dynamic models always require ``plugin_path``.

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
   caf::config_value::dictionary config;
   config.insert("init_config", heat_config);
   job.add_task(unit, "run", config);

   // Submit the job
   miaow::JobHandle job_handle = runtime.submit_job(std::move(job));

   // Wait for completion and retrieve the job record
   const miaow::JobRecord record = job_handle.get();

For concurrent independent runs, declare one unit per configuration using the same executor registration. Repeated tasks targeting one unit execute sequentially and reuse its executor.

Individual BMI operations
^^^^^^^^^^^^^^^^^^^^^^^^^

``StaticBmiTaskExecutor`` and ``DynamicBmiTaskExecutor`` inherit their operation handling and model ownership from ``BmiTaskExecutor``. Use ``job.add_task(unit, "initialize", config)`` with a configuration dictionary to initialize a retained model, followed by argument-free ``job.add_task(unit, "update")``, ``get_current_time``, and ``finalize`` tasks. The same dictionary can be used for initialization or a complete run when it includes the required fields. Direct static initialization with ``job.add_task(unit, "initialize", path)`` is also supported.

The bundled subprocess worker retains one ``DynamicBmiTaskExecutor`` in each unit's child process and supports the same operations and task payloads.

Each task invokes one BMI call. ``get_current_time`` returns a scalar double in ``TaskResult::output_``, in the model's native time units. The executor retains the initialized model until finalization and can initialize a fresh model afterward. Dynamic models retain their backend object and loaded plugin for that lifetime. ``run`` and a second ``initialize`` are rejected while a model is initialized. Clock tolerance applies only to complete runs.

The ``09_bmi_operations`` example demonstrates controller-generated operations with ``./miaow_bmi_operations`` from ``examples/09_bmi_operations``, using the heat model and configuration from ``01_heat``. It initializes one model, queries its current time, performs two updates with a query after each, and explicitly finalizes. The ``01_heat`` example performs a complete run.

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
   caf::config_value::dictionary config;
   config.insert("backend", "fortran");
   config.insert("plugin_path", plugin_path);
   config.insert("init_config", init_config);
   job.add_task(unit, "run", config);

   // Submit the job
   miaow::JobHandle job_handle = runtime.submit_job(std::move(job));

   // Wait for completion and retrieve the job record
   const miaow::JobRecord record = job_handle.get();

A subprocess-backed unit keeps its child and dynamic executor alive between operations until removal or job completion. Individual operations retain an initialized model and its plugin; complete ``run`` tasks each create and finalize a fresh model. The Julia/Trixi example places one simulation in a dedicated unit subprocess.

Schedule ``finalize`` before normal unit removal or job completion to receive the model's cleanup result. Subprocess shutdown forcibly terminates the child, including on task timeout, so executor destruction cannot guarantee finalization during shutdown.

Custom executables can implement the same :ref:`task contract <custom-subprocess-executables>`.

Dynamic C++ plugins must export the ``bmi_model_create()`` and ``bmi_model_destroy(...)`` symbols as C linkage. Dynamic Fortran plugins must export ``register_bmi(void**)`` for the linked `NOAA-OWP iso_c_fortran_bmi bridge <https://github.com/NOAA-OWP/iso_c_fortran_bmi>`_. See ``examples/03_dynamic_bmi`` for examples of concrete C++ and Fortran plugin workflows.

Model timekeeping
-----------------

``time_step_tolerance_seconds`` defines the absolute tolerance for the final-time comparison. When omitted, it uses ``miaow::default_bmi_time_step_tolerance_seconds`` (``1e-4`` seconds). An explicit value overrides the default. It must be finite, non-negative, and less than half the model time step. MIAOW converts the tolerance to the units reported by ``GetTimeUnits()``, supporting seconds, minutes, hours, and days with common abbreviations and singular or plural names.

The runner calls ``Update()`` repeatedly and fails the task if an update passes the end time beyond this tolerance. A model already at its end time receives initialization and finalization with no updates.

Diagnostics and artifacts
-------------------------

When ``RuntimeOptions::output_root_`` or CLI ``--output_root`` is set, MIAOW writes a ``job_record.json`` under ``<output_root>/run_<run_id>/job_<job_submission_id>/``. Records include unit and executor identifiers, operations, status, messages, metadata, structured output, timings, and artifact paths. Subprocess stdout and stderr logs may also be retained there.

Scientific output files and their locations are controlled by the model's initialization configuration. BMI tasks produce no additional scientific output or resolved configuration files.

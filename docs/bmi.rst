BMI integration
===============

This page describes the Basic Model Interface (BMI) integration paths and how to configure tasks for different BMI model execution patterns; see the :doc:`examples` section for concrete workflows and the :doc:`api` for constructor, member, and function details.

BMI task lifecycle
------------------

Build individual tasks with ``make_bmi_task(...)`` and task sets with ``make_bmi_tasks(...)``. Each task carries one flat model configuration in ``Task::args_`` and owns one complete model run:

1. Construct or load a fresh BMI model and call ``Initialize(init_config_)``.
2. Call ``Update()`` until the model reaches its end time.
3. Call ``Finalize()`` and release the model before reporting success.

Models manage their own forcing and scientific output. On failure, MIAOW attempts to finalize a successfully initialized model before releasing it. Each task result reports the number of completed updates.

Task configuration options
--------------------------

The setup pattern depends on whether the BMI model implementation comes from a statically typed C++ model or a dynamic plugin.

BMI task configuration
~~~~~~~~~~~~~~~~~~~~~~

The ``SimulationUnitId`` passed to ``make_bmi_task(...)`` or ``make_bmi_tasks(...)`` selects the ``SimulationUnit`` subclass that executes the BMI task. An ID returned by ``SimulationUnitRegistry::add<StaticBmiSimulationUnit<BmiModelType>>()`` selects static execution in the parent process, while an ID returned by ``SimulationUnitRegistry::add<DynamicBmiSimulationUnit>()`` selects dynamic plugin execution in the parent process. An ID returned by ``SimulationUnitRegistry::add_subprocess(bmi_subprocess_executable_path(), lifetime)`` selects dynamic plugin execution in a child subprocess, which is useful for non-thread-safe models. We describe the different options below.

Static BMI model
~~~~~~~~~~~~~~~~

The static model setup applies when the concrete BMI model type is known when compiling the executable that uses MIAOW. Tasks which specify a ``SimulationUnitId`` returned by ``SimulationUnitRegistry::add<StaticBmiSimulationUnit<YourModel>>()`` construct one new model object per task and execute the BMI lifecycle in the parent process. The following example registers the model type and constructs one task.

.. code-block:: cpp

   miaow::SimulationUnitRegistry registry;
   const miaow::SimulationUnitId heat =
       registry.add<miaow::StaticBmiSimulationUnit<BmiHeat>>();

   const miaow::StaticBmiConfig config{
       .init_config_ = heat_config,
   };
   const miaow::Task task = miaow::make_bmi_task(heat, config);

The same registered simulation unit ID can be applied to several configurations. The following example constructs one ``Task`` for each ``StaticBmiConfig`` in a vector, using ``first_heat_config`` and ``second_heat_config`` as their initialization configurations. The resulting tasks share the registered simulation unit ID ``heat`` and execute in the parent process:

.. code-block:: cpp

   const std::vector<miaow::StaticBmiConfig> configs{
       {.init_config_ = first_heat_config},
       {.init_config_ = second_heat_config},
   };
   const std::vector<miaow::Task> tasks =
       miaow::make_bmi_tasks(heat, configs);

These tasks are then submitted to the MIAOW runtime to be dispatched by a ``job_actor`` and executed concurrently by instances of the registered ``StaticBmiSimulationUnit<BmiHeat>`` simulation unit. The ``submit_job(...)`` call returns a ``JobHandle`` immediately, and ``JobHandle::get()`` waits for the completed ``JobRecord``:

.. code-block:: cpp

   miaow::Runtime runtime(registry);
   miaow::JobHandle job = runtime.submit_job(tasks);
   const miaow::JobRecord job_record = job.get();


Dynamic BMI plugin
~~~~~~~~~~~~~~~~~~

The dynamic plugin setup applies when the model is loaded from a C++ or Fortran plugin library. ``DynamicBmiConfig`` contains the backend, plugin path, initialization path, and clock tolerance for one task. The following example registers parent-process execution and configures one dynamic Fortran plugin task:

.. code-block:: cpp

   miaow::SimulationUnitRegistry registry;
   const miaow::SimulationUnitId dynamic_bmi =
       registry.add<miaow::DynamicBmiSimulationUnit>();

   const miaow::DynamicBmiConfig config{
       .backend_ = miaow::DynamicBmiBackend::fortran,
       .plugin_path_ = plugin_path,
       .init_config_ = init_config,
   };
   const miaow::Task task = miaow::make_bmi_task(dynamic_bmi, config);

Dynamic BMI plugins can be executed in child subprocesses by registering a ``SimulationUnitId`` through the generic ``add_subprocess(executable, lifetime)`` method rather than through ``add<DynamicBmiSimulationUnit>()``. The following example registers a single-task subprocess and constructs one dynamic BMI task to be executed in the child:

.. code-block:: cpp

   const miaow::SimulationUnitId dynamic_bmi_subprocess =
       registry.add_subprocess(
           miaow::bmi_subprocess_executable_path(),
           miaow::SubprocessLifetime::single_task);
   const miaow::Task subprocess_task =
       miaow::make_bmi_task(dynamic_bmi_subprocess, config);

In the above example, ``bmi_subprocess_executable_path()`` resolves the bundled worker that loads and runs one model in the subprocess. As described in :ref:`simulation-unit-execution`, ``SubprocessLifetime::single_task`` starts one child for each task, whereas ``SubprocessLifetime::persistent`` retains the child after a successful task. Every task constructs a fresh model, including tasks that reuse a persistent child process. The same job worker reuses the child only when its next task selects a persistent registration with the same executable and the child actor remains available. Another executable can be registered when it implements the public ``Task`` to ``TaskResult`` contract described in :ref:`custom-subprocess-executables`.

Dynamic C++ plugins must export the ``bmi_model_create()`` and ``bmi_model_destroy(...)`` symbols as C linkage. Dynamic Fortran plugins must export ``register_bmi(void**)`` for the linked `NOAA-OWP iso_c_fortran_bmi bridge <https://github.com/NOAA-OWP/iso_c_fortran_bmi>`_. See ``examples/03_dynamic_bmi`` for examples of concrete C++ and Fortran plugin workflows.

Model time
----------

``time_step_tolerance_seconds_`` defines the absolute tolerance for the final-time comparison, with a default of ``1e-4`` seconds. It must be finite, non-negative, and less than half the model timestep. MIAOW converts the tolerance to the units reported by ``GetTimeUnits()``, supporting seconds, minutes, hours, and days with common abbreviations and singular or plural names.

The runner calls ``Update()`` repeatedly and fails the task if an update passes the end time beyond this tolerance. A model already at its end time receives initialization and finalization with no updates.

Diagnostics and artifacts
-------------------------

When ``RuntimeOptions::output_root_`` or CLI ``--output_root`` is set, MIAOW writes a ``job_record.json`` under ``<output_root>/run_<run_id>/job_<job_id>/``. Records include task configurations, status, messages, metadata, structured results, timings, and artifact paths. Subprocess stdout and stderr logs may also be retained there.

Scientific output files and their locations are controlled by the model's initialization configuration. BMI tasks produce no additional scientific output or resolved configuration files.

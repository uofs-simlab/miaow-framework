Building, testing, and installation
===================================

Prerequisites
-------------

MIAOW requires `CMake <https://cmake.org/>`_ 3.30 or newer, a C++20 compiler, and a Fortran compiler. The CAF, JSON, and Boost provider options specify where CMake obtains these dependencies: from packages already installed on the system, by fetching and building them from source, or by trying the system first and fetching them if necessary. Boost is needed only when building the job record checker. See :ref:`build-options` for a description of the available provider settings.

On Ubuntu, the required compiler and build tools can be installed with the following command:

.. code-block:: sh

   sudo apt-get install build-essential cmake gfortran

On macOS with `Homebrew <https://brew.sh/>`_, the required compiler and build tools can be installed using the following commands:

.. code-block:: sh

   sudo xcode-select --install  # if Apple's Command Line Tools are not installed
   brew install cmake gcc

Repository development build
----------------------------

The repository can be configured and built with the following commands:

.. code-block:: sh

   cmake -S . -B build
   cmake --build build

``CITATION.cff`` is the single source for the software version. Use ``version: "0.3.0-dev"`` during development and ``version: "0.3.0"`` for a release; CMake extracts ``0.3.0`` and automatically reconfigures when the citation file changes.

With ``BUILD_TESTING=ON`` (the default), CMake builds the ``miaow_job_record_checks`` checker, the lifecycle and BMI dispatch test drivers, and the five examples required by the standard regression suite: ``00_hello``, ``01_heat``, ``03_dynamic_bmi``, ``07_job_controller``, and ``09_bmi_operations``. Their tests are registered even with ``BUILD_EXAMPLES=OFF``. See :ref:`build-options` for the components built in each configuration, and :ref:`building-examples` to select additional examples.

Run the tests enabled in the current build with the following command:

.. code-block:: sh

   ctest --test-dir build --output-on-failure

A single test can be selected by name with the following command:

.. code-block:: sh

   ctest --test-dir build -R <test_name> --output-on-failure

.. _building-examples:

Building examples
-----------------

``BUILD_TESTING=ON`` always includes the five regression examples. ``BUILD_EXAMPLES=ON`` adds the examples selected by ``MIAOW_EXAMPLES``; overlapping selections are built once. To build the selected examples without the regression suite:

.. code-block:: sh

   cmake -S . -B build -DBUILD_TESTING=OFF -DBUILD_EXAMPLES=ON
   cmake --build build

Override the selected example set with a semicolon-separated ``MIAOW_EXAMPLES`` value, for example ``-DMIAOW_EXAMPLES="00_hello;01_heat"``. This selection supplements the required regression examples when testing is enabled. A normal ``cmake --build build`` builds every included example; use ``--target miaow_hello`` to narrow the build to that specific target.

Each model-backed example accepts a ``MIAOW_<MODEL_NAME>_MODEL_ROOT`` override. When the override is empty, CMake resolves the source beneath ``MIAOW_DEFAULT_MODEL_BASE_DIR``. If the resolved directory is missing and the example declares a Git repository and revision, CMake fetches the model into that directory. See :doc:`examples` for model-specific dependencies and options.

Regression tests
----------------

CI runs the simulation-unit lifecycle cases and the regression tests for the default examples on every push and pull request. To run the same suite locally:

.. code-block:: sh

   cmake -S . -B build -DBUILD_TESTING=ON -DBUILD_EXAMPLES=OFF
   cmake --build build
   ctest --test-dir build -L regression --output-on-failure

CTest runs the examples and test drivers, checks exit codes, and uses Boost.Test to validate saved job records. JSON cases in ``tests/cases`` define inputs and expected results.

Tests cover BMI execution, unit removal and replacement, and callback delivery. Lifecycle cases run under both callback policies with one and two workers.

Default example success tests
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Examples run with their default inputs.

.. list-table::
   :header-rows: 1
   :widths: 34 28 38
   :class: regression-tests

   * - CTest name
     - Code executed
     - Expected result
   * - `miaow_example_00_hello`_
     - `00_hello/main.cpp`_

       Runs configurable greeting tasks with an in-process executor.
     - Saved tasks 0 and 1 succeed with messages ``hello`` and ``hello again``, names ``greeting_task_0`` and ``message_1_task_1``, and ``feature_id=greeting`` on task 0
   * - `miaow_example_01_heat`_
     - `01_heat/main.cpp`_

       Runs the C++ heat model with a statically linked BMI executor.
     - The statically linked C++ heat model completes 120 updates to its configured 20-second end time; its saved task succeeds
   * - `miaow_example_03_dynamic_bmi`_
     - `03_dynamic_bmi/main.cpp`_

       Submits in-process and subprocess runs for the C++ and Fortran heat-model plugins.
     - All four heat tasks succeed after 40 updates to 10 seconds; each plugin uses distinct task executor IDs for in-process and subprocess execution
   * - `miaow_example_07_job_controller`_
     - `07_job_controller/main.cpp`_

       Requests Heron iterations until convergence or the iteration limit.
     - Starting from 1, three Heron estimates match 1.5, 17/12, and 577/408 within ``1e-14``; the final residual ``|x^2 - 2|`` is at most ``1e-5``
   * - `miaow_example_09_bmi_operations`_
     - `09_bmi_operations/main.cpp`_

       Submits initialization, time queries, two updates, and finalization on one heat unit.
     - Seven tasks succeed on the same unit; model times match 0, 1/6, and 1/3 seconds within ``1e-14``


Alternative example regression tests
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Successful example runs use JSON input overrides.

.. list-table::
   :header-rows: 1
   :widths: 34 28 38
   :class: regression-tests

   * - CTest name
     - Code executed
     - Expected result
   * - `miaow_example_00_hello_empty`_
     - `00_hello/main.cpp`_

       Submits an empty job.
     - Submitting a job with an empty task list succeeds and writes ``job_record.json`` with an empty ``tasks`` array
   * - `miaow_example_03_dynamic_bmi_zero_tolerance`_
     - `03_dynamic_bmi/main.cpp`_

       Submits in-process and subprocess runs for the C++ and Fortran heat-model plugins.
     - With zero clock tolerance, all four tasks succeed after forty 0.25-second updates reach 10 seconds exactly
   * - `miaow_example_03_dynamic_bmi_zero_duration`_
     - `03_dynamic_bmi/main.cpp`_

       Submits in-process and subprocess runs for the C++ and Fortran heat-model plugins.
     - With initial and end times both zero, all four tasks succeed with zero updates
   * - `miaow_example_07_job_controller_small_values`_
     - `07_job_controller/main.cpp`_

       Requests Heron iterations until convergence or the iteration limit.
     - For radicand ``2e-16``, saved estimates match ``1e-8 * [1.5, 17/12, 577/408]`` within ``1e-14`` relative error; the final residual is positive and at most ``1e-21``


Example failure and recovery tests
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Examples exercise invalid inputs, deadlines, failed tasks, and recovery.

.. list-table::
   :header-rows: 1
   :widths: 34 28 38
   :class: regression-tests

   * - CTest name
     - Code executed
     - Expected result
   * - `miaow_example_00_hello_missing_message`_
     - `00_hello/main.cpp`_

       Submits a task without a message followed by a valid greeting, using one worker slot.
     - With one concurrent task slot, an operation without a message fails; the following unit's operation succeeds with message ``recovered``
   * - `miaow_example_00_hello_unknown_executor`_
     - `00_hello/main.cpp`_

       Submits greetings using an unregistered executor and a valid executor.
     - A unit selecting unregistered executor 999 fails during activation; the default controller fails the job and no job record is saved
   * - `miaow_example_00_hello_duplicate_feature_id`_
     - `00_hello/main.cpp`_

       Attempts to declare two greeting units with the same ``feature_id=greeting``.
     - Declaring two units with ``feature_id=greeting`` produces a duplicate unit-label error; no job record is saved
   * - `miaow_example_00_hello_feature_id_slash`_
     - `00_hello/main.cpp`_

       Uses a greeting feature label containing a forward slash.
     - A task with ``feature_id=greeting/1`` is rejected for containing a path separator; no job record is saved
   * - `miaow_example_00_hello_feature_id_backslash`_
     - `00_hello/main.cpp`_

       Uses a greeting feature label containing a backslash.
     - A task whose feature ID contains a backslash is rejected for containing a path separator; no job record is saved
   * - `miaow_example_00_hello_task_timeout`_
     - `00_hello/main.cpp`_

       Runs a 2.5-second greeting with a 1-second task deadline.
     - A 2.5-second operation exceeds its 1-second deadline; the default controller fails the job with ``request_timeout`` and saves no job record. Shutdown waits for the native call to return
   * - `miaow_example_00_hello_job_timeout`_
     - `00_hello/main.cpp`_

       Queues two 2.5-second greetings with one worker slot and a 1-second job deadline.
     - A 1-second job timeout marks the active 2.5-second task and a queued task failed; the queued record retains its target, has no dispatch offset, and has a zero run time
   * - `miaow_example_00_hello_invalid_max_workers`_
     - `00_hello/main.cpp`_

       Passes ``--max_workers 0`` to the greeting example.
     - ``--max_workers 0`` reports ``requires a positive integer``; no job record is saved
   * - `miaow_example_00_hello_invalid_task_timeout`_
     - `00_hello/main.cpp`_

       Passes ``--task_request_timeout 0`` to the greeting example.
     - ``--task_request_timeout 0`` reports ``requires a positive integer``; no job record is saved
   * - `miaow_example_00_hello_invalid_job_timeout`_
     - `00_hello/main.cpp`_

       Passes ``--job_request_timeout 0`` to the greeting example.
     - ``--job_request_timeout 0`` reports ``requires a positive integer``; no job record is saved
   * - `miaow_example_03_dynamic_bmi_missing_cpp_plugin`_
     - `03_dynamic_bmi/main.cpp`_

       Submits four plugin tasks with a missing C++ plugin path and a valid Fortran plugin path.
     - A nonexistent C++ plugin path fails both C++ tasks; both Fortran tasks use a valid plugin path and succeed after 40 updates
   * - `miaow_example_03_dynamic_bmi_missing_fortran_plugin`_
     - `03_dynamic_bmi/main.cpp`_

       Submits four plugin tasks with a missing Fortran plugin path and a valid C++ plugin path.
     - A nonexistent Fortran plugin path fails both Fortran tasks; both C++ tasks use a valid plugin path and succeed after 40 updates
   * - `miaow_example_03_dynamic_bmi_invalid_tolerance`_
     - `03_dynamic_bmi/main.cpp`_

       Sets a 1-second clock tolerance for heat models with a 0.25-second time step.
     - A 1-second clock tolerance exceeds half the 0.25-second time step; all four tasks fail with zero updates
   * - `miaow_example_03_dynamic_bmi_negative_tolerance`_
     - `03_dynamic_bmi/main.cpp`_

       Sets a clock tolerance of -1 second for all four plugin tasks.
     - A clock tolerance of -1 second fails validation; all four tasks fail with zero updates
   * - `miaow_example_03_dynamic_bmi_tolerance_boundary`_
     - `03_dynamic_bmi/main.cpp`_

       Sets clock tolerance to half the 0.25-second model time step.
     - A clock tolerance of 0.125 seconds equals half the 0.25-second time step; all four tasks fail with zero updates
   * - `miaow_example_03_dynamic_bmi_overshoot`_
     - `03_dynamic_bmi/main.cpp`_

       Sets the model end time to 0.1 seconds with a 0.25-second time step.
     - With end time 0.1 seconds, all four tasks fail after one 0.25-second update with ``update passed the end time``
   * - `miaow_example_07_job_controller_non_convergence`_
     - `07_job_controller/main.cpp`_

       Limits the Heron controller to one iteration.
     - With the iteration limit set to 1, the controller reports non-convergence after its first iteration; the program exits with code 1 and saves no job record


Tests using dedicated drivers
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

BMI task dispatch driver
""""""""""""""""""""""""

These cases use ``tests/bmi_task_dispatch_driver.cpp``. The five execution paths are a statically linked C++ model, C++ and Fortran plugins in-process, and C++ and Fortran plugins in subprocesses.

.. list-table::
   :header-rows: 1
   :widths: 34 28 38
   :class: regression-tests

   * - CTest name
     - Code executed
     - Expected result
   * - `miaow_bmi_task_dispatch`_
     - `bmi_task_dispatch_driver.cpp`_

       Submits an unsupported operation, then a valid run, on each of five BMI execution paths.
     - Five execution paths each reject an unknown operation and then complete a valid run after 40 updates
   * - `miaow_bmi_static_lifecycle`_
     - `bmi_task_dispatch_driver.cpp`_

       Rejects invalid lifecycle operations on one static heat model, then finalizes and reinitializes it.
     - Updates outside an initialized session, duplicate initialization, and a complete run during a session fail; the active clock is preserved, and a fresh session starts at zero
   * - `miaow_bmi_dynamic_cpp_lifecycle`_
     - `bmi_task_dispatch_driver.cpp`_

       Runs the lifecycle sequence with an in-process C++ plugin.
     - The same lifecycle guards hold; rejected operations preserve time 0.25 seconds, and reinitialization starts at zero
   * - `miaow_bmi_dynamic_fortran_lifecycle`_
     - `bmi_task_dispatch_driver.cpp`_

       Runs the lifecycle sequence with an in-process Fortran plugin.
     - The same lifecycle guards hold; rejected operations preserve time 0.25 seconds, and reinitialization starts at zero
   * - `miaow_bmi_subprocess_cpp_lifecycle`_
     - `bmi_task_dispatch_driver.cpp`_

       Runs the lifecycle sequence with a C++ plugin in a persistent subprocess.
     - Model time persists between tasks; rejected operations preserve time 0.25 seconds, and reinitialization starts at zero
   * - `miaow_bmi_subprocess_fortran_lifecycle`_
     - `bmi_task_dispatch_driver.cpp`_

       Runs the lifecycle sequence with a Fortran plugin in a persistent subprocess.
     - Model time persists between tasks; rejected operations preserve time 0.25 seconds, and reinitialization starts at zero
   * - `miaow_bmi_repeated_run`_
     - `bmi_task_dispatch_driver.cpp`_

       Submits two successive runs per unit across five BMI execution paths.
     - Five execution paths each complete two successive runs on the same unit, with 40 updates per run
   * - `miaow_bmi_repeated_run_concurrent`_
     - `bmi_task_dispatch_driver.cpp`_

       Submits two successive runs per unit across five BMI execution paths.
     - With three worker slots, each of the five units completes two successful runs of 40 updates.


Simulation unit lifecycle driver
""""""""""""""""""""""""""""""""

These cases use ``tests/simulation_unit_lifecycle_driver.cpp``.

.. list-table::
   :header-rows: 1
   :widths: 34 28 38
   :class: regression-tests

   * - CTest name
     - Code executed
     - Expected result
   * - `miaow_simulation_unit_removal_with_queued_work`_
     - `simulation_unit_lifecycle_driver.cpp`_

       Queues work, removes the target unit, and continues work on another unit.
     - With one worker slot and asynchronous callbacks, accepted target work drains, new work is rejected, repeated removal is harmless, every task is reported once, and the other unit retains its handle and state.
   * - `miaow_simulation_unit_removal_with_queued_work_concurrent`_
     - `simulation_unit_lifecycle_driver.cpp`_

       Queues work, removes the target unit, and continues work on another unit.
     - With two worker slots and asynchronous callbacks, accepted target work drains, new work is rejected, repeated removal is harmless, every task is reported once, and the other unit retains its handle and state.
   * - `miaow_simulation_unit_removal_with_queued_work_batch_synchronous`_
     - `simulation_unit_lifecycle_driver.cpp`_

       Queues work, removes the target unit, and continues work on another unit.
     - With one worker slot and batch-synchronous callbacks, one callback reports all four tasks with no unit failures; accepted work drains, new work is rejected, and the other unit retains its handle and state.
   * - `miaow_simulation_unit_removal_with_queued_work_batch_synchronous_concurrent`_
     - `simulation_unit_lifecycle_driver.cpp`_

       Queues work, removes the target unit, and continues work on another unit.
     - With two worker slots and batch-synchronous callbacks, one callback reports all four tasks with no unit failures; accepted work drains, new work is rejected, and the other unit retains its handle and state.
   * - `miaow_simulation_unit_replacement_after_failure`_
     - `simulation_unit_lifecycle_driver.cpp`_

       Injects executor-construction failure, replaces the unit, and continues work.
     - With one worker slot and asynchronous callbacks, dispatched and queued target tasks fail once with one unit-failure report; the replacement reuses the label with a new handle and fresh state, while the other unit retains its handle and state.
   * - `miaow_simulation_unit_replacement_after_failure_concurrent`_
     - `simulation_unit_lifecycle_driver.cpp`_

       Injects executor-construction failure, replaces the unit, and continues work.
     - With two worker slots and asynchronous callbacks, dispatched and queued target tasks fail once with one unit-failure report; the replacement reuses the label with a new handle and fresh state, while the other unit retains its handle and state.
   * - `miaow_simulation_unit_replacement_after_failure_batch_synchronous`_
     - `simulation_unit_lifecycle_driver.cpp`_

       Injects executor-construction failure, replaces the unit, and continues work.
     - With one worker slot, the first batch callback reports two failed tasks, one successful task, and one unit failure; the second reports three successful tasks and no unit failures.
   * - `miaow_simulation_unit_replacement_after_failure_batch_synchronous_concurrent`_
     - `simulation_unit_lifecycle_driver.cpp`_

       Injects executor-construction failure, replaces the unit, and continues work.
     - With two worker slots, the first batch callback reports two failed tasks, one successful task, and one unit failure; the second reports three successful tasks and no unit failures.
   * - `miaow_subprocess_shutdown_within_grace`_
     - `simulation_unit_lifecycle_driver.cpp`_ and `subprocess_shutdown_worker.cpp`_

       Times out a child task that subsequently returns and completes destructor cleanup within the shutdown grace period.
     - The child exits normally and writes its cleanup marker. The active and queued tasks fail once with one unit-failure report, the queued task never dispatches, new work on the failed unit is rejected, and the worker slot becomes available only after the child is reaped.
   * - `miaow_subprocess_shutdown_grace_expired`_
     - `simulation_unit_lifecycle_driver.cpp`_ and `subprocess_shutdown_worker.cpp`_

       Times out a child task whose destructor cleanup exceeds the configured grace period.
     - The child starts cleanup but is terminated by SIGKILL before finishing it. The timeout remains the failure cause, the child is reaped, queued work never dispatches, and the independent unit can then use the worker slot.


.. _miaow_example_00_hello: https://github.com/uofs-simlab/miaow-framework/blob/main/examples/00_hello/CMakeLists.txt
.. _00_hello/main.cpp: https://github.com/uofs-simlab/miaow-framework/blob/main/examples/00_hello/main.cpp
.. _miaow_example_01_heat: https://github.com/uofs-simlab/miaow-framework/blob/main/examples/01_heat/CMakeLists.txt
.. _01_heat/main.cpp: https://github.com/uofs-simlab/miaow-framework/blob/main/examples/01_heat/main.cpp
.. _miaow_example_03_dynamic_bmi: https://github.com/uofs-simlab/miaow-framework/blob/main/examples/03_dynamic_bmi/CMakeLists.txt
.. _03_dynamic_bmi/main.cpp: https://github.com/uofs-simlab/miaow-framework/blob/main/examples/03_dynamic_bmi/main.cpp
.. _miaow_example_07_job_controller: https://github.com/uofs-simlab/miaow-framework/blob/main/examples/07_job_controller/CMakeLists.txt
.. _07_job_controller/main.cpp: https://github.com/uofs-simlab/miaow-framework/blob/main/examples/07_job_controller/main.cpp
.. _miaow_example_09_bmi_operations: https://github.com/uofs-simlab/miaow-framework/blob/main/examples/09_bmi_operations/CMakeLists.txt
.. _09_bmi_operations/main.cpp: https://github.com/uofs-simlab/miaow-framework/blob/main/examples/09_bmi_operations/main.cpp
.. _miaow_example_00_hello_empty: https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/hello_empty.json
.. _miaow_example_03_dynamic_bmi_zero_tolerance: https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/dynamic_zero_tolerance.json
.. _miaow_example_03_dynamic_bmi_zero_duration: https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/dynamic_zero_duration.json
.. _miaow_example_07_job_controller_small_values: https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/controller_small_values.json
.. _miaow_example_00_hello_missing_message: https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/hello_missing_message.json
.. _miaow_example_00_hello_unknown_executor: https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/hello_unknown_executor.json
.. _miaow_example_00_hello_duplicate_feature_id: https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/hello_duplicate_feature_id.json
.. _miaow_example_00_hello_feature_id_slash: https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/hello_feature_id_slash.json
.. _miaow_example_00_hello_feature_id_backslash: https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/hello_feature_id_backslash.json
.. _miaow_example_00_hello_task_timeout: https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/hello_task_timeout.json
.. _miaow_example_00_hello_job_timeout: https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/hello_job_timeout.json
.. _miaow_example_00_hello_invalid_max_workers: https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/hello_invalid_max_workers.json
.. _miaow_example_00_hello_invalid_task_timeout: https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/hello_invalid_task_timeout.json
.. _miaow_example_00_hello_invalid_job_timeout: https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/hello_invalid_job_timeout.json
.. _miaow_example_03_dynamic_bmi_missing_cpp_plugin: https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/dynamic_missing_cpp_plugin.json
.. _miaow_example_03_dynamic_bmi_missing_fortran_plugin: https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/dynamic_missing_fortran_plugin.json
.. _miaow_example_03_dynamic_bmi_invalid_tolerance: https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/dynamic_invalid_tolerance.json
.. _miaow_example_03_dynamic_bmi_negative_tolerance: https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/dynamic_negative_tolerance.json
.. _miaow_example_03_dynamic_bmi_tolerance_boundary: https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/dynamic_tolerance_boundary.json
.. _miaow_example_03_dynamic_bmi_overshoot: https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/dynamic_overshoot.json
.. _miaow_example_07_job_controller_non_convergence: https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/controller_non_convergence.json
.. _miaow_bmi_task_dispatch: https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/dynamic_unknown_operation.json
.. _miaow_bmi_static_lifecycle: https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/bmi_static_lifecycle.json
.. _miaow_bmi_dynamic_cpp_lifecycle: https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/bmi_dynamic_cpp_lifecycle.json
.. _miaow_bmi_dynamic_fortran_lifecycle: https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/bmi_dynamic_fortran_lifecycle.json
.. _miaow_bmi_subprocess_cpp_lifecycle: https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/bmi_subprocess_cpp_lifecycle.json
.. _miaow_bmi_subprocess_fortran_lifecycle: https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/bmi_subprocess_fortran_lifecycle.json
.. _bmi_task_dispatch_driver.cpp: https://github.com/uofs-simlab/miaow-framework/blob/main/tests/bmi_task_dispatch_driver.cpp
.. _miaow_bmi_repeated_run: https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/dynamic_repeated_run.json
.. _miaow_bmi_repeated_run_concurrent: https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/dynamic_repeated_run_concurrent.json
.. _miaow_simulation_unit_removal_with_queued_work: https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/unit_removal_with_queued_work.json
.. _simulation_unit_lifecycle_driver.cpp: https://github.com/uofs-simlab/miaow-framework/blob/main/tests/simulation_unit_lifecycle_driver.cpp
.. _subprocess_shutdown_worker.cpp: https://github.com/uofs-simlab/miaow-framework/blob/main/tests/subprocess_shutdown_worker.cpp
.. _miaow_subprocess_shutdown_within_grace: https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/subprocess_shutdown_within_grace.json
.. _miaow_subprocess_shutdown_grace_expired: https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/subprocess_shutdown_grace_expired.json
.. _miaow_simulation_unit_removal_with_queued_work_concurrent: https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/unit_removal_with_queued_work_concurrent.json
.. _miaow_simulation_unit_removal_with_queued_work_batch_synchronous: https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/unit_removal_with_queued_work_batch_synchronous.json
.. _miaow_simulation_unit_removal_with_queued_work_batch_synchronous_concurrent: https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/unit_removal_with_queued_work_batch_synchronous_concurrent.json
.. _miaow_simulation_unit_replacement_after_failure: https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/unit_replacement_after_failure.json
.. _miaow_simulation_unit_replacement_after_failure_concurrent: https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/unit_replacement_after_failure_concurrent.json
.. _miaow_simulation_unit_replacement_after_failure_batch_synchronous: https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/unit_replacement_after_failure_batch_synchronous.json
.. _miaow_simulation_unit_replacement_after_failure_batch_synchronous_concurrent: https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/unit_replacement_after_failure_batch_synchronous_concurrent.json

Library installation build
--------------------------

The following example configures a build without tests and selects ``$HOME/.local`` as an example installation prefix. Any writable installation path can be supplied through ``CMAKE_INSTALL_PREFIX``:

.. code-block:: sh

   cmake -S . -B build \
       -DBUILD_TESTING=OFF \
       -DBUILD_EXAMPLES=OFF \
       -DCMAKE_INSTALL_PREFIX="$HOME/.local"

   cmake --build build
   cmake --install build

This configuration builds and installs the ``miaow`` C++ library, the ``iso_c_bmi`` Fortran BMI bridge, and the ``miaow_subprocess`` BMI worker executable.

Building MIAOW from source requires C++20 and Fortran compilers. The completed installation can be used by a C++-only project without enabling Fortran or requiring a Fortran compiler.

Using the installed package
^^^^^^^^^^^^^^^^^^^^^^^^^^^

In another project, define the executable or library that uses MIAOW and link it to the installed ``miaow::miaow`` target. For example, a minimal downstream ``CMakeLists.txt`` is:

.. code-block:: cmake

   cmake_minimum_required(VERSION 3.30)

   project(my_program LANGUAGES CXX)

   find_package(miaow CONFIG REQUIRED)

   add_executable(my_program main.cpp)
   target_link_libraries(my_program PRIVATE miaow::miaow)

The imported target supplies the installed MIAOW include directory, requests C++20, and links the required CAF and procedural BMI bridge libraries. Source files can include ``miaow.hpp``, ``miaow_bmi.hpp``, and ``miaow_io.hpp`` as needed.

A custom subprocess worker is another executable linked against ``miaow::miaow``:

.. code-block:: cmake

   add_executable(my_worker worker.cpp)
   target_link_libraries(my_worker PRIVATE miaow::miaow)

When the selected installation prefix is outside CMake's normal search locations, the downstream configuration supplies the same path through ``CMAKE_PREFIX_PATH``:

.. code-block:: sh

   cmake -S . -B build -DCMAKE_PREFIX_PATH="$HOME/.local"
   cmake --build build

The MIAOW package locates the required CAF components when ``find_package(miaow)`` runs. CAF must be discoverable through the system installation, ``CMAKE_PREFIX_PATH``, or the downstream project's CMake toolchain. `nlohmann/json <https://json.nlohmann.me/>`_ is a private header-only build dependency and is not required by installed consumers. The installed procedural BMI bridge depends on a compatible Fortran runtime library, such as ``libgfortran`` when the bridge is built with GNU Fortran. This runtime library must remain available when an application that links to MIAOW runs, but compiling the downstream project does not require a Fortran compiler.

.. note::

   Configure ``CMAKE_INSTALL_PREFIX`` before building so ``bmi_subprocess_executable_path()`` can locate the installed worker. Changing only ``cmake --install build --prefix <path>`` does not update this lookup. If you relocate the installation, reconfigure and rebuild MIAOW or pass the installed worker path directly to ``TaskExecutorRegistry::add_subprocess()``.

.. _build-options:

Build options
-------------

All top-level repository configurations build the following runtime components, which are installed by ``cmake --install``:

* ``miaow``: the static C++ library containing the runtime, job and unit APIs, and BMI task executors.
* ``iso_c_bmi``: the shared Fortran BMI bridge. It exposes C-compatible functions so MIAOW's C++ code can call Fortran BMI models, both in-process and in subprocesses.
* ``miaow_subprocess``: the built-in BMI worker executable. MIAOW starts it when a subprocess-backed unit first needs it and reuses it for that unit's tasks.

The standard regression suite is enabled when ``BUILD_TESTING`` is ``ON``, and builds the following additional programs:

* ``miaow_job_record_checks``: the Boost.Test executable that checks saved job records and controller observations.
* ``miaow_simulation_unit_lifecycle_driver``: the model-free workload driver for removal, failure recovery, and callback delivery.
* ``miaow_bmi_dispatch_driver``: the heat-model workload driver for operation failures and repeated runs across the five BMI execution paths.
* The ``00_hello``, ``01_heat``, ``03_dynamic_bmi``, ``07_job_controller``, and ``09_bmi_operations`` example programs, including the heat-model libraries and C++ and Fortran plugins needed by their tests.

The following top-level CMake options are available when building MIAOW:

.. list-table::
   :header-rows: 1
   :widths: 35 30 45

   * - Flag
     - Default
     - Description
   * - ``-DBUILD_TESTING``
     - ``ON``
     - Build and register the standard regression suite, including its checker, drivers, and required example programs, in a top-level repository build
   * - ``-DBUILD_EXAMPLES``
     - ``OFF``
     - Build the examples selected by ``MIAOW_EXAMPLES``; if ``BUILD_TESTING`` is also ``ON``, required regression examples are built even if not listed in ``MIAOW_EXAMPLES``
   * - ``-DMIAOW_EXAMPLES``
     - | ``00_hello;01_heat;``
       | ``03_dynamic_bmi;``
       | ``07_job_controller;``
       | ``09_bmi_operations``
     - Semicolon-separated list of examples selected by ``BUILD_EXAMPLES``; testing includes its required examples independently
   * - ``-DMIAOW_BOOST_PROVIDER``
     - ``auto``
     - Boost.Test provider: ``system``, ``fetch``, or ``auto``; used only for the job record checker
   * - ``-DMIAOW_BOOST_VERSION``
     - ``1.86.0``
     - Boost version used by the ``FetchContent`` fallback
   * - ``-DMIAOW_JSON_PROVIDER``
     - ``auto``
     - nlohmann/json provider: ``system``, ``fetch``, or ``auto``
   * - ``-DMIAOW_JSON_VERSION``
     - ``3.12.0``
     - nlohmann/json version used by ``find_package`` and ``FetchContent``
   * - ``-DMIAOW_CAF_PROVIDER``
     - ``auto``
     - CAF provider: ``system``, ``fetch``, or ``auto``
   * - ``-DMIAOW_CAF_VERSION``
     - ``1.1.0``
     - CAF version used by ``find_package`` and ``FetchContent``
   * - ``-DMIAOW_DEFAULT_MODEL_BASE_DIR``
     - ``${PROJECT_SOURCE_DIR}/third_party``
     - Default base directory where external model sources are resolved or fetched
   * - ``-DMIAOW_ISO_C_FORTRAN_BMI_ROOT``
     - empty
     - Optional explicit path to a valid local ``NOAA-OWP/iso_c_fortran_bmi`` checkout

When ``MIAOW_ISO_C_FORTRAN_BMI_ROOT`` is empty, CMake looks for ``iso_c_fortran_bmi`` under ``MIAOW_DEFAULT_MODEL_BASE_DIR`` and fetches it into that location if missing.

Provider values are interpreted as follows:

- ``system``: require ``find_package(...)`` to provide the dependency
- ``fetch``: skip ``find_package(...)`` and use ``FetchContent``
- ``auto``: try ``find_package(...)``, then use ``FetchContent`` if not found

Documentation build
-------------------

The generated API documentation uses `Doxygen <https://www.doxygen.nl/>`_ for C++ documentation-comment extraction and `Sphinx <https://www.sphinx-doc.org/>`_ with `Breathe <https://breathe.readthedocs.io/>`_ for HTML rendering. Architecture diagrams use Mermaid sources in ``docs/diagrams``, rendered by ``sphinxcontrib-mermaid``. Doxygen must be installed separately and available on ``PATH``.

The following commands create a dedicated Python environment and build the documentation:

.. code-block:: sh

   python3 -m venv docs/.venv
   docs/.venv/bin/pip install -r docs/requirements.txt
   docs/.venv/bin/sphinx-build -E -n -b html -d docs/_build/doctrees docs docs/_build/html

The generated HTML entry point is ``docs/_build/html/index.html``.

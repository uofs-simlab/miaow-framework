Building, testing, and installation
===================================

Prerequisites
-------------

MIAOW requires `CMake <https://cmake.org/>`_ 3.30 or newer, a C++20 compiler, and a Fortran compiler. The CAF, JSON, and Boost provider options specify where CMake obtains these dependencies: from packages already installed on the system, by fetching and building them from source, or by trying the system first and fetching them if necessary. Boost is needed only when building the example regression checker. See :ref:`build-options` for a description of the available provider settings.

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

The default development build includes the ``miaow_example_tests`` checker with ``BUILD_TESTING=ON``, but examples are disabled and no CTest tests are registered. Enable example executables as described in :ref:`building-examples` to register their tests.

Run the tests enabled in the current build with the following command:

.. code-block:: sh

   ctest --test-dir build --output-on-failure

A single test can be selected by name with the following command:

.. code-block:: sh

   ctest --test-dir build -R <test_name> --output-on-failure

.. _building-examples:

Building examples
-----------------

Examples are disabled by default. The following configuration enables and builds the default example set:

.. code-block:: sh

   cmake -S . -B build -DBUILD_EXAMPLES=ON
   cmake --build build

Override the default example set with a semicolon-separated ``MIAOW_EXAMPLES`` value, for example ``-DMIAOW_EXAMPLES="00_hello;01_heat"``. A normal ``cmake --build build`` builds every selected example; use ``--target miaow_hello`` to narrow the build to that specific target.

Each model-backed example accepts a ``MIAOW_<MODEL_NAME>_MODEL_ROOT`` override. When the override is empty, CMake resolves the source beneath ``MIAOW_DEFAULT_MODEL_BASE_DIR``. If the resolved directory is missing and the example declares a Git repository and revision, CMake fetches the model into that directory. See :doc:`examples` for model-specific dependencies and options.

Regression tests
----------------

CI runs regression tests through the four default examples on every push and pull request. To run the same suite locally:

.. code-block:: sh

   cmake -S . -B build -DBUILD_EXAMPLES=ON
   cmake --build build
   ctest --test-dir build -L regression --output-on-failure

The ``01_heat`` regression uses the pinned CSDMS C++ heat model. The ``03_dynamic_bmi`` regressions use both the C++ and Fortran heat models. CMake locates or fetches their sources; use ``MIAOW_HEAT_MODEL_ROOT``, ``MIAOW_DYNAMIC_BMI_CXX_MODEL_ROOT``, and ``MIAOW_DYNAMIC_BMI_FORTRAN_MODEL_ROOT`` to supply local checkouts. The regressions execute the actual example programs with a separate output directory for each case under the build tree. The CMake runner checks the exact exit code, then selects a Boost.Test checker in ``tests/example_regression_checks.cpp`` to inspect saved job records. Successful-run checkers share ``JobRecordChecker``, which requires successful tasks, unique task IDs, assigned worker IDs, and dispatch offsets. The ``expected_failure`` checker exercises options that lead to expected errors, including successful work after a failed task. Errors that prevent a job record must produce the expected diagnostic and leave no record; a crash or an unrelated non-zero exit code does not satisfy these checks.

Each JSON case in ``tests/cases`` supplies example inputs, an expected exit code, and any runtime overrides. Failure cases also specify diagnostic fragments and expected task records. Heat initialization files beside the JSON cases select zero-duration and overshoot runs. Example-local ``CMakeLists.txt`` files register each CTest name and choose its checker; multiple cases can share one checker. The table below lists every default CTest case, with test names linking to their JSON case files if available. Tests without a JSON case use the default inputs defined in the example's source code; their links lead to the CMake test registration. Each dynamic BMI case runs four tasks: one in-process and one in a fresh subprocess for each of the C++ and Fortran plugins. Saved records below refer to ``job_record.json``.

.. list-table::
   :header-rows: 1
   :widths: 45 55

   * - CTest name
     - Expected behaviour
   * - `miaow_example_00_hello <https://github.com/uofs-simlab/miaow-framework/blob/main/examples/00_hello/CMakeLists.txt>`_
     - Saved tasks 0 and 1 succeed with messages ``hello`` and ``hello again``, names ``greeting`` and ``task_1``, and ``feature_id=greeting`` on task 0
   * - `miaow_example_00_hello_empty <https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/hello_empty.json>`_
     - Submitting a job with an empty task list succeeds and writes ``job_record.json`` with an empty ``tasks`` array
   * - `miaow_example_00_hello_missing_message <https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/hello_missing_message.json>`_
     - With one worker, a task without a message fails; the following task succeeds with message ``recovered``
   * - `miaow_example_00_hello_unknown_unit <https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/hello_unknown_unit.json>`_
     - With one worker, a task selecting unregistered unit 999 fails; the following task selects the registered unit and returns ``recovered``
   * - `miaow_example_00_hello_duplicate_feature_id <https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/hello_duplicate_feature_id.json>`_
     - Two tasks with ``feature_id=greeting`` produce a duplicate task name error; no job record is saved
   * - `miaow_example_00_hello_feature_id_slash <https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/hello_feature_id_slash.json>`_
     - A task with ``feature_id=greeting/1`` is rejected for containing a path separator; no job record is saved
   * - `miaow_example_00_hello_feature_id_backslash <https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/hello_feature_id_backslash.json>`_
     - A task whose feature ID contains a backslash is rejected for containing a path separator; no job record is saved
   * - `miaow_example_00_hello_task_timeout <https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/hello_task_timeout.json>`_
     - A task sleeping for 2.5 seconds exceeds its 1-second request timeout; its saved status is ``failed`` and its message contains ``request_timeout``
   * - `miaow_example_00_hello_job_timeout <https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/hello_job_timeout.json>`_
     - A 1-second job timeout marks the active 2.5-second task and a queued task failed; the queued record has no worker ID or dispatch offset and a zero run time
   * - `miaow_example_00_hello_invalid_max_workers <https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/hello_invalid_max_workers.json>`_
     - ``--max_workers 0`` reports ``requires a positive integer``; no job record is saved
   * - `miaow_example_00_hello_invalid_task_timeout <https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/hello_invalid_task_timeout.json>`_
     - ``--task_request_timeout 0`` reports ``requires a positive integer``; no job record is saved
   * - `miaow_example_00_hello_invalid_job_timeout <https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/hello_invalid_job_timeout.json>`_
     - ``--job_request_timeout 0`` reports ``requires a positive integer``; no job record is saved
   * - `miaow_example_01_heat <https://github.com/uofs-simlab/miaow-framework/blob/main/examples/01_heat/CMakeLists.txt>`_
     - The statically linked C++ heat model completes 120 updates to its configured 20-second end time; its saved task succeeds
   * - `miaow_example_03_dynamic_bmi <https://github.com/uofs-simlab/miaow-framework/blob/main/examples/03_dynamic_bmi/CMakeLists.txt>`_
     - All four heat tasks succeed after 40 updates to 10 seconds; each plugin uses distinct simulation unit IDs for in-process and subprocess execution
   * - `miaow_example_03_dynamic_bmi_zero_tolerance <https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/dynamic_zero_tolerance.json>`_
     - With zero clock tolerance, all four tasks succeed after forty 0.25-second updates reach 10 seconds exactly
   * - `miaow_example_03_dynamic_bmi_zero_duration <https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/dynamic_zero_duration.json>`_
     - With initial and end times both zero, all four tasks succeed with zero updates
   * - `miaow_example_03_dynamic_bmi_missing_cpp_plugin <https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/dynamic_missing_cpp_plugin.json>`_
     - A nonexistent C++ plugin path fails both C++ tasks; both Fortran tasks use a valid plugin path and succeed after 40 updates
   * - `miaow_example_03_dynamic_bmi_missing_fortran_plugin <https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/dynamic_missing_fortran_plugin.json>`_
     - A nonexistent Fortran plugin path fails both Fortran tasks; both C++ tasks use a valid plugin path and succeed after 40 updates
   * - `miaow_example_03_dynamic_bmi_invalid_tolerance <https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/dynamic_invalid_tolerance.json>`_
     - A 1-second clock tolerance exceeds half the 0.25-second timestep; all four tasks fail with zero updates
   * - `miaow_example_03_dynamic_bmi_negative_tolerance <https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/dynamic_negative_tolerance.json>`_
     - A clock tolerance of -1 second fails validation; all four tasks fail with zero updates
   * - `miaow_example_03_dynamic_bmi_tolerance_boundary <https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/dynamic_tolerance_boundary.json>`_
     - A clock tolerance of 0.125 seconds equals half the 0.25-second timestep; all four tasks fail with zero updates
   * - `miaow_example_03_dynamic_bmi_overshoot <https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/dynamic_overshoot.json>`_
     - With end time 0.1 seconds, all four tasks fail after one 0.25-second update with ``update passed the end time``
   * - `miaow_example_07_job_controller <https://github.com/uofs-simlab/miaow-framework/blob/main/examples/07_job_controller/CMakeLists.txt>`_
     - Starting from 1, three Heron estimates match 1.5, 17/12, and 577/408 within ``1e-14``; the final residual ``|x^2 - 2|`` is at most ``1e-5``
   * - `miaow_example_07_job_controller_non_convergence <https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/controller_non_convergence.json>`_
     - With the iteration limit set to 1, the controller reports non-convergence after its first iteration; the program exits with code 1 and saves no job record
   * - `miaow_example_07_job_controller_small_values <https://github.com/uofs-simlab/miaow-framework/blob/main/tests/cases/controller_small_values.json>`_
     - For radicand ``2e-16``, saved estimates match ``1e-8 * [1.5, 17/12, 577/408]`` within ``1e-14`` relative error; the final residual is positive and at most ``1e-21``

Library installation build
--------------------------

The following example configures a build without tests and selects ``$HOME/.local`` as an example installation prefix. Any writable installation path can be supplied through ``CMAKE_INSTALL_PREFIX``:

.. code-block:: sh

   cmake -S . -B build \
       -DBUILD_TESTING=OFF \
       -DCMAKE_INSTALL_PREFIX="$HOME/.local"

   cmake --build build
   cmake --install build

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

The MIAOW package locates the required CAF components when ``find_package(miaow)`` runs. CAF must be discoverable through the system installation, ``CMAKE_PREFIX_PATH``, or the downstream project's CMake toolchain. nlohmann/json is a private header-only build dependency and is not required by installed consumers. The installed procedural BMI bridge depends on a compatible Fortran runtime library, such as ``libgfortran`` when the bridge is built with GNU Fortran. This runtime library must remain available when an application that links to MIAOW runs, but compiling the downstream project does not require a Fortran compiler.

.. note::

   Configure ``CMAKE_INSTALL_PREFIX`` before building so ``bmi_subprocess_executable_path()`` can locate the installed worker. Changing only ``cmake --install build --prefix <path>`` does not update this lookup. If you relocate the installation, reconfigure and rebuild MIAOW or pass the installed worker path directly to ``SimulationUnitRegistry::add_subprocess()``.

.. _build-options:

Build options
-------------

The following top-level CMake options are available:

.. list-table::
   :header-rows: 1
   :widths: 35 30 45

   * - Flag
     - Default
     - Description
   * - ``-DBUILD_TESTING``
     - ``ON``
     - Build the regression checker and register tests for enabled examples in a top-level repository build
   * - ``-DBUILD_EXAMPLES``
     - ``OFF``
     - Build selected example programs
   * - ``-DMIAOW_EXAMPLES``
     - | ``00_hello;01_heat;``
       | ``03_dynamic_bmi;``
       | ``07_job_controller``
     - Semicolon-separated list of examples to build when ``BUILD_EXAMPLES`` is enabled
   * - ``-DMIAOW_BOOST_PROVIDER``
     - ``auto``
     - Boost.Test provider: ``system``, ``fetch``, or ``auto``; used only for the example regression checker
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

The generated API documentation uses `Doxygen <https://www.doxygen.nl/>`_ for C++ documentation-comment extraction and `Sphinx <https://www.sphinx-doc.org/>`_ with `Breathe <https://breathe.readthedocs.io/>`_ for HTML rendering. Doxygen must be installed separately and available on ``PATH``.

The following commands create a dedicated Python environment and build the documentation:

.. code-block:: sh

   python3 -m venv docs/.venv
   docs/.venv/bin/pip install -r docs/requirements.txt
   docs/.venv/bin/sphinx-build -E -n -b html -d docs/_build/doctrees docs docs/_build/html

The generated HTML entry point is ``docs/_build/html/index.html``.

Building and installation
=========================

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

CI runs regression tests through the four default examples, including inputs that exercise failure paths. To run the same suite locally:

.. code-block:: sh

   cmake -S . -B build -DBUILD_EXAMPLES=ON \
       -DMIAOW_EXAMPLES="00_hello;01_heat;03_dynamic_bmi;07_job_controller"
   cmake --build build
   ctest --test-dir build -L regression --output-on-failure

The heat regressions use the pinned CSDMS C++ and Fortran models. CMake locates or fetches their sources for examples 01 and 03; use ``MIAOW_HEAT_MODEL_ROOT``, ``MIAOW_DYNAMIC_BMI_CXX_MODEL_ROOT``, and ``MIAOW_DYNAMIC_BMI_FORTRAN_MODEL_ROOT`` to supply local checkouts.

The regressions execute the actual example programs with a separate output directory for each case under the build tree. The CMake runner checks the expected exit code, then selects a Boost.Test case in ``tests/example_regression_checks.cpp`` to inspect saved job records. Failure cases in ``tests/cases`` supply example inputs, expected task statuses and messages, and any runtime timeout overrides. Errors that prevent a job record must produce the expected diagnostic and leave no record. A crash or an unrelated non-zero exit does not satisfy these checks.

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Test
     - Behaviour protected
   * - ``miaow_example_00_hello``
     - Both messages, task identities, feature metadata, and dispatch metadata survive execution and persistence
   * - ``miaow_example_01_heat``
     - The static C++ BMI model completes its configured 120 updates
   * - ``miaow_example_03_dynamic_bmi``
     - Dynamic C++ and Fortran models each complete 40 updates, both in-process and in fresh subprocesses
   * - ``miaow_example_07_job_controller``
     - Controller feedback produces the three expected Heron iterates and reaches the configured residual tolerance
   * - ``miaow_example_00_hello_*``
     - Failed tasks and subsequent successful work; unknown simulation units; invalid task names; task and job timeouts, including queued-task dispatch metadata
   * - ``miaow_example_03_dynamic_bmi_*``
     - Missing C++ and Fortran plugins, plus cleanup after rejecting an excessive clock tolerance, both in-process and in subprocesses
   * - ``miaow_example_07_job_controller_*``
     - Controller non-convergence reaches the caller; small estimates and residuals retain their numerical precision in saved output

The heat checks protect BMI execution and clock handling; they do not compare temperature fields. The example models do not return those fields through a whole-run task. Scientific model validation should compare selected native output values against reviewed references with explicit numerical tolerances. The suite does not separately test CAF serialization, raw BMI value access, non-second model clocks, finalization returning failure, or every controller lifecycle and scheduling configuration.

The PSO example is a manual scheduling demonstration: its wall-clock budget, randomized delays, and asynchronous execution make its trajectory unsuitable as a fixed numerical reference. LibTrixi and FUSE smoke tests are labelled ``optional`` and are registered only when those examples and their dependencies are selected. SUMMA scientific regressions require a short dataset and reviewed native-output references; they are outside the routine suite.

Library installation build
--------------------------

The following example configures a build without tests and selects ``$HOME/.local`` as an example installation prefix. Any writable installation path can be supplied through ``CMAKE_INSTALL_PREFIX``:

.. code-block:: sh

   cmake -S . -B build \
       -DBUILD_TESTING=OFF \
       -DCMAKE_INSTALL_PREFIX="$HOME/.local"

   cmake --build build
   cmake --install build

Building MIAOW from source requires C++20 and Fortran compilers. The completed installation can be consumed by a C++-only project without enabling Fortran or requiring a Fortran compiler.

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

Building and installation
=========================

Prerequisites
-------------

MIAOW requires `CMake <https://cmake.org/>`_ 3.21 or newer, a C++20 compiler, and a Fortran compiler. The Boost and CAF provider options specify where CMake obtains these dependencies: from packages already installed on the system, by fetching and building them from source, or by trying the system first and fetching them if necessary. See :ref:`build-options` for a description of the available provider settings.

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

The default development build includes the framework tests and does not build examples.

The complete test suite runs with the following command:

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

   cmake_minimum_required(VERSION 3.21)

   project(my_program LANGUAGES CXX)

   find_package(miaow CONFIG REQUIRED)

   add_executable(my_program main.cpp)
   target_link_libraries(my_program PRIVATE miaow::miaow)

The imported target supplies the installed MIAOW include directory, requests C++20, and links the required CAF, Boost, and procedural BMI bridge libraries. Source files can include ``miaow.hpp``, ``miaow_bmi.hpp``, and ``miaow_io.hpp`` as needed.

A custom subprocess worker is another executable linked against ``miaow::miaow``:

.. code-block:: cmake

   add_executable(my_worker worker.cpp)
   target_link_libraries(my_worker PRIVATE miaow::miaow)

When the selected installation prefix is outside CMake's normal search locations, the downstream configuration supplies the same path through ``CMAKE_PREFIX_PATH``:

.. code-block:: sh

   cmake -S . -B build -DCMAKE_PREFIX_PATH="$HOME/.local"
   cmake --build build

The MIAOW package locates the required CAF and Boost components when ``find_package(miaow)`` runs. These dependencies must be discoverable through the system installation, ``CMAKE_PREFIX_PATH``, or the downstream project's CMake toolchain. The installed procedural BMI bridge depends on a compatible Fortran runtime library, such as ``libgfortran`` when the bridge is built with GNU Fortran. This runtime library must remain available when an application that links to MIAOW runs, but compiling the downstream project does not require a Fortran compiler.

.. note::

   When MIAOW is configured, CMake supplies ``MIAOW_SUBPROCESS_PATH`` (the absolute build-tree path to ``miaow_subprocess``) and ``MIAOW_INSTALLED_SUBPROCESS_PATH`` (the absolute expected installed path derived from ``CMAKE_INSTALL_PREFIX``) as preprocessor definitions while compiling ``src/bmi/execute.cpp``. MIAOW's ``bmi_subprocess_executable_path()`` function returns ``MIAOW_SUBPROCESS_PATH`` when that executable exists, otherwise returns ``MIAOW_INSTALLED_SUBPROCESS_PATH`` when that executable exists, and throws if neither executable exists. Because ``cmake --install build --prefix <path>`` changes the installation destination without updating ``MIAOW_INSTALLED_SUBPROCESS_PATH``, downstream users should configure MIAOW with the correct ``-DCMAKE_INSTALL_PREFIX=<path>`` before building. To switch the install directory, users should reconfigure and rebuild MIAOW or pass the installed executable path directly to ``SimulationUnitRegistry::add_subprocess()`` when registering the subprocess executable, rather than using the default path returned by ``bmi_subprocess_executable_path()``.

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
     - Build the framework test executables in a top-level repository build
   * - ``-DBUILD_EXAMPLES``
     - ``OFF``
     - Build selected example programs
   * - ``-DMIAOW_EXAMPLES``
     - | ``00_hello;01_heat;``
       | ``02_soil_freeze_thaw;``
       | ``03_dynamic_bmi;``
       | ``07_job_controller``
     - Semicolon-separated list of examples to build when ``BUILD_EXAMPLES`` is enabled
   * - ``-DMIAOW_BOOST_PROVIDER``
     - ``auto``
     - Boost provider: ``system``, ``fetch``, or ``auto``
   * - ``-DMIAOW_BOOST_VERSION``
     - ``1.86.0``
     - Boost version used by the ``FetchContent`` fallback
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

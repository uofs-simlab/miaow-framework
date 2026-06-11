Development
=================

Build and test
--------------

Configure and build from the repository root:

.. code-block:: sh

   cmake -S . -B build
   cmake --build build

Run all tests:

.. code-block:: sh

   ctest --test-dir build --output-on-failure

Run a single test by name:

.. code-block:: sh

   ctest --test-dir build -R <test_name> --output-on-failure

.. _build-options:

Build options
-------------

These are the top-level CMake options:

.. list-table::
   :header-rows: 1
   :widths: 35 30 45

   * - Flag
     - Default
     - Description
   * - ``-DBUILD_EXAMPLES``
     - ``ON``
     - Build all example programs
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
     - Default base directory where external model sources are resolved or
       fetched
   * - ``-DMIAOW_ISO_C_FORTRAN_BMI_ROOT``
     - empty
     - Optional explicit path to a valid local
       ``NOAA-OWP/iso_c_fortran_bmi`` checkout

When ``MIAOW_ISO_C_FORTRAN_BMI_ROOT`` is empty, CMake looks for
``iso_c_fortran_bmi`` under ``MIAOW_DEFAULT_MODEL_BASE_DIR`` and fetches it
into that location if missing.

Provider values are interpreted as follows:

- ``system``: require ``find_package(...)`` to provide the dependency
- ``fetch``: skip ``find_package(...)`` and use ``FetchContent``
- ``auto``: try ``find_package(...)``, then use ``FetchContent`` if not found

A Conan-based CI-style setup uses these commands:

.. code-block:: sh

   conan install . --output-folder=build/conan --build=missing
   cmake -S . -B build \
     -DMIAOW_BOOST_PROVIDER=system \
     -DMIAOW_CAF_PROVIDER=system
   cmake --build build --config Release

Documentation build
-------------------

Generated API documentation uses Doxygen for C++ docstring extraction and
Sphinx with Breathe for HTML rendering. Doxygen must be installed separately
and available on ``PATH``.

Build the docs from a dedicated Python environment:

.. code-block:: sh

   python3 -m venv docs/.venv
   docs/.venv/bin/pip install -r docs/requirements.txt
   rm -rf docs/_build/html docs/_build/doctrees
   docs/.venv/bin/sphinx-build -b html -d docs/_build/doctrees docs docs/_build/html

The generated HTML entry point is ``docs/_build/html/index.html``.

Code structure
--------------

.. code-block:: text

   include/
     miaow.hpp              # public runtime/task API
     miaow_bmi.hpp          # BMI simulation-unit wrapper and realization contracts
     miaow_io.hpp           # public I/O, diagnostics, and CLI API

   src/
     core/
       runtime.cpp          # Runtime submit path and output tree setup
       actors.cpp           # orchestrator/job/worker actor behaviour
       process.cpp          # subprocess launch, connection, and process reuse
       internal.hpp         # core-private actor declarations and JobState

     bmi/
       execute.cpp          # BMI pipeline lifecycle execution
       dynamic.cpp          # DynamicBmiModel adapter for C++/Fortran plugins
       forcing.cpp          # streaming external BMI forcing CSV source
       io.cpp               # BMI config, artifact, and output CSV helpers
       pipeline.cpp         # ordered BMI module pipeline and coupling application
       realization.cpp      # BMI realization parsing, task, and registration helpers
       subprocess.cpp       # BMI subprocess executable entry point
       internal.hpp         # BMI-private forcing and output declarations

     io/
       cli.cpp              # runtime command-line option parsing
       csv.cpp              # CSV parsing plus shared forcing helper utilities
       diagnostics.cpp      # diagnostics formatting and printing helpers
       job_record.cpp       # runtime job record output writing
       internal.hpp         # IO-private declarations and helpers

   examples/
     00_hello/
     01_heat/
     02_soil_freeze_thaw/
     03_dynamic_bmi/
     04_summa/

Namespace layout
----------------

- Public API: ``miaow``
- Core internals: ``miaow::core``
- BMI internals: ``miaow::bmi``
- Internal I/O helpers: ``miaow::io``
- The external `BMI C++ API <https://github.com/csdms/bmi-cxx/tree/master>`_
  is available through the ``miaow::bmi_api = ::bmi`` alias.

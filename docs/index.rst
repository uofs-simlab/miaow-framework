MIAOW: Model-Independent Actor Orchestration Workflows for Scientific Computing
===============================================================================

Managing concurrent numerical simulation tasks with dynamic workloads and
heterogeneous hardware can feel like herding cats.
`MIAOW <https://github.com/uofs-simlab/miaow-framework>`_ embraces that reality
by using actor-based asynchronous orchestration so tasks run when they are
ready, maximizing utilization across available resources.

MIAOW targets the `C++20 standard <https://en.cppreference.com/w/cpp/20.html>`_,
uses the `C++ Actor Framework (CAF) <https://www.actor-framework.org/>`_ as its
actor backend, relies on `Boost <https://www.boost.org/>`_ for process
management and JSON support, and includes C++/Fortran
`Basic Model Interface (BMI) <https://bmi.csdms.io/en/stable/>`_ integration
paths.

Quick start
-----------

See the repository README for platform-specific dependency installation. Build
the core library and subprocess runner:

.. code-block:: sh

   cmake -S . -B build -DBUILD_EXAMPLES=OFF
   cmake --build build

Build the default examples and run the test suite:

.. code-block:: sh

   cmake -S . -B build -DBUILD_EXAMPLES=ON
   cmake --build build
   ctest --test-dir build --output-on-failure

Documentation
-------------

.. toctree::
   :maxdepth: 2

   architecture
   bmi
   examples
   development
   api

Authors and funding
-------------------

MIAOW was developed at the University of Saskatchewan by
`Tristan Montoya <https://tjbmontoya.com/>`_ and
`Raymond Spiteri <https://www.cs.usask.ca/people/faculty%20profiles/ray-spiteri.php>`_,
with funding support from the `Cooperative Institute for Research to Operations
in Hydrology (CIROH) <https://ciroh.ua.edu/>`_.

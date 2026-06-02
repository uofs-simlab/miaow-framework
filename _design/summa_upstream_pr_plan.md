# SUMMA upstream PR plan

## Status
Planned

## Scope
This note describes two small upstream pull requests against
`CH-Earth/summa:develop_sundials`. The goal is to make SUMMA's NextGen BMI
support usable by MIAOW without generated source patching or a synthetic wrapper
source tree.

The pull requests should stay separate because they affect different review
surfaces:

1. Runtime behaviour for BMI forcing and finalization.
2. CMake layout support for direct NextGen builds.

## Runtime behaviour PR
This pull request fixes the BMI execution path without changing SUMMA's build
layout.

In `build/source/engine/read_force.f90`, the `NGEN_FORCING_ACTIVE` path should
use BMI-provided forcing values and avoid native NetCDF forcing reads. The
implementation should:

- Call `createForcingTimeData(currentJulDay, time_data, err, message)`.
- Set `dataJulDay = currentJulDay`.
- Skip file opening, `fulltimeVec`, `fullforcingStruct`, `forcFileInfo`, and
  `readForcingData`.
- Preserve common time bookkeeping after forcing time data is created, including
  missing time checks, `fracJulDay`, `yearLength`, and forcing structure time
  assignment.

In `build/source/driver/summa_util.f90`, successful `NGEN_ACTIVE`
finalization should return after closing open output files instead of calling
Fortran `stop`. Fatal error behaviour should be unchanged: nonzero errors should
still print the fatal message and stop with nonzero status.

## CMake layout PR
This pull request makes the NextGen BMI build work from a normal SUMMA checkout
while preserving the existing wrapper layout.

In `build/CMakeLists.txt`, add a cache path for the BMI bridge source, for
example `ISO_C_FORTRAN_BMI_ROOT`. The default should preserve the current
wrapper assumption by resolving to `../iso_c_fortran_bmi`. If the resolved path
does not contain `CMakeLists.txt`, configuration should fail with a clear
message.

The NextGen build should resolve the SUMMA source root from the source layout:

- If `${CMAKE_CURRENT_SOURCE_DIR}/summa/build/source/driver/summa_bmi.f90`
  exists, use the current wrapper layout.
- Otherwise, use `${CMAKE_CURRENT_SOURCE_DIR}/..` so
  `cmake -S <summa>/build` works from a direct checkout.

Use the resolved source root for `F_MASTER`, `CMAKE_MODULE_PATH`,
`CMAKE_PREFIX_PATH`, and the `summabmi.pc.in` input path. Do not change target
names, compiler flags, install destinations, dependency discovery, or source
lists.

## Verification
For the runtime behaviour PR, build SUMMA with `USE_NEXTGEN=ON`. In MIAOW,
temporarily remove the generated source patch step while keeping the current
wrapper staging, then run:

```sh
cmake -S . -B build -DMIAOW_SUMMA_GIT_TAG=<runtime-pr-commit>
cmake --build build
ctest --test-dir build -R miaow_example_04_summa --output-on-failure
```

The expected result is no forcing segfault, no premature subprocess exit during
`Finalize()`, and complete JSON and CSV output artifacts.

For the CMake layout PR, verify both source layouts:

```sh
cmake -S /path/to/summa/build -B /tmp/summa-build \
  -DUSE_NEXTGEN=ON \
  -DISO_C_FORTRAN_BMI_ROOT=/path/to/iso_c_fortran_bmi
cmake --build /tmp/summa-build --target summabmi
```

The existing wrapper-style NextGen build should still work. MIAOW should also
be able to stop copying SUMMA into a wrapper tree by pointing its external build
at `${summa_source_dir}/build` and passing `ISO_C_FORTRAN_BMI_ROOT`.

## Assumptions
The runtime behaviour PR is the priority because it removes the generated
source patching. The CMake layout PR can follow after the runtime changes are
merged or can be rebased onto the same upstream branch.

The upstream pull requests should not add broad test infrastructure unless
SUMMA maintainers request it. Verification should rely on the existing NextGen
build path and MIAOW's SUMMA smoke test.

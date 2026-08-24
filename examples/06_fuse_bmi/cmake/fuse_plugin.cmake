# FUSE Fortran BMI plugin for MIAOW -- uofs-simlab/fuse

# Allow a local checkout to override the pinned FUSE source
set(MIAOW_FUSE_MODEL_ROOT "" CACHE PATH
    "Path to a local uofs-simlab/fuse source tree")
set(fuse_git_repository "https://github.com/uofs-simlab/fuse.git")
set(fuse_git_tag "d73d24bb8678908973ca6f82f9d6eb6405cbb85f")

# Choose the FUSE path, fetching the pinned source when it is absent
miaow_resolve_model_root(fuse_root
    MODEL_ROOT "${MIAOW_FUSE_MODEL_ROOT}"
    MODEL_LOCAL_SUBDIR "fuse"
    MODEL_LABEL "fuse"
)

if(NOT EXISTS "${fuse_root}")
    get_filename_component(fuse_source_parent "${fuse_root}" DIRECTORY)
    file(MAKE_DIRECTORY "${fuse_source_parent}")

    include(FetchContent)
    FetchContent_Populate(
        fuse_source
        GIT_REPOSITORY "${fuse_git_repository}"
        GIT_TAG "${fuse_git_tag}"
        SOURCE_DIR "${fuse_root}"
        BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/fuse-source-build"
    )
endif()

# Validate the files required to build the FUSE BMI plugin
if(NOT EXISTS "${fuse_root}/bmi/fuse_bmi.f90")
    if(MIAOW_FUSE_MODEL_ROOT)
        message(FATAL_ERROR
            "MIAOW_FUSE_MODEL_ROOT must point to a uofs-simlab/fuse source tree")
    else()
        message(FATAL_ERROR
            "Fetched uofs-simlab/fuse source tree is missing bmi/fuse_bmi.f90")
    endif()
endif()
set(fuse_source_dir "${fuse_root}/build/FUSE_SRC")
if(NOT EXISTS "${fuse_source_dir}/FUSE_HOOK/kinds_dmsl_kit_FUSE.f90")
    message(FATAL_ERROR
        "FUSE source tree is missing build/FUSE_SRC")
endif()

set(FUSE_SOURCES
    "${fuse_source_dir}/FUSE_HOOK/kinds_dmsl_kit_FUSE.f90"
    "${fuse_source_dir}/FUSE_HOOK/utilities_dmsl_kit_FUSE.f90"
    "${fuse_source_dir}/FUSE_HOOK/fuse_fileManager.f90"
    "${fuse_source_dir}/FUSE_NR/nrtype.f90"
    "${fuse_source_dir}/FUSE_NR/nr.f90"
    "${fuse_source_dir}/FUSE_NR/nrutil.f90"
    "${fuse_source_dir}/FUSE_ENGINE/model_defn.f90"
    "${fuse_source_dir}/FUSE_ENGINE/model_defnames.f90"
    "${fuse_source_dir}/FUSE_ENGINE/multiconst.f90"
    "${fuse_source_dir}/FUSE_ENGINE/multiforce.f90"
    "${fuse_source_dir}/FUSE_ENGINE/multibands.f90"
    "${fuse_source_dir}/FUSE_ENGINE/multiparam.f90"
    "${fuse_source_dir}/FUSE_ENGINE/multistate.f90"
    "${fuse_source_dir}/FUSE_ENGINE/multi_flux.f90"
    "${fuse_source_dir}/FUSE_ENGINE/multiroute.f90"
    "${fuse_source_dir}/FUSE_ENGINE/multistats.f90"
    "${fuse_source_dir}/FUSE_ENGINE/model_numerix.f90"
    "${fuse_source_dir}/FUSE_TIME/time_io.f90"
    "${fuse_source_dir}/FUSE_ENGINE/metaoutput.f90"
    "${fuse_source_dir}/FUSE_ENGINE/metaparams.f90"
    "${fuse_source_dir}/FUSE_ENGINE/meta_stats.f90"
    "${fuse_source_dir}/FUSE_ENGINE/selectmodl.f90"
    "${fuse_source_dir}/FUSE_ENGINE/putpar_str.f90"
    "${fuse_source_dir}/FUSE_ENGINE/getpar_str.f90"
    "${fuse_source_dir}/FUSE_ENGINE/par_insert.f90"
    "${fuse_source_dir}/FUSE_ENGINE/parextract.f90"
    "${fuse_source_dir}/FUSE_ENGINE/varextract.f90"
    "${fuse_source_dir}/FUSE_ENGINE/sumextract.f90"
    "${fuse_source_dir}/FUSE_ENGINE/str_2_xtry.f90"
    "${fuse_source_dir}/FUSE_ENGINE/xtry_2_str.f90"
    "${fuse_source_dir}/FUSE_NR/ludcmp.f90"
    "${fuse_source_dir}/FUSE_NR/lubksb.f90"
    "${fuse_source_dir}/FUSE_NR/svbksb.f90"
    "${fuse_source_dir}/FUSE_NR/svdcmp.f90"
    "${fuse_source_dir}/FUSE_NR/pythag.f90"
    "${fuse_source_dir}/FUSE_NR/gammln.f90"
    "${fuse_source_dir}/FUSE_NR/gammp.f90"
    "${fuse_source_dir}/FUSE_NR/gcf.f90"
    "${fuse_source_dir}/FUSE_NR/gser.f90"
    "${fuse_source_dir}/FUSE_ENGINE/mod_derivs.f90"
    "${fuse_source_dir}/FUSE_ENGINE/update_swe.f90"
    "${fuse_source_dir}/FUSE_ENGINE/qrainerror.f90"
    "${fuse_source_dir}/FUSE_ENGINE/qsatexcess.f90"
    "${fuse_source_dir}/FUSE_ENGINE/evap_upper.f90"
    "${fuse_source_dir}/FUSE_ENGINE/evap_lower.f90"
    "${fuse_source_dir}/FUSE_ENGINE/qinterflow.f90"
    "${fuse_source_dir}/FUSE_ENGINE/qpercolate.f90"
    "${fuse_source_dir}/FUSE_ENGINE/q_baseflow.f90"
    "${fuse_source_dir}/FUSE_ENGINE/q_misscell.f90"
    "${fuse_source_dir}/FUSE_ENGINE/logismooth.f90"
    "${fuse_source_dir}/FUSE_ENGINE/mstate_eqn.f90"
    "${fuse_source_dir}/FUSE_ENGINE/fix_states.f90"
    "${fuse_source_dir}/FUSE_ENGINE/meanfluxes.f90"
    "${fuse_source_dir}/FUSE_ENGINE/wgt_fluxes.f90"
    "${fuse_source_dir}/FUSE_ENGINE/updatstate.f90"
    "${fuse_source_dir}/FUSE_ENGINE/q_overland.f90"
    "${fuse_source_dir}/FUSE_ENGINE/interfaceb.f90"
    "${fuse_source_dir}/FUSE_ENGINE/limit_xtry.f90"
    "${fuse_source_dir}/FUSE_ENGINE/viol_state.f90"
    "${fuse_source_dir}/FUSE_ENGINE/fuse_deriv.f90"
    "${fuse_source_dir}/FUSE_ENGINE/fmin.f90"
    "${fuse_source_dir}/FUSE_ENGINE/fdjac_ode.f90"
    "${fuse_source_dir}/FUSE_ENGINE/flux_deriv.f90"
    "${fuse_source_dir}/FUSE_ENGINE/disaggflux.f90"
    "${fuse_source_dir}/FUSE_ENGINE/fuse_sieul.f90"
    "${fuse_source_dir}/FUSE_ENGINE/newtoniter.f90"
    "${fuse_source_dir}/FUSE_ENGINE/lnsrch.f90"
    "${fuse_source_dir}/FUSE_ENGINE/ascii_util.f90"
    "${fuse_source_dir}/FUSE_ENGINE/uniquemodl.f90"
    "${fuse_source_dir}/FUSE_ENGINE/getnumerix.f90"
    "${fuse_source_dir}/FUSE_ENGINE/getparmeta.f90"
    "${fuse_source_dir}/FUSE_ENGINE/assign_stt.f90"
    "${fuse_source_dir}/FUSE_ENGINE/assign_flx.f90"
    "${fuse_source_dir}/FUSE_ENGINE/assign_par.f90"
    "${fuse_source_dir}/FUSE_ENGINE/adjust_stt.f90"
    "${fuse_source_dir}/FUSE_ENGINE/par_derive.f90"
    "${fuse_source_dir}/FUSE_ENGINE/bucketsize.f90"
    "${fuse_source_dir}/FUSE_ENGINE/mean_tipow.f90"
    "${fuse_source_dir}/FUSE_ENGINE/qbsaturatn.f90"
    "${fuse_source_dir}/FUSE_ENGINE/qtimedelay.f90"
    "${fuse_source_dir}/FUSE_ENGINE/init_stats.f90"
    "${fuse_source_dir}/FUSE_ENGINE/init_state.f90"
    "${fuse_source_dir}/FUSE_ENGINE/metrics.f90"
    "${fuse_source_dir}/FUSE_ENGINE/conv_funcs.f90"
    "${fuse_source_dir}/FUSE_ENGINE/force_info.f90"
    "${fuse_source_dir}/FUSE_ENGINE/clrsky_rad.f90"
    "${fuse_source_dir}/FUSE_ENGINE/getPETgrid.f90"
    "${fuse_source_dir}/FUSE_ENGINE/get_mbands.f90"
    "${fuse_source_dir}/FUSE_ENGINE/get_time_indices.f90"
    "${fuse_source_dir}/FUSE_ENGINE/initfluxes.f90"
    "${fuse_source_dir}/FUSE_ENGINE/set_all.f90"
    "${fuse_source_dir}/FUSE_ENGINE/ode_int.f90"
    "${fuse_source_dir}/FUSE_ENGINE/fuse_solve.f90"
    "${fuse_source_dir}/FUSE_ENGINE/comp_stats.f90"
    "${fuse_source_dir}/FUSE_ENGINE/mean_stats.f90"
    "${fuse_source_dir}/FUSE_NETCDF/handle_err.f90"
    "${fuse_source_dir}/FUSE_NETCDF/extractor.f90"
    "${fuse_source_dir}/FUSE_NETCDF/juldayss.f90"
    "${fuse_source_dir}/FUSE_NETCDF/caldatss.f90"
    "${fuse_source_dir}/FUSE_NETCDF/get_gforce.f90"
    "${fuse_source_dir}/FUSE_NETCDF/get_smodel.f90"
    "${fuse_source_dir}/FUSE_NETCDF/get_fparam.f90"
    "${fuse_source_dir}/FUSE_NETCDF/def_params.f90"
    "${fuse_source_dir}/FUSE_NETCDF/def_output.f90"
    "${fuse_source_dir}/FUSE_NETCDF/def_sstats.f90"
    "${fuse_source_dir}/FUSE_NETCDF/put_params.f90"
    "${fuse_source_dir}/FUSE_NETCDF/put_output.f90"
    "${fuse_source_dir}/FUSE_NETCDF/put_sstats.f90"
    "${fuse_source_dir}/FUSE_DMSL/fuse_metric.f90"
    "${fuse_root}/bmi/fuse_bmi.f90"
    "${CMAKE_CURRENT_LIST_DIR}/../register_bmi_fuse.f90"
)

# Build shared library directly (avoids cmake_parse_arguments limits)
add_library(miaow_dynamic_fuse_plugin SHARED ${FUSE_SOURCES})

# Fortran compiler flags: -cpp required (CH-Earth sources use #ifdef __MPI__)
target_compile_options(miaow_dynamic_fuse_plugin PRIVATE
    $<$<COMPILE_LANGUAGE:Fortran>:
        -ffree-line-length-none
        -cpp
        -O2
    >
)

# Link the iso_c_bmi bridge (provides procedural BMI entry points)
target_link_libraries(miaow_dynamic_fuse_plugin PRIVATE iso_c_bmi)

# Fortran module output directory
set_target_properties(miaow_dynamic_fuse_plugin PROPERTIES
    Fortran_MODULE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fuse_mod"
)

# NetCDF (FUSE's external dependency)
find_package(PkgConfig REQUIRED)
pkg_check_modules(NETCDF_FORTRAN REQUIRED IMPORTED_TARGET netcdf-fortran)
target_link_libraries(miaow_dynamic_fuse_plugin PRIVATE
    PkgConfig::NETCDF_FORTRAN
)

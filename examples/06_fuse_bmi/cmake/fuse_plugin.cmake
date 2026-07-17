# FUSE Fortran BMI plugin for MIAOW
set(FUSE_SRC "/home/mandana/fuse/build/FUSE_SRC")
set(FUSE_ROOT "/home/mandana/fuse")

set(FUSE_SOURCES
    "${FUSE_SRC}/FUSE_HOOK/kinds_dmsl_kit_FUSE.f90"
    "${FUSE_SRC}/FUSE_HOOK/utilities_dmsl_kit_FUSE.f90"
    "${FUSE_SRC}/FUSE_HOOK/fuse_fileManager.f90"
    "${FUSE_SRC}/FUSE_NR/nrtype.f90"
    "${FUSE_SRC}/FUSE_NR/nr.f90"
    "${FUSE_SRC}/FUSE_NR/nrutil.f90"
    "${FUSE_SRC}/FUSE_ENGINE/calib_control.f90"
    "${FUSE_SRC}/FUSE_ENGINE/model_defn.f90"
    "${FUSE_SRC}/FUSE_ENGINE/model_defnames.f90"
    "${FUSE_SRC}/FUSE_ENGINE/multiconst.f90"
    "${FUSE_SRC}/FUSE_ENGINE/multiforce.f90"
    "${FUSE_SRC}/FUSE_ENGINE/multibands.f90"
    "${FUSE_SRC}/FUSE_ENGINE/multiparam.f90"
    "${FUSE_SRC}/FUSE_ENGINE/multistate.f90"
    "${FUSE_SRC}/FUSE_ENGINE/multi_flux.f90"
    "${FUSE_SRC}/FUSE_ENGINE/multiroute.f90"
    "${FUSE_SRC}/FUSE_ENGINE/multistats.f90"
    "${FUSE_SRC}/FUSE_ENGINE/eval_metrics.f90"
    "${FUSE_SRC}/FUSE_ENGINE/model_numerix.f90"
    "${FUSE_SRC}/FUSE_TIME/time_io.f90"
    "${FUSE_SRC}/FUSE_ENGINE/metaoutput.f90"
    "${FUSE_SRC}/FUSE_ENGINE/metaparams.f90"
    "${FUSE_SRC}/FUSE_ENGINE/meta_stats.f90"
    "${FUSE_SRC}/FUSE_ENGINE/selectmodl.f90"
    "${FUSE_SRC}/FUSE_ENGINE/putpar_str.f90"
    "${FUSE_SRC}/FUSE_ENGINE/getpar_str.f90"
    "${FUSE_SRC}/FUSE_ENGINE/par_insert.f90"
    "${FUSE_SRC}/FUSE_ENGINE/parextract.f90"
    "${FUSE_SRC}/FUSE_ENGINE/varextract.f90"
    "${FUSE_SRC}/FUSE_ENGINE/sumextract.f90"
    "${FUSE_SRC}/FUSE_ENGINE/str_2_xtry.f90"
    "${FUSE_SRC}/FUSE_ENGINE/xtry_2_str.f90"
    "${FUSE_SRC}/FUSE_NR/ludcmp.f90"
    "${FUSE_SRC}/FUSE_NR/lubksb.f90"
    "${FUSE_SRC}/FUSE_NR/svbksb.f90"
    "${FUSE_SRC}/FUSE_NR/svdcmp.f90"
    "${FUSE_SRC}/FUSE_NR/pythag.f90"
    "${FUSE_SRC}/FUSE_NR/gammln.f90"
    "${FUSE_SRC}/FUSE_NR/gammp.f90"
    "${FUSE_SRC}/FUSE_NR/gcf.f90"
    "${FUSE_SRC}/FUSE_NR/gser.f90"
    "${FUSE_SRC}/FUSE_ENGINE/mod_derivs.f90"
    "${FUSE_SRC}/FUSE_ENGINE/update_swe.f90"
    "${FUSE_SRC}/FUSE_ENGINE/qrainerror.f90"
    "${FUSE_SRC}/FUSE_ENGINE/qsatexcess.f90"
    "${FUSE_SRC}/FUSE_ENGINE/evap_upper.f90"
    "${FUSE_SRC}/FUSE_ENGINE/evap_lower.f90"
    "${FUSE_SRC}/FUSE_ENGINE/qinterflow.f90"
    "${FUSE_SRC}/FUSE_ENGINE/qpercolate.f90"
    "${FUSE_SRC}/FUSE_ENGINE/q_baseflow.f90"
    "${FUSE_SRC}/FUSE_ENGINE/q_misscell.f90"
    "${FUSE_SRC}/FUSE_ENGINE/logismooth.f90"
    "${FUSE_SRC}/FUSE_ENGINE/mstate_eqn.f90"
    "${FUSE_SRC}/FUSE_ENGINE/fix_states.f90"
    "${FUSE_SRC}/FUSE_ENGINE/meanfluxes.f90"
    "${FUSE_SRC}/FUSE_ENGINE/wgt_fluxes.f90"
    "${FUSE_SRC}/FUSE_ENGINE/updatstate.f90"
    "${FUSE_SRC}/FUSE_ENGINE/q_overland.f90"
    "${FUSE_SRC}/FUSE_ENGINE/interfaceb.f90"
    "${FUSE_SRC}/FUSE_ENGINE/limit_xtry.f90"
    "${FUSE_SRC}/FUSE_ENGINE/viol_state.f90"
    "${FUSE_SRC}/FUSE_ENGINE/fuse_deriv.f90"
    "${FUSE_SRC}/FUSE_ENGINE/fmin.f90"
    "${FUSE_SRC}/FUSE_ENGINE/fdjac_ode.f90"
    "${FUSE_SRC}/FUSE_ENGINE/flux_deriv.f90"
    "${FUSE_SRC}/FUSE_ENGINE/disaggflux.f90"
    "${FUSE_SRC}/FUSE_ENGINE/fuse_sieul.f90"
    "${FUSE_SRC}/FUSE_ENGINE/newtoniter.f90"
    "${FUSE_SRC}/FUSE_ENGINE/lnsrch.f90"
    "${FUSE_SRC}/FUSE_ENGINE/ascii_util.f90"
    "${FUSE_SRC}/FUSE_ENGINE/uniquemodl.f90"
    "${FUSE_SRC}/FUSE_ENGINE/getnumerix.f90"
    "${FUSE_SRC}/FUSE_ENGINE/getparmeta.f90"
    "${FUSE_SRC}/FUSE_ENGINE/assign_stt.f90"
    "${FUSE_SRC}/FUSE_ENGINE/assign_flx.f90"
    "${FUSE_SRC}/FUSE_ENGINE/assign_par.f90"
    "${FUSE_SRC}/FUSE_ENGINE/adjust_stt.f90"
    "${FUSE_SRC}/FUSE_ENGINE/par_derive.f90"
    "${FUSE_SRC}/FUSE_ENGINE/bucketsize.f90"
    "${FUSE_SRC}/FUSE_ENGINE/mean_tipow.f90"
    "${FUSE_SRC}/FUSE_ENGINE/qbsaturatn.f90"
    "${FUSE_SRC}/FUSE_ENGINE/qtimedelay.f90"
    "${FUSE_SRC}/FUSE_ENGINE/init_stats.f90"
    "${FUSE_SRC}/FUSE_ENGINE/init_state.f90"
    "${FUSE_SRC}/FUSE_ENGINE/user_state.f90"
    "${FUSE_SRC}/FUSE_ENGINE/init_user_state.f90"
    "${FUSE_SRC}/FUSE_ENGINE/output_state.f90"
    "${FUSE_SRC}/FUSE_ENGINE/conv_funcs.f90"
    "${FUSE_SRC}/FUSE_ENGINE/force_info.f90"
    "${FUSE_SRC}/FUSE_ENGINE/clrsky_rad.f90"
    "${FUSE_SRC}/FUSE_ENGINE/getPETgrid.f90"
    "${FUSE_SRC}/FUSE_ENGINE/get_mbands.f90"
    "${FUSE_SRC}/FUSE_ENGINE/initfluxes.f90"
    "${FUSE_SRC}/FUSE_ENGINE/set_all.f90"
    "${FUSE_SRC}/FUSE_ENGINE/ode_int.f90"
    "${FUSE_SRC}/FUSE_ENGINE/fuse_solve.f90"
    "${FUSE_SRC}/FUSE_ENGINE/comp_stats.f90"
    "${FUSE_SRC}/FUSE_ENGINE/mean_stats.f90"
    "${FUSE_SRC}/FUSE_NETCDF/handle_err.f90"
    "${FUSE_SRC}/FUSE_NETCDF/extractor.f90"
    "${FUSE_SRC}/FUSE_NETCDF/juldayss.f90"
    "${FUSE_SRC}/FUSE_NETCDF/caldatss.f90"
    "${FUSE_SRC}/FUSE_NETCDF/get_gforce.f90"
    "${FUSE_SRC}/FUSE_NETCDF/get_smodel.f90"
    "${FUSE_SRC}/FUSE_NETCDF/get_fparam.f90"
    "${FUSE_SRC}/FUSE_NETCDF/def_params.f90"
    "${FUSE_SRC}/FUSE_NETCDF/def_output.f90"
    "${FUSE_SRC}/FUSE_NETCDF/def_sstats.f90"
    "${FUSE_SRC}/FUSE_NETCDF/put_params.f90"
    "${FUSE_SRC}/FUSE_NETCDF/put_output.f90"
    "${FUSE_SRC}/FUSE_NETCDF/put_sstats.f90"
    "${FUSE_SRC}/FUSE_DMSL/sobol.f90"
    "${FUSE_SRC}/FUSE_DMSL/fuse_rmse.f90"
    "${FUSE_SRC}/FUSE_DMSL/functn.f90"
    "${FUSE_ROOT}/fuse_bmi.f90"
    "${CMAKE_CURRENT_LIST_DIR}/../register_bmi_fuse.f90"
)

# Build shared library directly (avoids cmake_parse_arguments limits)
add_library(miaow_dynamic_fuse_plugin SHARED ${FUSE_SOURCES})

# Fortran compiler flags matching FUSE's own Makefile
target_compile_options(miaow_dynamic_fuse_plugin PRIVATE
    $<$<COMPILE_LANGUAGE:Fortran>:
        -ffree-line-length-none
        -fallow-argument-mismatch
        -std=legacy
        -g
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
pkg_check_modules(NETCDF_FORTRAN REQUIRED netcdf-fortran)
target_include_directories(miaow_dynamic_fuse_plugin PRIVATE
    ${NETCDF_FORTRAN_INCLUDE_DIRS}
    /usr/include
)
target_link_libraries(miaow_dynamic_fuse_plugin PRIVATE
    ${NETCDF_FORTRAN_LIBRARIES}
    netcdf
)

# Pre-compiled SCE object
target_link_libraries(miaow_dynamic_fuse_plugin PRIVATE
    "${FUSE_SRC}/FUSE_SCE/sce_16plus.o"
)

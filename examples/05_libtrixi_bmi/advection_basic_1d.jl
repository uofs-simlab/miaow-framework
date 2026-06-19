using LibTrixi
using Trixi
using OrdinaryDiffEqLowStorageRK
using Random

# Draw CFL numbers uniformly in log space for workload variationw
function randomized_cfl()
    cfl_min = parse(Float64, get(ENV, "MIAOW_LIBTRIXI_CFL_MIN", "1.0e-4"))
    cfl_max = parse(Float64, get(ENV, "MIAOW_LIBTRIXI_CFL_MAX", "1.0e0"))
    @assert cfl_min > 0.0 "MIAOW_LIBTRIXI_CFL_MIN must be positive"
    @assert cfl_max >= cfl_min "MIAOW_LIBTRIXI_CFL_MAX must be at least cfl_min"
    return exp(log(cfl_min) + rand() * (log(cfl_max) - log(cfl_min)))
end

# libtrixi loads this function by name when it initializes a simulation
function init_simstate()
    advection_velocity = 1.0
    equations = LinearScalarAdvectionEquation1D(advection_velocity)

    solver = DGSEM(polydeg = 3, surface_flux = flux_lax_friedrichs)

    mesh = TreeMesh(-1.0, 1.0;
                    initial_refinement_level = 4,
                    n_cells_max = 30_000,
                    periodicity = true)

    semi = SemidiscretizationHyperbolic(mesh, equations,
                                        initial_condition_convergence_test,
                                        solver;
                                        boundary_conditions =
                                            boundary_condition_periodic)

    ode = semidiscretize(semi, (0.0, 1.0))

    cfl = randomized_cfl()
    println(stderr, "libtrixi advection config: cfl = ", cfl)
    flush(stderr)

    stepsize_callback = StepsizeCallback(cfl = cfl)
    callbacks = CallbackSet(stepsize_callback)

    integrator = init(ode, CarpenterKennedy2N54(williamson_condition = false);
                      dt = 1.0,
                      save_everystep = false,
                      callback = callbacks)

    return SimulationState(semi, integrator)
end

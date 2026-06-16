using LibTrixi
using Trixi
using OrdinaryDiffEqLowStorageRK

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

    summary_callback = SummaryCallback()
    analysis_callback = AnalysisCallback(semi, interval = 100)
    save_solution = SaveSolutionCallback(interval = 100,
                                         solution_variables = cons2prim)
    stepsize_callback = StepsizeCallback(cfl = 1.6)
    callbacks = CallbackSet(summary_callback, analysis_callback,
                            save_solution, stepsize_callback)

    integrator = init(ode, CarpenterKennedy2N54(williamson_condition = false);
                      dt = 1.0,
                      save_everystep = false,
                      callback = callbacks)

    return SimulationState(semi, integrator)
end

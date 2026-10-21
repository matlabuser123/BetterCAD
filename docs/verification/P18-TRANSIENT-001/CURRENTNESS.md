# P18-TRANSIENT-001 — what a transient history is bound to, and when it stops being current

```text
SUBJECT:  the provenance of a TransientThermalSolution, the order of the checks before any arithmetic,
          and the lifecycle of a held result around a failed run
CODE:     TransientSolutionSource, staleReasons, currentTransientSolutionSource, requireCurrent,
          runTransient; P18-DATA-001's ThermalResultSlot and analysisState (unchanged)
```

## The source is a value, not a hash

```text
TransientSolutionSource
    system         the assembled system's own source: body, mesh stamp, node count, geometry revision,
                   mode, material id and revision, the canonical loads and the CONVECTION conditions, by value
    temperatures   the canonical PRESCRIBED-TEMPERATURE conditions, by value (the system does not carry them)
    initial        the uniform initial temperature
    transient      time step, end time, output policy
    settings       the residual tolerance and pivot floor
```

`operator==` is field-complete by `= default`, so a dependency cannot be added without being compared, and
`staleReasons` says **which** moved, in the enum's order: `body, mesh, geometry, mode, material, loads,
convections, prescribed_temperatures, initial_condition, time_step, end_time, output_policy, settings`.

## Each edit stales exactly what it changes (tested one at a time on the real block)

| Edit | Reasons | Assembled system |
| --- | --- | --- |
| nothing | none | current |
| prescribed temperature 400 → 410 K | `prescribed_temperatures` | **still current** (K, C, Q do not contain `T_bar`) |
| initial temperature | `initial_condition` | current |
| time step | `time_step` | current |
| end time | `end_time` | current |
| output policy | `output_policy` | current |
| residual tolerance | `settings` | current |
| convection coefficient | `convections` | stale |
| volumetric generation | `loads` | stale |
| specific heat (a thermal-material edit) | `material` | stale — **although K is unchanged** |
| a remesh with identical node and element counts and ids | `mesh` (and others) | stale; old system, boundary, numbering and constraints each refused |

The specific-heat row is the one the transient adds: the steady system never reads `cp`, but the transient
system's `C` does, and the material's revision is part of the system's source, so the edit is refused as
`stale_system` naming `material`.

## The checks before any arithmetic, in this order

```text
 1  the solver settings                       invalid_settings
 2  the time step                             invalid_time_step
 3  the end time (canonical: 0 < dt <= t_end) invalid_end_time
 4  the output policy                         invalid_output_policy
 5  the initial temperature                   invalid_initial_temperature
 6  the assembled system against the model, loads, conditions and material, for TRANSIENT mode      stale_system
 7  the prepared boundary's numbering         dof_map_mismatch
 8  the prepared boundary against the conditions                                                    stale_boundary
 9  the system carries a capacity             not_transient_system
10  the numbering is the system's             dof_map_mismatch
11  the constraints belong to the numbering   constraint_mismatch
12  the time grid, its step count             too_many_time_steps
13  the history size                          history_too_large
```

A bad setting is reported **as** a bad setting even when the system is also stale (test
`SettingsAreRefusedBeforeStalenessIsExamined`, with a steady system, five bad settings, five distinct
problems), and a stale input is reported as one, never as whatever numerical symptom it would have caused.
`transientSolveFailure` runs the same code and returns the same refusal, with its step and time.

## The history is atomic

`TransientThermalSolution` has one private constructor and one friend (`solveTransientThermalSystem`), reached
only after the last step is accepted. A run that fails at step `k` returns a `TransientFailure` and **no
history**: states accepted before step `k` are not published as a result. Compile-fail:
`A_SOLUTION_CONSTRUCTED_DIRECTLY`. At the model level the `Result` form carries only the error.

## The lifecycle around P18-DATA-001's slot

```text
R1 current  ->  the user edits the residual tolerance to 1e-30 (a valid canonical number no solve can meet)
            ->  R1 is SolvedStale
            ->  the run fails at step 1 with residual_too_large; the slot records the failure
R1 still held, still STALE; the latest attempt FAILED; analysisState == SolveFailed; no current result exists.
```

and without an earlier result a failed first run leaves the slot empty (`result == nullptr`, a failure
recorded). The held result in the test carries the final temperatures, the final time and a zero stored-energy
rate with zero gradient and flux channels: **only** the source, the temperatures and the time matter to the
lifecycle, and the channels that belong to P18-POST-001 and P18-BALANCE-001 are not invented.

## Not a ThermalResult

The solve returns a `TransientThermalSolution`, not a sequence of `ThermalResult`. A `ThermalResult` needs a
gradient and a flux per element and a heat balance (with a stored-energy rate for a transient sample), which
are P18-POST-001's and P18-BALANCE-001's; filling them with zeros would claim a result that is not one.
`ThermalResult`'s own comment assigns "the container for the sequence" to this milestone: it is
`TransientThermalSolution`, which holds the sequence of fields in the order the integrator produced them, with
their times, and from which P18-POST-001 will build one `ThermalResult` per kept time level.

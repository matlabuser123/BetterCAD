# P18-TRANSIENT-001 — scope: what was done, and what was deliberately not

```text
SUBJECT:  the line between this milestone and the ones around it, and the absences the compiler enforces
```

## Done here

```text
backward-Euler integration of C dT/dt + K T = Q at a fixed step from t = 0, with exact nonzero Dirichlet
partitioning at every step, a uniform initial temperature, constant loads and conditions
the deterministic time grid, with a shortened last step and an exact landing on t_end
the per-step acceptance by two independent residuals; atomic, source-bound results; stale/failed lifecycle
pure-Neumann and disconnected positive-capacity models SUCCEED; the steady singularity rules do not leak in
```

## Not done here, and the milestone that owns it

| Not done | Owner | How the absence is enforced |
| --- | --- | --- |
| `grad(T)`, heat flux per element | P18-POST-001 | `THE_SOLUTION_HAS_NO_GRADIENT`, `THE_SOLUTION_HAS_NO_HEAT_FLUX` |
| reactions, heat balance, stored-energy rate as a result channel | P18-BALANCE-001 | `THE_SOLUTION_HAS_NO_REACTIONS`, `THE_SOLUTION_HAS_NO_HEAT_BALANCE` |
| a sequence of `ThermalResult` | P18-POST-001 | the solve returns a `TransientThermalSolution` |
| validation against external benchmarks | P18-VALID-001 | — |
| any GUI, plot, colour map, animation | P18-VIZ-001 | no Qt in `thermal` (the layering checker) |
| time-dependent loads or conditions | not authorised (ADR-049) | `THE_TRANSIENT_SETTINGS_HAVE_NO_TIME_PROFILE`, `A_LOAD_HAS_NO_TIME_FUNCTION`, `A_BOUNDARY_CONDITION_HAS_NO_TIME_FUNCTION` |
| adaptive stepping, step doubling | not authorised | `THE_TRANSIENT_SETTINGS_HAVE_NO_ADAPTIVE_STEP`; a failed step fails the run |
| retry with a smaller step | not authorised | `THE_SETTINGS_HAVE_NO_RETRY` |
| a method other than backward Euler (theta, Crank-Nicolson, BDF) | not authorised | `THE_TRANSIENT_SETTINGS_HAVE_NO_METHOD_SELECTOR`, `…_NO_THETA` |
| lumped capacity | not authorised | `THE_SETTINGS_HAVE_NO_LUMPING`; the consistent `C` is used as assembled |
| regularisation, pinning, a penalty | not authorised | `THE_SETTINGS_HAVE_NO_REGULARISATION`, `THE_SOLUTION_HAS_NO_PENALTY`; mutation E06 |
| interpolation to off-grid times, a list of output times | not authorised | `THE_SOLUTION_HAS_NO_INTERPOLATION`, `THE_TRANSIENT_SETTINGS_HAVE_NO_OUTPUT_TIMES` |
| restart from a previous history | not authorised | `THE_SOLUTION_HAS_NO_RESTART` |
| reuse of one factorisation across constant steps | optimisation, not correctness | recorded in [FORMULATION.md](FORMULATION.md) |
| persistence of a history | not authorised | nothing here is canonical; the history is derived and disposable |
| threads, GPU, an iterative solver | not authorised | one serial direct solve; source audit below |

## The time-varying policies (the TODO's two items), stated

```text
LOADS        constant in time. A ThermalLoad is a number (a generation in W/m3, a flux in W/m2); the canonical data
             (P18-DATA-001, ADR-049) has no field in which a time function could live, so none can be supplied,
             ignored or silently frozen. Q is assembled once and is the same Q at every step: the formula's
             Q[n+1] equals Q.
BOUNDARY     constant in time. A prescribed temperature and a convection condition are numbers; T_bar and h, T_inf
CONDITIONS   are the same at every step. A change of either is an EDIT of the analysis: it stales the history
             (CURRENTNESS.md) and is run again.
INITIAL      one uniform temperature, absolute, finite; the prescribed values dominate it at t = 0.
OUTPUT       the grid's own times: FinalOnly or EveryStep. There is no list of requested output times in the canonical
             data, so there is no off-grid time, no event splitting and no interpolation.
ENFORCED BY  three compile-fail cases (a load, a condition and the transient settings have no time function) and the
             absence of any such member.
A FUTURE     a time-dependent load is a change to ADR-049 and to the canonical data and a new milestone; it is not an
CHANGE       option of this solver, and nothing here is shaped to make it a one-line addition.
```
## Source audit

`qualification/source-audit.txt` is the output of `qualification/source-audit.ps1` run over
`ThermalTransient.hpp` and `ThermalTransient.cpp`. It counts each pattern in code (comments stripped) and anywhere:

```text
unordered_map / unordered_set / std::hash        code 0, anywhere 0   container order is never the iteration order
std::thread / async / jthread / mutex / atomic   code 0, anywhere 0   serial
OpenMP / std::execution / parallel               code 0, anywhere 0
Eigen                                            code 0, anywhere 0   no linear algebra library in this header or source
toDense / a dense matrix type                    code 0, anywhere 0   the effective matrix is sparse CSR; no n x n allocation
regularis / epsilon / penalty                    code 0; 2 comment lines in the header saying "Nothing is regularised" and "no A += eps*I"
lump                                             code 0; 1 comment line each in the header and the source saying C is not lumped
retry / adaptive / halv                          code 0; 3 comment lines in the header saying there is none
analyseAnchoring (a call)                        code 0; named in one source comment to say it is NOT called
rand / time( / chrono / clock                    code 0, anywhere 0
pin / datum / reference temperature              code 1: the word "datum" in the text of a diagnostic ("never singular for want of a datum")
```

## Layering

`thermal` (45) includes `core`, `features`, `meshing` and `linalg` (private) and nothing else; the
architecture checker reports the new header and source clean and the link-graph audit shows
`bettercad_thermal` linking no `structural` target. No P17 file was touched by this milestone.

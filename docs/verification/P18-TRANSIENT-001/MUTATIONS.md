# P18-TRANSIENT-001 — mutation probes

```text
RESULT:   85 distinct mutations, 85 killed, 0 survived. Along the way: 3 survivors (test gaps, closed), 6 probes that did
          not compile (reformulated), 18 probes silently skipped in the first pass (rerun under new ids).
METHOD:   a textual mutation of the production file, a build of the whole test binary, the transient suite
          ([transient], 68 then 72 cases), the file restored from the original text; the verdict is read from
          the test output. Scripts and per-mutation logs: qualification/mutations/.
TARGET:   src/thermal/ThermalTransient.cpp for every probe. The steady solver, the kernel and the structural
          wrapper were not mutated here (their own probes are in P18-SOLVE-001).
```

## The passes, and why there are four

| Pass | Driver | Ran against | Ran | Outcome |
| --- | --- | --- | --- | --- |
| 1 | mutation-driver-transient.ps1 | the first complete test set (68 cases) | 52 | 46 killed, **3 survived** (V02, V03, V04), 4 did not compile (H02, H04, H05, P04) |
| 2 | mutation-driver-transient2.ps1 | the final tests (72 cases) | 38 | 36 killed first time, 2 did not compile (TA07, TS03) |
| 3 | mutation-driver-transient3.ps1 | the same | 2 | both killed (TA07b, TS03b, the two reformulated) |
| 4 | mutation-driver-transient4.ps1 | the same | 1 | killed (TA21, the faithful steady-rule leak) |

Pass 1 ran against these file hashes (before four tests and two test assertions were added, which the survivors and the
skipped probes required); pass 2 to 4 ran against the final ones. Both sets were verified restored after the run
(	ransient-postmutation-hashes*.txt equal their premutation pairs).

```text
pass 1
509ae90fad8a79a25e2160143bba54458da37984d81b2b592f4aca2bce0c6d79  src\thermal\ThermalTransient.cpp
fa7d852e5dd43539361feb9b5d28cd556794433615d94d9c1e159b455a8263c8  include\bettercad\thermal\ThermalTransient.hpp
6a5ed3065d75cea2d16f301fd2aee9864f44027d3c447563382ee9bd186e96df  tests\thermal\ThermalTransientTests.cpp
817dacf8b55f32878f5a97ac74996ae954469325fa4af066326131b691ba29e2  tests\thermal\ThermalTransientMappedTests.cpp
fedc0f15f2c994af5c558953a0ca435ea1fe3ee105a871558758750b5f4cc652  tests\thermal\ThermalTransientTestSupport.hpp
e433f682982fe11c234ecde24fbc5a9c479e7f757ee16717cdeaebdb8fa7630b  src\thermal\CMakeLists.txt

passes 2-4
c8387b59b97f78c8c2be606f618899513eeef5218cdaac3f9cfc619521b9c606  src\thermal\ThermalTransient.cpp
fa7d852e5dd43539361feb9b5d28cd556794433615d94d9c1e159b455a8263c8  include\bettercad\thermal\ThermalTransient.hpp
d8d4bf0d1cd7b7f36acc759f0abdd2b1b5a4d126a69900547475ce87be8e10de  tests\thermal\ThermalTransientTests.cpp
084d9fbadd583472396694a0daf515f55875feeb1366162995e8fd1078204b41  tests\thermal\ThermalTransientMappedTests.cpp
fedc0f15f2c994af5c558953a0ca435ea1fe3ee105a871558758750b5f4cc652  tests\thermal\ThermalTransientTestSupport.hpp
e433f682982fe11c234ecde24fbc5a9c479e7f757ee16717cdeaebdb8fa7630b  src\thermal\CMakeLists.txt
```

Tests were only ever ADDED between the passes (and one production message was lengthened), so a mutation killed in pass 1 is
still killed: a stronger suite cannot let a killed mutation back in.

## The three survivors, and what they taught

| Probe | Mutation | Why it survived | Closed by |
| --- | --- | --- | --- |
| V02 | candidate finiteness check of alidateTransientStep removed | the kernel's own check then reports the same classification (
on_finite_solution) | the message now asserted (it names the UNKNOWN, the kernel names a free equation), and a NaN at a prescribed unknown, which without the check would be reported as a moved constraint; killed as TV02 |
| V03 | previous-state finiteness check removed | the right-hand side check then reports the same classification (
on_finite_input) | the message asserted ("previous temperature of unknown 1"); killed as TV03 |
| V04 | physical-residual threshold not applied | for a correct build the physical residual is implied by the reduced one, so no test of a wrong STEP could tell them apart | a small-step test that the reduced residual accepts (1.7e-10) and the physical one refuses (1.7e-7); the physical residual is the stricter test of the increment; killed as TV04 |

The same pattern the steady solver's review found (a check shadowed by a later one with the same classification), and the same
remedy: assert what only the right check says.

## The probes that did not compile

```text
H02, H04, H05   removing a term left a parameter unused (-Werror=unused-parameter)       reformulated as TH02, TH04, TH05 (multiply the term by 0.0)
P04             dropping the stored conditions left a parameter unused                    reformulated as TP04 (clear the vector, then store it)
TA07            dropping the dt comparison left the cache variable set but never used     reformulated as TA07b (false && …)
TS03            replacing the initial-temperature check left the parameter unused         reformulated as TS03b ((void)initial, …)
```

A probe that does not compile proves nothing about the tests, so all six were VOID until reformulated; none is counted as a kill.

## The probes silently skipped in pass 1

The first driver named its probes A01…A15 and S03…S05, which collided with log files from earlier milestones in the same
scratch directory; the driver's "skip a probe whose log already holds a verdict" rule skipped eighteen without running them.
They were found by reading the progress table (the A series was absent) and rerun in pass 2 as TA01…TA15 and TS03…TS05
(TA07 and TS03 then as TA07b and TS03b). No claim in this milestone rests on a skipped probe.

## The first verdicts were wrong for the same reason

Start-Process can leave a process's ExitCode empty, and the first harness decided "survived" from an exit code of zero, so
it reported a missing exit code as KILLED. Three probes whose test run printed "All tests passed" were therefore logged as
killed. The progress table was recomputed from the test output, the harness (mutate-t.ps1) now reads the output and never the
exit code, and the three survivors above are the corrected record.

## Probes that did not end in a ailed line

```text
TA08   the history-size guard removed: the run of the refusing test does not end (a million steps of 51 unknowns), killed by the
       240-second timeout; the report says so
TA09   EveryStep keeps no state after the initial one: a library bounds assertion aborts the run (the test reads a sample that
       does not exist)
TA11   EveryStep omits the initial state: failed checks, then the same abort
```

## All 85

| Probe | Mutation | Pass | Verdict |
| --- | --- | --- | --- |
| G01 | grid tolerance zero (a floating-point integer ratio leaves a sliver step) | 1 | KILLED (4 cases failed) |
| G02 | grid tolerance 1e-3 (a real remainder is swallowed) | 1 | KILLED (1 cases failed) |
| G03 | times accumulated by repeated addition | 1 | KILLED (1 cases failed) |
| G04 | shortened last step uses the nominal step | 1 | KILLED (5 cases failed) |
| G05 | final time computed instead of being t_end exactly | 1 | KILLED (7 cases failed) |
| G06 | floor replaced by ceil for the number of full steps | 1 | KILLED (6 cases failed) |
| G07 | resource guard on the step count removed | 1 | KILLED (2 cases failed) |
| G08 | a zero end time is not special-cased | 1 | KILLED (2 cases failed) |
| G09 | integrated span reports the end time | 1 | KILLED (1 cases failed) |
| E01 | C multiplied by dt instead of divided | 1 | KILLED (35 cases failed) |
| E02 | capacity-only entries not divided by dt | 1 | KILLED (2 cases failed) |
| E03 | conductance-only entries dropped | 1 | KILLED (4 cases failed) |
| E04 | symmetry gate of the effective matrix removed | 1 | KILLED (1 cases failed) |
| E05 | lumped capacity (off-diagonal C dropped) in the effective matrix | 1 | KILLED (36 cases failed) |
| E06 | epsilon added to the effective diagonal | 1 | KILLED (19 cases failed) |
| H01 | history sign flipped | 1 | KILLED (38 cases failed) |
| H03 | history multiplied by dt instead of divided | 1 | KILLED (31 cases failed) |
| V01 | constraint postcondition checked on the previous state instead of the candidate | 1 | KILLED (1 cases failed) |
| V05 | reduced residual not recorded | 1 | KILLED (1 cases failed) |
| V06 | physical residual not recorded | 1 | KILLED (1 cases failed) |
| R01 | physical residual: heat input added instead of subtracted | 1 | KILLED (33 cases failed) |
| R02 | physical residual: the capacity term dropped | 1 | KILLED (40 cases failed) |
| R03 | physical residual: the capacity term multiplied by dt | 1 | KILLED (33 cases failed) |
| R04 | physical residual: every row counted, prescribed boundary heat included | 1 | KILLED (29 cases failed) |
| R05 | physical residual: denominator without the capacity term | 1 | KILLED (1 cases failed) |
| R06 | physical residual: denominator without the conductance term | 1 | KILLED (4 cases failed) |
| R07 | physical residual: per-row finiteness check removed | 1 | KILLED (1 cases failed) |
| R08 | physical residual: norm finiteness check removed | 1 | KILLED (1 cases failed) |
| P01 | initial condition dropped from the solution source | 1 | KILLED (5 cases failed) |
| P02 | transient settings dropped from the solution source | 1 | KILLED (5 cases failed) |
| P03 | solver settings dropped from the solution source | 1 | KILLED (1 cases failed) |
| P05 | prescribed temperatures ignored by the staleness comparison | 1 | KILLED (2 cases failed) |
| P06 | initial condition ignored by the staleness comparison | 1 | KILLED (2 cases failed) |
| P07 | time step ignored by the staleness comparison | 1 | KILLED (2 cases failed) |
| P08 | end time ignored by the staleness comparison | 1 | KILLED (2 cases failed) |
| P09 | output policy ignored by the staleness comparison | 1 | KILLED (2 cases failed) |
| P10 | solver settings ignored by the staleness comparison | 1 | KILLED (2 cases failed) |
| P11 | current source computed for steady mode | 1 | KILLED (4 cases failed) |
| P12 | the assembled system is checked for steady mode | 1 | KILLED (16 cases failed) |
| P13 | stale assembled system accepted | 1 | KILLED (4 cases failed) |
| P14 | stale prepared boundary accepted | 1 | KILLED (1 cases failed) |
| P15 | numbering of another mesh accepted | 1 | KILLED (2 cases failed) |
| P16 | every condition recorded as a prescribed temperature | 1 | KILLED (2 cases failed) |
| P17 | prepared boundary of another numbering accepted | 1 | KILLED (1 cases failed) |
| S01 | end time shorter than one step accepted | 1 | KILLED (3 cases failed) |
| S02 | time step not checked before the system | 1 | KILLED (1 cases failed) |
| TA01 | prescribed does not dominate the initial temperature | 2 | KILLED (8 cases failed) |
| TA02 | initial overrides not counted | 2 | KILLED (4 cases failed) |
| TA03 | the steady anchoring analysis leaks into the transient solve | 2 | KILLED (1 cases failed) |
| TA04 | the state does not advance from step to step | 2 | KILLED (21 cases failed) |
| TA05 | the nominal step is used for every step, including the shortened one | 2 | KILLED (2 cases failed) |
| TA06 | the per-step acceptance is skipped inside the run | 2 | KILLED (3 cases failed) |
| TA07b | the effective matrix is not rebuilt when dt changes (reformulated: keeps the variable used) | 3 | KILLED (2 cases failed) |
| TA08 | the history-size guard removed | 2 | KILLED (timeout, 240 s) |
| TA09 | EveryStep keeps no state after the initial one | 2 | KILLED (the test run aborted on a library bounds assertion) |
| TA10 | the final state is stored twice for EveryStep | 2 | KILLED (7 cases failed) |
| TA11 | EveryStep omits the initial state | 2 | KILLED (the test run aborted on a library bounds assertion after failed checks) |
| TA12 | kernel solves not counted | 2 | KILLED (2 cases failed) |
| TA13 | singular reported as a factorisation failure | 2 | KILLED (1 cases failed) |
| TA14 | the pivot floor is not the user setting | 2 | KILLED (1 cases failed) |
| TA15 | the residual tolerance is not the user setting | 2 | KILLED (1 cases failed) |
| TA16 | kernel-invoked flag not set | 2 | KILLED (1 cases failed) |
| TA17 | pivot ratio not recorded | 2 | KILLED (1 cases failed) |
| TA18 | step index off by one | 2 | KILLED (1 cases failed) |
| TA19 | resolved initial state replaced by the raw one | 2 | KILLED (1 cases failed) |
| TA20 | first EveryStep sample is the unresolved initial state | 2 | KILLED (8 cases failed) |
| TA21 | the steady singularity rule leaks in: the conduction matrix K alone must be anchored | 4 | KILLED (10 cases failed) |
| TV02 | candidate finiteness check removed | 2 | KILLED (1 cases failed) |
| TV03 | previous-state finiteness check removed | 2 | KILLED (1 cases failed) |
| TV04 | physical residual threshold not applied | 2 | KILLED (2 cases failed) |
| TS03b | initial temperature not checked before the system (reformulated: keeps the parameter used) | 3 | KILLED (1 cases failed) |
| TS04 | output policy not checked before the system | 2 | KILLED (1 cases failed) |
| TS05 | a steady-mode definition is taken for a transient one | 2 | KILLED (1 cases failed) |
| TH02 | history term omitted (reformulated: no unused parameter) | 2 | KILLED (42 cases failed) |
| TH04 | history not divided by dt (reformulated) | 2 | KILLED (35 cases failed) |
| TH05 | heat input omitted from the right-hand side (reformulated) | 2 | KILLED (32 cases failed) |
| TP04 | prescribed conditions dropped from the solution source (reformulated) | 2 | KILLED (4 cases failed) |
| TG10 | integrated span of a shortened grid computed as N dt | 2 | KILLED (1 cases failed) |
| TX02 | sample offset ignores the unknown count | 2 | KILLED (5 cases failed) |
| TX03 | final temperatures are the first sample | 2 | KILLED (3 cases failed) |
| TX04 | worst reduced residual is the smallest | 2 | KILLED (1 cases failed) |
| TX05 | worst physical residual is the smallest | 2 | KILLED (1 cases failed) |
| TX06 | load edits reported as convection edits | 2 | KILLED (2 cases failed) |
| TX07 | two diagnostic names collide | 2 | KILLED (1 cases failed) |
| TX08 | current source drops the prescribed conditions | 2 | KILLED (4 cases failed) |

Groups: **G** time grid (9), **E** effective matrix (6), **H** history term (5: H01, H03, TH02, TH04, TH05), **V/TV** the acceptance
step (6), **R** the physical residual (8), **P/TP** provenance and staleness (17), **S/TS** settings and order (5), **TA** the run
(21), **TG10**, **TX** accessors and vocabulary (7).

## Coverage the table maps to the milestone's concerns

| Concern | Probes |
| --- | --- |
| formula C/dt + K, history, consistent capacity | E01, E02, E03, E05, H01, H03, TH02, TH04, TH05 |
| nonzero Dirichlet and the initial state | V01, TA01, TA02, TA19, TA20 |
| time grid and the last step | G01-G09, TG10, TA05, TA07b |
| steady singularity must not leak | TA03, TA21, TA13 |
| no regularisation, pinning or lumping | E05, E06 |
| two independent residuals | TA06, V04 (TV04), V05, V06, R01-R08 |
| output policy and the stored history | TA09, TA10, TA11, TA12, TA16-TA20, TX02, TX03 |
| settings reach the kernel | TA14, TA15 |
| provenance, staleness, lifecycle | P01-P17, TP04, TX06, TX08 |
| order of refusals | S01, S02, TS03b, TS04, TS05 |
| resource guards | G07, TA08 |
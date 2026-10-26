# P18-QUAL-001 - cross-preset equivalence (brief sections 118-122)

Debug (`debug-ext`), Release (`release-ext`) and Debug-shared (`debug-shared-ext`), each cleaned, rebuilt with warnings as errors, rebuilt again with nothing to do, and run through the **full unfiltered CTest**
(5804 tests on each). Source: `qualification/postrun-summary.txt`, produced by `postrun-qual.ps1` from the three preset trees.

## Totals

| preset | build | build log warnings / errors | no-op rebuild | full ctest | repeat set |
|---|---|---:|---|---|---|
| debug-ext | 31 min (17:27 -> 17:58) | 0 / 0 | one line, nothing to do | **5804 of 5804** passed (5147 s) | 2561 tests, until-fail:5, passed (1937 s) |
| release-ext | 39 min (19:24 -> 20:03) | 0 / 0 | one line, nothing to do | **5804 of 5804** passed (4690 s) | 2561 tests, until-fail:5, passed (1665 s) |
| debug-shared-ext | 31 min (21:21 -> 21:52) | 0 / 0 | one line, nothing to do | **5804 of 5804** passed (5042 s) | not part of the repeat set (as in every milestone since P16-SIZE-001) |

5804 is the 5789 of P18-REFMOD-001 plus the 15 tests this milestone added: 7 `P18Qualification_*`, `ThermalPersistence_WhatTheWriterEmitsTheReaderAccepts`,
`ThermalValidation_AMissingOrShortQualitySummaryIsNotAPass`, `_AMeshAtTheEdgeOfTheAcceptedBoundsStillSolvesAndBalances`, three `ThermalSolve_*`, `ThermalCli_ValidationJsonCarries...` and the compile-fail case.
The harness stderr is empty of diagnostics; the layering checker passes on the source tree.

## The reference-model matrix (the same cases, on each preset's own test executable)

| group | debug-ext | release-ext | debug-shared-ext |
|---|---:|---:|---:|
| RM01 | 8/8 | 8/8 | 8/8 |
| RM02 | 2/2 | 2/2 | 2/2 |
| RM03 | 3/3 | 3/3 | 3/3 |
| RM04 | 3/3 | 3/3 | 3/3 |
| RM05 | 2/2 | 2/2 | 2/2 |
| RM06 | 3/3 | 3/3 | 3/3 |
| RM07 | 3/3 | 3/3 | 3/3 |
| RM08 | 8/8 | 8/8 | 8/8 |
| RM09 | 4/4 | 4/4 | 4/4 |
| RM10 | 4/4 | 4/4 | 4/4 |
| persistence / determinism / independence / intent | 2/2, 2/2, 2/2, 1/1 | same | same |
| `[refmod][thermal]` whole | 47 cases, 52 assertion groups, 0 failures | same | same |

## The currentness matrix

`[p18qual]` on each preset's own executable: all seven final-gate cases (five Netgen-backed, two plain; 1012 assertions) pass on all three presets
(`qualification/p18qual-<preset>.txt`): the authority matrix over 20 documents (15 accepted, 5 refused, all byte-identical before and after), the six-edit steady sequence, the transient sequence, the failed
regeneration, the manual edit-back. The staleness sets are therefore **the same sets on three compilers' configurations**, not one preset's.

## Equivalence of what the presets produced

| comparison | result |
|---|---|
| the 20 reference documents the runner wrote, SHA-256 | **identical on all three presets**, all 21 files (the 20 and the edited stale-mesh copy) |
| the reference workflow's 57 non-runner processes: exit code, stdout bytes, stdout hash | **57 of 57 identical** on release and on debug-shared against debug |
| the reference workflow's 32 parsed payloads | key sets identical, text identical, **0 numeric fields differ in a single bit**, worst relative difference 0 |
| the CLI workflow's 74 processes | exit codes **74 of 74 identical**; 50 byte-identical; 24 differ in bytes - the 24 are the messages that echo the build path (`debug-ext` vs `release-ext` is two bytes) and the `version` text (the build type) |
| the CLI workflow's 27 parsed payloads | key sets identical, text identical, **0 numeric fields differ**, worst relative difference 0 |
| the 72 timing-free evidence lines of the reference suite (`BETTERCAD_REFMOD_EVIDENCE`) | **72 of 72 identical to the last digit** on release and on debug-shared against debug |
| the independent verifier (`verify-independent.ps1`) | exit 0 on all three presets (0 of 28 checks failed on each); its negative control exits 1 |

The two cross-preset facts that matter for a numerical code are the last two bullets of the payload rows: the numbers a `-O2` build and a `-O0` build and a DLL build print are the same
bits, for every quantity of 59 payloads. (This is a property of the deterministic assembly order and the absence of fast-math, not of luck; the determinism case proves the former.)

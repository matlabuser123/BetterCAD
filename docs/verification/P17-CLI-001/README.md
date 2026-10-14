# P17-CLI-001 — Headless Structural FEA

```text
AUTHORIZED 2026-10-14 by an explicit scope decision from the owner. TODO.md
had recorded "NOT AUTHORIZED: P17-PERSIST-001 passing will not be permission
to start it", which blocked CLAUDE from starting it unprompted -- not the
owner from authorizing it.
```

## Evidence

| Document | What it holds |
| --- | --- |
| [AUDIT.md](AUDIT.md) | the CLI architecture audit, and the six questions the brief asked me to decide that the project had already decided |
| [COMMANDS.md](COMMANDS.md) | the eleven commands, the core API each calls, and the one core rule this surface cannot reach |
| [STRUCTURED_OUTPUT.md](STRUCTURED_OUTPUT.md) | the payload schemas, the **two kinds of failure**, and the parse gate |
| [EXIT_CODES.md](EXIT_CODES.md) | three codes, not six — and why the granularity lives in the diagnostic code |
| [BACKEND_STDOUT.md](BACKEND_STDOUT.md) | Netgen's chatter: already contained in the adapter, measured rather than inherited on trust |
| [CORE_EQUIVALENCE.md](CORE_EQUIVALENCE.md) | the tables — mutation fingerprints, and every summary scalar against the core |
| [FAILURE_MATRIX.md](FAILURE_MATRIX.md) | every refusal class, and an honest account of the two the CLI cannot present |
| [MULTI_PROCESS.md](MULTI_PROCESS.md) | 28 steps, 28 processes, the fresh-binary proof and the byte-identity measurement |
| [MUTATION.md](MUTATION.md) | 17 runs, 13 mutations, **two real gaps** closed and one equivalent mutant measured |
| [ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md) | 31 attacks, 8 findings, **1 production defect** |
| [FREEZE.md](FREEZE.md) | the three-preset qualification |

The brief recommends separate `FRESH_BINARY.md` and `END_TO_END.md`; both are
folded into [MULTI_PROCESS.md](MULTI_PROCESS.md), which is where the workflow
that proves them lives. Nothing the brief asks for is omitted — the fresh-binary
table is under "The fresh-binary proof" and the step-by-step run is under "The
sequence, as it ran".

## The shape of the milestone

**The audit was the valuable part, and it said the CLI already had the
architecture.** Every question the brief raises about command style, exit
codes, mutation semantics, output channels and backend suppression already had
an answer in the tree with a recorded reason:

```text
the edit spine (ADR-009)      a mutation is a function over a loaded
                              document, and ONE registration makes it both a
                              single-shot command and a batch verb
exit codes                    three, since P10, with the granularity in the
                              diagnostic CODE
structured output             JsonValue, --json, quantity(value, unit)
the face grammar              parseFaceReference, shared with five other
                              command families
the quantity parser           parseQuantity, dimension-generic on purpose
Netgen's stdout               already contained in the adapter, by the
                              milestone that found it the hard way
fresh-binary proof            structural: $<TARGET_FILE:bettercad_cli>
zero-match protection         ZeroMatchGuard.cmake, with its own self-proof
```

So this adds **no new command style, no new output envelope, no new unit
semantics, no new exit-code policy and no change to any core module**.

```text
production change   2 new files   StructuralEdits.cpp   (5 edits)
                                  StructuralReports.cpp (6 reports)
                    5 modified    Commands.hpp, Cli.cpp, Edits.hpp,
                                  EditRegistry.cpp, CMakeLists.txt
core modules        UNCHANGED -- not one file in src/ or include/
tests               1 new test file (19 cases), 1 new workflow script,
                    9 process tests, 2 new zero-match filters
```

## The one production defect, and what found it

**`elementCount` meant the mesh total — triangles plus tetrahedra — and was
printed beside a stress range over tetrahedra.** A 12-tetrahedron block
reported 24.

It was found because the equivalence test compared the printed count against
`RecoveredFields::elements().size()` rather than against "some positive
number". Both counts are now reported, named apart, and probe **L6** confirms
the distinction is load-bearing.

That is the whole argument for writing equivalence tests instead of
smoke tests: a report that prints a plausible number is exactly what a smoke
test cannot distinguish from a correct one.

## Two claims that are structural rather than defended

**No engineering implementation can be in the CLI**, because none of the types
is named there. Measured by grep over both files: `solvePreparedSystem`,
`GeometryMeshMap`, `prepareStructuralLoads`, `MeshDofMap`, `ConstraintSet`,
`GlobalStructuralSystem`, `Eigen`, `Simplicial`, `vonMises =`, `sqrt` — **zero
each**. The five names that do appear (`Tet4`, `eigen`, `DofIndex`, `NodeId`,
`ElementId`) appear **only in comments**, checked line by line.

**No mesh-local target can enter**, because there is no option that could carry
one. `--node`, `--element`, `--facet` and `--at` do not exist, so each is a
usage error — which is how their absence is *proved* rather than observed. A
consequence: `NodalForceLoad`, the one qualified load form naming a `NodeId`,
is not creatable from the CLI, which is correct given ADR-048 refuses to
persist it.

## Where the brief and the project disagreed, and who won

| The brief suggests | BetterCAD does | Chosen |
| --- | --- | --- |
| six exit-code classes | three, since P10 | **three** — §50 says "only if the current contract permits it" |
| `{"status":"error"}` on stdout for failures | stdout **empty** when the command cannot answer | **the project's** — §4 and §48 both defer to it |
| a summary reporting `NoCurrentResult` | the sibling module's reports build what they need and exit | **the sibling's** — §195's own recommendation, and §89's model needs a result to be findable across processes, which ADR-048 forbids |

The third is the one worth naming: following §89 would have made every summary
command permanently useless from a fresh process.

## Where it was qualified, and why that is the one deviation

**Every qualification before this one ran on the uqhas machine. This one ran
on the hasib machine**, by an explicit owner decision on 2026-10-14 when the
uqhas machine was unavailable. [FREEZE.md](FREEZE.md) records what is
identical -- the compiler to the WinLibs revision, the bundled CMake and
Ninja, the dependency prefix rebuilt from the same hash-verified archives, a
configure summary that differs only in paths -- and the three things that
are not: the build root, the prefix path, and three FetchContent source
directories, each forced by the second machine and each recorded beside the
invocation in `qualification/run-qualification.cmd`.

Two of the project's carried constraints were reproduced on the way, which
is its own small confirmation that they are real: the bundled CMake's curl
has no CA trust anchors (constraint 7), and a synchronised or virtualised
folder is no place for a build tree. The result is the same three-preset
verdict every milestone before it has had, from a second, independently
built environment.

## Totals

```text
in-process tests      19 cases -- every engineering assertion an equivalence
                      against the core, no expected-value table
fresh-process tests   9, each driving the real binary
                      + the 28-step multi-process workflow
rejection cases       37 across the two halves, each naming a path or a code
mutation              17 runs, 13 mutations, 12 killed, 1 equivalent
                      (measured), 2 real gaps closed
adversarial review    31 attacks, 8 findings, 1 production defect
blast radius          apps/ and tests/ only. No core module changed, so no
                      predecessor is requalified
qualification         3949/3949 x 3 presets, 1078 x 5 in two, 0 warnings
                      over 676 objects, 17 stages exit 0, 3h00m43s; the
                      eight fingerprints before == after, six of them equal
                      to the P17-PERSIST-001 freeze. FREEZE.md
```

## Three things that went wrong and were recorded rather than tidied away

**I nearly wrote a second unit parser.** The first draft read pressure and
acceleration as bare SI doubles on the grounds that BetterCAD has no catalogue
for them. It has: `Pa`, `MPa`, `GPa`, `N`, `m/s^2` all exist, and
`parseQuantity` is dimension-generic *precisely* so a new quantity cannot
acquire a second spelling. Caught by reading P15-CLI-001's comment rather than
by a test.

**Two of my own expected exit codes were wrong, not the code** — and correcting
them surfaced that no CLI spelling can produce an empty component mask, so the
core's empty-mask refusal is unreachable through this surface. Recorded rather
than left looking like a gap.

**A probe came back "did not compile" with no compile error in the log.** The
disk had filled to zero and `ar` truncated the library to 8 bytes. The
restore-build gate caught it, so an 8-byte archive and a mid-probe test binary
were found instead of trusted — and the probe, rerun, killed three tests. A
void is a void, and this is why.

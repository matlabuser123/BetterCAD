# P16-QUAL-001 — warning audit

```text
UNEXPECTED WARNINGS: 0
```

The gate is 0 unexpected warnings, and "unexpected" is defined **before**
looking: anything a human would have to read and decide about. A status line
that happens to contain the word is not a warning, and a test whose *name*
contains "Failed" is not a failure. Both appear below because both tripped a
naive count first.

## Every channel, in all three presets

```text
| CHANNEL   | debug-ext | release-ext | debug-shared-ext | NOTE                 |
| configure |     0     |      0      |        0         | see below            |
| compile   |     0     |      0      |        0         | -Werror, 596 objects |
| link      |     0     |      0      |        0         |                      |
| runtime   |     0     |      0      |        0         | see below            |
| tests     |     0     |      0      |        0         | 3387/3387 each       |
| CLI       |     0     |      0      |        0         | stdout clean on
|           |           |             |                  | failure, by contract |
| Netgen    |     0     |      0      |        0         | see below            |
```

Compilation is the strongest of these: the build runs with **`-Werror`**, so a
warning is not counted after the fact — it fails the build. 596 objects per
preset, three clean builds, zero.

## The two things a naive count reports and which are not warnings

**1. `configure` appears to emit one warning per preset.** It is this line:

```text
--   Warnings as errors    : ON
```

CMake's own status report of the warning *policy*. Counting it would mean the
stricter the project got, the more warnings it would appear to have.

**2. The test log appears to contain 82 "failed" lines and 4 "Netgen" lines.**
They are test **names**:

```text
unit.MeshingCommand_AFailedGenerationLeavesTheIntentAndTheHistoryIntact
unit.LinearPattern_FailedPatternSavesAndLoadsUnchanged
unit.VolumeBackend_IsNetgenWhenTheBuildLinkedIt
unit.VolumeBackend_ReportsThePinnedNetgenVersion
```

A suite that tests its failure paths will always contain the word. The verdict
line is the one that matters: `100% tests passed out of 3387`.

## Netgen's own channel, and why it is silent

Netgen writes to **stdout**, not stderr, and meshing with a local size
restriction prints

```text
 WARNING: RestrictLocalH called, creating mesh-size tree
```

`2>/dev/null` does not remove it. P16-CLI-001 found this when `mesh-info --json`
became unparseable at line 1 column 2 — the first line a script read was the
warning rather than the opening brace — and fixed it **in the backend adapter**,
under the mutex it already holds, rather than in the consumer: stdout is a
shared resource, and a consumer compensating for a core defect would leave the
GUI and the tests with the same corrupted stream.

So the qualification logs contain **no** occurrence of it. That is the fix
working, not an absence of local sizing: RM-MESH-07 and the sizing suite
exercise `RestrictLocalH` heavily in the same runs.

One place Netgen's output is still expected and is *correct*:
`VolumeBackend_RefusesAnOpenSurfaceRatherThanReportingSuccess` feeds it an open
surface, and its own test says so —

> "Expect Netgen to print 'Meshing of domain 1 failed' while this runs; that is
> the backend being honest in the one channel it has, and it is not an error in
> the test."

That is a documented, named expectation attached to the one test that provokes
it, not a baseline exemption.

## Third-party noise policy

BetterCAD exempts third-party headers from its warning flags by including them
as `SYSTEM` — Catch2, nlohmann/json, Eigen and the OCCT and Netgen headers.
That is a build-system decision made long before P16 and it is why `-Werror`
over 596 objects is achievable at all. It is not a per-warning suppression and
it hides nothing in BetterCAD's own code.

Nothing was added to that list by any P16 milestone.

## Result

```text
UNEXPECTED WARNINGS:  0
"unexpected" defined before looking, and the two false positives a naive
count produces are recorded above rather than quietly filtered
```

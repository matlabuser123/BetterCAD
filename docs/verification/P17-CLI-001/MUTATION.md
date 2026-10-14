# P17-CLI-001 — the mutation probes

```text
17 probe runs over 13 distinct mutations.
12 KILLED, 1 equivalent mutant kept and MEASURED.
TWO REAL TEST GAPS found and closed.
Two VOID results, both from the same environmental cause, both rerun.

PROBES RAN BEFORE THE QUALIFICATION. A surviving probe stopped the pre-freeze
gates by design rather than being discovered inside a three-hour run.
```

## The batch

Filtered across **both** halves — the in-process cases and the fresh-process
ones, 28 tests — because a mutation that broke only the executable would
survive the first set, and one that broke only the command logic would survive
the second.

| Probe | Mutation | Verdict |
| --- | --- | --- |
| L1 | `structural-solve` swallows its own failure | KILLED (3) |
| L2 | `structural-validate` always returns Success | KILLED (3) |
| L3 | the staged validation pre-check skipped | KILLED (2) |
| L4 | `out << "debug: emitting\n"` before the payload | KILLED (2) |
| L5 | the largest von Mises divided by 1e6, still labelled `Pa` | **SURVIVED** → closed |
| L6 | `tetrahedronCount` reports the mesh total | KILLED (2) |
| L7 | the displacement tie-break reversed | **SURVIVED** — equivalent |
| L8 | the reaction sign flipped for presentation | KILLED (1) |
| L9 | an ambiguous document analysed by first match | KILLED (1) |
| L10 | a load's `faceResolves` hard-coded to `true` | **SURVIVED** → closed |
| L11 | the load edit bypasses `AddStructuralLoadCommand` | KILLED (2) |
| L12 | two load kinds at once silently take one | KILLED (1) |
| L13 | a usage error reported as a model failure | KILLED (3) |

## L5 — the extremum's value was never read

The stress summary carries von Mises **twice**: once as a range, once inside
`largest`. L5 corrupted only the second, dividing by 1e6 while leaving the
`"unit": "Pa"` label alone — the exact shape of the brief's §82 concern.

It survived because the test compared the extremum's **id** and the **unit**
and never its **value**:

```text
CHECK element id       the probe did not touch it
CHECK unit == "Pa"     the probe did not touch it
CHECK domain           the probe did not touch it
the number itself      read by NOTHING
```

The workflow script read `vonMises.max` — the range — which the probe also did
not touch. So two layers of testing both looked past the one number that
changed.

**Closed by comparing the values, scoped.** A new `jsonSection` helper extracts
one nested object so the repeated key cannot be confused, and the test now
compares `largest.vonMises`, `largest.sigma1`, the range's `max`, **and that
the two agree with each other** — plus all six stress component ranges against
the core's own spans. The displacement summary had the identical gap on
`largest.magnitude` and got the identical treatment.

L5 rerun after the fix: **KILLED (1)**.

## L10 — two call sites, one assertion

`faceResolves` is written in two places, once for a load's target and once for
a restraint's. L10 hard-coded the **load's** to `true`.

It survived because the fixture carried an unresolved **restraint** and
resolved loads, and the assertion was a whole-payload substring:

```cpp
CHECK_THAT(run.out, ContainsSubstring("\"faceResolves\": false"));
```

The restraint's `false` satisfied it either way. The assertion was true, and
it passed under the very defect it was written for — the **misdirected** shape
this project has now seen six times.

**Closed by splitting the fixture in two.** One section carries an unresolved
load and only resolved restraints; the other carries an unresolved restraint
and only resolved loads. Each additionally asserts the *other* collection's
section contains no `false` at all, so a `false` can only have come from the
entity under test.

L10 rerun after the fix: **KILLED (1)**.

## L7 — an equivalent mutant, with the measurement

Reversing the tie-break — reporting the **highest** id among nodes carrying the
maximum rather than the lowest — changes nothing observable, and the reason was
measured rather than argued:

```text
1 of 9 nodes carry the maximum displacement magnitude
1 of 12 elements carry the maximum von Mises
```

**The maximum is unique**, so the tie-break branch is never taken on any
fixture this CLI can produce. Both orderings are deterministic, so no
determinism test could distinguish them either.

The rule is kept, and the test now **encodes** it — the reported entity must be
the lowest id among those achieving the maximum — so it would kill the mutation
the moment a tie existed. The counts are reported by the test itself via
`WARN`, which is what turns "the branch is unreachable" from an excuse into a
measurement.

> Why not construct a tie? It needs two nodes with bit-identical magnitudes,
> which needs a symmetric mesh, which Netgen does not produce. Hand-building
> one would test the core's mesh builder rather than the CLI.

## L13 and the first L14 — two VOIDs with one cause

L13 came back "DID NOT COMPILE — VOID, not a kill", and the probe harness
refused to record it as a verdict, which is the rule.

**It was not a compile error.** The build log contained **zero** `error:` lines;
the failure was `ar` truncating `libbettercad_cli_lib.a` to 8 bytes because
**the disk filled to 0 bytes mid-run**. The same cause then failed the run's
restore build, which is how it was noticed at all.

That matters twice over:

- A void is a void. Recording L13 as a compiler kill would have hidden a probe
  that, on rerun, **killed 3 tests**.
- The restore-build gate earned its place again. It reported failure, so the
  8-byte archive and the mid-probe test binary were caught instead of being
  trusted — which is the trap "a restored source is not a restored binary"
  describes, and the next green run would have been meaningless.

Both were rerun after space was reclaimed, against hash-verified pristine
sources and a green 28-test baseline.

## What the kills establish

| Claim | Probe |
| --- | --- |
| the exit status carries the gate | L1, L2, L13 |
| the staged validation is load-bearing, and the solver is not reached | L3 |
| stdout is exactly one payload | L4 |
| a unit label matches its number | L5 |
| the per-tetrahedron count is not the mesh total | L6 |
| the core's reaction sign survives presentation | L8 |
| ambiguity is refused, never guessed | L9 |
| an unresolved target is reported as unresolved | L10 |
| the edit goes through the P17-CMD command | L11 |
| exactly one load kind, named | L12 |

## The protocol

```text
pristine copies hash-verified   b7c6262c (reports), 04acb1dd (edits)
                                before the run, before every probe, after
                                every restore, and again at the rerun
every anchor dry-run first      13/13 unique, then 4/4 on the rerun
a red baseline refuses the run  checked: 100% of 28 both times
a mutation that changes nothing refused as VOID
a mutation that does not
  compile                       refused as VOID -- and the one that reported
                                it had no compile error at all
the run ends with a restore
  BUILD, not just a restore     which is what caught the truncated archive
the baseline re-run afterwards  100% of 28
```

Full logs: `logs/probe-cli.log`, `logs/probe-cli-rerun.log`,
`logs/probe-cli-verdicts.txt`.

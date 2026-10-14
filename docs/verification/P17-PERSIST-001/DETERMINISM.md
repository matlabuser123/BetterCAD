# P17-PERSIST-001 — deterministic serialization

```text
Same canonical state  ->  same bytes. Every time, in every preset, from
                          either of two edit paths.
```

## The mechanism is structural, not a sorting pass

`Json` is `nlohmann::ordered_json`, so a written file's **key order is this
file's insertion order** and its **array order is whatever order it is given**.
Determinism therefore comes from writing things in a fixed order rather than
from canonicalising at the end, and nothing in the serializer iterates an
unordered container.

```text
object keys          mesh, mode, loads, restraints, solver -- inserted in that
                     order, once, in one function
loads                the definition's own order (see below)
restraints           the definition's own order
components           kDofComponents order, from a NAME TABLE, never an
                     insertion order
solver keys          algorithm, relative_residual_tolerance, pivot_floor
numbers              nlohmann's shortest round-trip form, locale-independent
```

There is no `std::map`, no `std::unordered_map`, no hash and no sort in the
whole file.

## The order is the user's, and that is the opposite of P16

P16 sorts its local sizing controls and says why: P16-SIZE-001 declares that
order meaningless and P16-CMD-001's fingerprint ignores it. P17 is the opposite
case and says so just as plainly — `StructuralAnalysisDefinition` carries
"**ORDER IS THE USER'S AND IS PRESERVED**" over both vectors, and P17-CMD-001's
probe C8 exists because an edit that silently reordered the loads was a real
defect.

Both are right for their own reason, and the brief's §81 asks for exactly this
distinction ("if collection order is canonical/document-semantic, preserve it;
do not sort arbitrarily and break command/UI semantics").

Determinism is unaffected, because here the order *is* part of the canonical
state: two documents with the same intent have the same order by definition.

`StructuralPersistence_LoadAndRestraintOrderIsTheUsersAndIsPreserved` uses ids
**4, 2, 7** and **3, 1, 5** — deliberately not ascending — and asserts the
positions, plus `loads[0].id() > loads[1].id()` so the test is about order and
not about sorting.

## The component mask has no insertion order to lose

`RestraintComponents` is a bitmask. There is no sequence inside it, so
iterating the name table is the only order it can produce — canonicalisation is
free rather than enforced.

`SerializationIsByteIdenticalAndIndependentOfEditOrder` proves it rather than
asserting the comment: a mask built `Uz` then `Uy` writes the same bytes as one
built `Uy` then `Uz`, and the test also checks `"uy"` is actually present so it
is not comparing two sections that both lack it.

## The measurements

| Check | Result |
| --- | --- |
| five repeated saves of one document | **identical**, all five |
| a mask built in the opposite component order | **identical** section |
| save → load → save | **identical** bytes |
| save → load → save → load → save | **identical** — a fixed point, not one lucky round |
| the same final state via commands + undo/redo vs. set directly | **identical** section |
| 38 persistence tests under `--repeat until-fail:5` | *(recorded in FREEZE.md)* |

**`save1 == save2` is checked as a fixed point, not a pair.** A third pass must
agree too, which is what distinguishes convergence from a single round that
happened to match.

## Floating point

The format stores bare SI doubles and nlohmann writes the shortest form that
round-trips. Tested at four awkward values rather than one:

```text
1.0e-6        small but in the engineering domain
1.0e12        large but in the engineering domain
0.1 + 0.2     NOT 0.3 -- a formatter that wrote "0.3" is caught here and
              nowhere else
1.0 / 3.0     needs all seventeen significant digits
```

Each is required to come back **bit for bit**, with `==` and not a tolerance.

`1.23456789e-8` and `2.5e-11` — the fixture's non-default solver settings — are
likewise compared exactly, and are additionally checked to *differ* from the
constructor defaults, so a dropped field cannot hide behind a default that
happens to match.

**Locale**: nothing in the serializer formats a number through a locale-aware
path; nlohmann's dtoa is locale-independent by construction. No `1,25`-for-`1.25`
failure mode exists to test, and no test claims to have tested one.

## Nothing environment-dependent is written

```text
no timestamp
no build path, source path or temporary path
no memory address, pointer or handle
no object revision, undo depth or edit count
no hostname, user or process id
no random or hash-derived value
```

The revision is the interesting one, because it is *reachable* from the writer
and is not canonical. Probe **P1** writes it and was killed by 28 of the 64
tests in the probe filter — so the absence is enforced by tests and not only by
the author's care.

## Hashes of the artefacts that persist

The byte-identity gate is a direct byte comparison inside the tests, which is
stronger than comparing digests — the brief's §183 asks for both and prefers
the comparison where practical. The four before/after-solve files live in a
per-test temporary directory that is removed on teardown; see
[BYTE_IDENTITY.md](BYTE_IDENTITY.md) for why they are not kept.

The two artefacts that do persist on disk are the committed fixtures:

```text
examples/models/analysed_plate.bcad
    SHA-256  56f71f8c051c86472f905b420efca66a1d4c5027565420d8224f07dc41b648fc
    bytes    7271

examples/models/meshed_plate.bcad          (the pre-P17 compatibility fixture)
    SHA-256  62e043690ef649916d1a816be19305c4795f534528a0557e2513560981efea76
    bytes    6539
```

## Cross-preset

The determinism tests are ordinary unit tests, so they run in all three presets
during the qualification and the result is recorded in [FREEZE.md](FREEZE.md).
BetterCAD's existing contract does not require byte-identical files *across*
presets and nothing here claims it: what each preset independently satisfies is
the full determinism and round-trip set above. The numeric formatting is the
same algorithm in every build, so cross-preset equality is expected — but
expected is not measured, and this file does not record it as though it were.

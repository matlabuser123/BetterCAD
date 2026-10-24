# P18-PERSIST-001 — byte identity across the solve (the hard gate)

```text
the same canonical document, saved before a thermal solve and after it, BYTE FOR BYTE
  steady      8 states compared, every derived stage, a stale result, a failed solve      4230 bytes each
  transient   a result of 201 states x 84 nodes + 393 elements (3,926,736 bytes of numbers)   5251 bytes before and after
```

A thermal solve, a recovery, a balance, a validation and a display-state build must have **zero effect** on the canonical serialization. Two tests state it, with direct byte comparison (not a hash) and the offset of the first differing byte reported on failure.

## Steady: eight states of one document

`ThermalPersistence_TheFileIsIdenticalWhateverASteadySolveHasDone`, on a real mesh (the steel block, a generating body, a held cap and a convecting cap — several free unknowns, so the field varies). The document is saved:

| # | State | Bytes |
| --- | --- | --- |
| A | before anything derived (the mesh exists; no assembly, no solve) | 4230 |
| 1 | after assembly **and** the steady solve | 4230 |
| 2 | after POST (temperature, gradient, heat flux recovered) | 4230 |
| 3 | after BALANCE | 4230 |
| 4 | after VALID (a validation report built) | 4230 |
| 5 | after VIZ (the display state, with the report) | 4230 |
| 6 | with a **stale** result: a remesh gives a new `MeshStamp` and stales everything while changing no document object | 4230 |
| 7 | after a stale display state | 4230 |
| 8 | after a **failed** solve attempt: a fresh `Mesher` that generated nothing | 4230 |

All eight equal state A. **Why eight and not two:** a format that wrote a result only when the result was *valid* would pass a before/after comparison. The stale and failed states are the ones that catch it.

## Transient: a large result

`ThermalPersistence_TheFileIsIdenticalWhateverALargeTransientSolveHasDone`, on a finer mesh (a 3 mm local size on one side face), `dt = 0.01 s` to `t = 2 s`, `EveryStep`:

```text
states                       201
nodes                        84
elements                     393
numbers in the result        201 x (84 + 6 x 393) x 8 bytes = 3,926,736 bytes   (temperatures, gradient and flux components)
the canonical file           5,251 bytes before the solve, after it, after VALID, after VIZ     identical
```

The test requires the result to be more than a hundred times the size of the whole file — so it **could not** have been inside it — and the three post-solve saves to equal the baseline byte for byte. If a single temperature had reached the file the size would have moved.

## The file does not follow the mesh

`ThermalPersistence_TheFileDoesNotFollowTheMeshOrTheResultSize`: the same canonical transient intent on a **cylinder** (a block cannot demonstrate this: its boundary is planar and exactly representable, so two sizes give one mesh — recorded by P16-SIZE-001, P16-PERSIST-001 and P17-PERSIST-001) meshed at 20 mm and at 1.5 mm.

```text
coarse   6206 elements        fine   6601 elements        thermal section 1452 bytes, equal, byte for byte
```

## The document is untouched by saving

`ThermalPersistence_SavingChangesNothingAboutTheDocument`: saving moves neither the analysis revision nor the document revision, and a result that was current before the save is still current after it — saving neither staled it nor refreshed anything.

## Golden bytes, not only self-comparison

Every comparison above is the writer against itself inside one process, which cannot see a changed key order, indentation or number format. Two tests pin the bytes themselves:

* **`TheHandWrittenFileIsExactlyWhatTheWriterProduces`** loads `examples/models/thermal_plate.bcad` — typed by hand, in the order and formatting the schema defines, with `1e-08` as nlohmann prints it — and saves it again. The thermal section (1453 bytes) and the **whole file (7496 bytes)** reproduce the original character for character.
* **`TheGoldenSectionsHaveFixedBytes`** pins an FNV-1a fingerprint of the exact bytes of the steady (1559 bytes) and transient (1728 bytes) golden sections as constants in the test: `4cec5ef3f4cc7014` and `23d057e01f95e19c`. Because the constants are in the source, the debug, release and shared-library builds are all held to the same bytes without any post-run comparison; `postrun-persist.ps1` additionally prints each preset's `PERFP` line as the cross-preset record.

Both were added after the independent review observed that, without them, swapping the two key insertions for `mesh` and `mode` would have passed every test. Mutations PER40 and PER41 are exactly that change ([MUTATIONS.md](MUTATIONS.md)).

## What is not claimed

* The whole-file comparison across **two documents** is never made: a `Document` writes a fresh UUID into its header, so two documents never have equal whole-file bytes whatever their intent. Cross-document comparisons use the thermal section; same-document comparisons use the whole file.
* File-size independence is claimed for the thermal section only. The mesh *control* (P16 intent) is in the file and legitimately differs between the two cylinder documents.

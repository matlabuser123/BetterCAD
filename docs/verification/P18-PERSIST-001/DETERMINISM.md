# P18-PERSIST-001 — deterministic serialization

```text
the same canonical document  ->  the same bytes: ten times in a row, after a save/load/save, through a different history,
under a different locale, and (by constants in the test source) in every build
```

## The mechanism, and what it excludes

| Source of variation | Present in the thermal writer? | How that is known |
| --- | --- | --- |
| key order | no — `Json` is `nlohmann::ordered_json`, and the writer inserts keys in one fixed order (`mesh, mode, loads, boundary_conditions, initial_temperature, solver, transient`) | `TheHandWrittenFileIsExactlyWhatTheWriterProduces` and the pinned FNV constants; mutations PER40 and PER41 reorder keys |
| record order | no — loads and conditions are `std::vector`s kept in the user's order; nothing is sorted or iterated through a map | `LoadAndConditionOrderIsTheUsersAndIsPreserved` (4, 2, 7) |
| an unordered container or a hash | no | gate rule 6; `source-audit.ps1`: 0 |
| a clock, a timestamp, a duration, an iteration count | no — no object type writes one | gate rule 6; `NoDerivedStateReachesTheFile` refuses `"timestamp"`, `"duration"`, `"iteration"`, `"revision"` as keys |
| a revision or a counter | no — mutation PER14 writes the analysis revision and is killed | |
| a pointer or an address | no | |
| floating-point formatting | the format-wide shortest-round-trip formatter, locale-independent | `ExtremeAndAwkwardFiniteValuesSurviveBitForBit` (bit patterns); `TheDecimalPointDoesNotDependOnTheLocale` |
| the state of the solve | no — the writer's only argument is the analysis object | [BYTE_IDENTITY.md](BYTE_IDENTITY.md) |
| the history that built the document | no | `SerializationIsByteIdenticalRepeatedlyAndIndependentOfHowItWasBuilt` |
| whitespace | the format's own: 2-space indent, trailing newline, LF | the hand-written file, in the repository with LF, is reproduced exactly |

## The tests

* **Ten saves of one document are identical** (whole file, 4970 bytes; thermal section 1728 bytes).
* **The same state reached two ways is identical.** One analysis is built directly; another is built up through **nine commands** (adds, an edit that replaces an earlier value of a load, the initial temperature, the transient settings, the solver settings) with an **undo and a redo in the middle** — different revisions, a different edit count, a history of 9 — and the two thermal sections are equal. A format that stored a revision, an undo depth or an edit count would differ.
* **Save → load → save** is byte-identical, and a **third pass** agrees: a fixed point, not a lucky pair.
* **An explicit empty collection** (`"loads": []`) is accepted, normalised away by the first save, and every save after that is identical.
* **Locale.** `std::setlocale(LC_NUMERIC, …)` is switched to a comma-decimal locale if one can be found (`de_DE.UTF-8`, `German_Germany.1252`, `German`, `de-DE`, `fr-FR`, …); the file is saved under it and must equal the file saved under the C locale, must not contain `0,125`, and must contain `"time_step": 0.125`. **If no such locale is installed the test says so with a warning and does not pretend**; on this machine a comma-decimal locale WAS found and used, in all three presets: the `no comma-decimal locale` warning is absent from the XML results of `debug-ext`, `release-ext` and `debug-shared-ext` (`qualification/postrun-summary.txt` records the run), so the test was not vacuous here.
* **Bit-exact values**: `0.1 + 0.2`, the smallest subnormal, the largest finite double, −0.0, 1e−300 survive as `bit_cast<uint64_t>`.

## The golden constants

`TheGoldenSectionsHaveFixedBytes` fingerprints the exact bytes of the two golden sections (FNV-1a over every byte) and pins them in the source:

```text
steady     1559 bytes   4cec5ef3f4cc7014
transient  1728 bytes   23d057e01f95e19c
```

These are the same in debug, release and shared-library builds **because the test asserts them there**, which is stronger than comparing the builds' outputs after the fact (that comparison is also printed: the `PERFP` line of each preset in `qualification/persist-messages-*.txt`, compared by `postrun-persist.ps1`). They change only when the schema, the key order, the formatting or a golden value does — a deliberate act with a new constant.

## What is not claimed

* The whole-file bytes of **two documents** are never compared, because a `Document` writes its UUID into the header. Same-document comparisons use the whole file; cross-document comparisons use the thermal section.
* Names containing Unicode or escapes: object names are validated identifiers, so of five probes only `Study2` is a name the document accepts. The rest are refused by the document, identically each time. There is no free text in a thermal section to escape.

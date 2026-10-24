# P18-CMD-001 — determinism

```text
The same sequence of commands, run again, produces the same canonical states, the same descriptions, the same error codes and the same stack depths.
The same sequence in debug, release and shared-library builds produces the same fingerprints, bit for bit.
```

## What could make a command layer non-deterministic, and what was checked

| Source | Present in the command files? | How that is known |
| --- | --- | --- |
| a clock, a random number, a thread, an atomic | no | `source-audit.ps1` (comments removed): all zero; ownership gate rule 6 fails the build otherwise |
| an unordered container or a hash | no | the same audit and gate rule |
| a pointer or an address used as a key or hashed | no | the fingerprint oracle hashes exact value bits in a fixed order; no address reaches it |
| iteration over anything in an unspecified order | no | loads and conditions are `std::vector`s kept in the user's order; a record is found by identity with `find_if` |
| a number formatted and parsed back | no | formatted only into a `description()`; never parsed (gate rule 5) |
| a revision or a counter written by hand | no | `setDefinition` is the one writer; the audit counts zero |
| a stored derived value that could differ between builds | no | the payload is two canonical definitions (`P18_CMD_CANONICAL_HISTORY_SCHEMA_FINGERPRINT`) |

## The runtime checks

* **`RepeatedSequenceDeterministic`** runs one journey five times on a transient analysis (add a load, add a convection condition, a refused NaN load, set the initial temperature, set the transient settings, two undos, an edit that clears the redo stack, and a refused redo)
  and requires the recorded canonical state after every step, every command description, every error code and both stack depths to be equal across the runs. It also requires the journey to have really exercised
  a refusal and a cleared redo, so equality is not that of two empty runs.
* **`HistorySemanticFingerprintDeterministic`** builds two separate documents the same way and requires equal fingerprints, then changes one value by **one unit in the last place** and requires them to differ: the
  fingerprint sees exactly the canonical content and every bit of it.
* **`DescriptionsAreStable`** pins the text of all ten descriptions.
* **`CMDFP journey`** is a `WARN` line carrying the nine per-step fingerprints, the undo depth and the redo depth. It is not asserted; it is printed so that the *same line can be compared across presets*.

## Across presets

`postrun-cmd.ps1` runs `ThermalCommand_*` through the XML reporter on the binaries of each preset and compares the `CMDFP` lines:

```text
CMDFP journey 6c590ae01a1382bc 0b866c7ac044222a 0b866c7ac044222a 5239297faeb46a5f 61c41c3a731eb653 5239297faeb46a5f 0b866c7ac044222a fa81deafa50a5bad fa81deafa50a5bad depth 3 redo 0

debug-ext vs release-ext          1 vs 1 lines, differing: 0
debug-ext vs debug-shared-ext     1 vs 1 lines, differing: 0
```

The fingerprint is a function of the canonical definition only, so equality across an optimised build, an unoptimised build and a build with the thermal module in a DLL says that nothing in the command layer
depends on codegen, floating-point contraction, inlining or symbol placement. The line also reads like the history it summarises: state 2 equals state 1 (the refused NaN load changed nothing), state 5 equals state 3 and state 6 equals state 1 (each undo returned exactly to the
earlier canonical content), and state 8 equals state 7 (the refused redo changed nothing); the other states are all different from one another.

The three presets' binaries and the 95-case suite are listed in [qualification/postrun-summary.txt](qualification/postrun-summary.txt); the per-preset message files are `qualification/cmd-messages-*.txt`.

## What is not claimed

Determinism of the *solve* is P18-SOLVE-001's and P18-TRANSIENT-001's; here a solve appears only in the currentness tests, which compare a fresh solve of a restored intent with the old one bit for bit, and
were run on all three presets.

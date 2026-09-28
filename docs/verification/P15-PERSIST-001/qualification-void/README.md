# P15-PERSIST-001 — the VOID first qualification

The first qualification **passed every stage** and was voided anyway, by me, for one
blank line.

## What happened

```text
debug-ext          2716/2716   release-ext  2716/2716   debug-shared-ext  2716/2716
repeat release-ext 13580 = 2716 x 5         repeat debug-ext  13580 = 2716 x 5
qualification finished Tue 29/09/2026 00:14:18, 0 stage(s) failed
```

Then the pre-commit `git diff --check` reported:

```text
tests/io/DocumentFileTests.cpp:741: new blank line at EOF.
```

Removing the five obsolete "cannot be saved" tests had left the file ending `}\n\n`
instead of `}\n`. **Zero** committed source files in the repository end with a blank
line, so this was a genuine violation of a universal standard, introduced by me.

## Why it was voided rather than waved through

CLAUDE.md: *"If source or tests change after the freeze, the qualification is void and is
run again from clean."* And: *"On a failed gate: stop, diagnose, fix, re-run, continue
only on PASS. Never continue past a failure, and never relax a gate to obtain one."*

`tests/` is inside the eight-path source fingerprint, so fixing the line changes the
qualified tree. The alternative was to argue that whitespace is not really a source
change — which is exactly the reasoning that makes a freeze stop meaning anything. One
exception for a blank line is the precedent for the next exception.

So: about three hours of machine time for one byte. The discipline is the point, and the
cost is the price of the rule being real.

## The lesson, which is about ordering rather than whitespace

`git diff --check` was run where the brief puts it — before the commit — and that is
**after** the freeze. It costs nothing and belongs in the pre-freeze checklist, beside
the shared-preset build that caught the dll-import defect earlier in this same
milestone.

```text
pre-freeze checklist, as it should have been
  git diff --check                     costs a second
  build the shared preset              costs twenty minutes
  then freeze, then qualify            costs three hours
```

Both of this milestone's avoidable costs came from running a cheap check after an
expensive one.

## What changed between the two runs

One byte: the trailing newline of `tests/io/DocumentFileTests.cpp`. No test, no
assertion, no product code. The second qualification's `tests` tree hash therefore
differs from the first, which is the whole reason this attempt is void.

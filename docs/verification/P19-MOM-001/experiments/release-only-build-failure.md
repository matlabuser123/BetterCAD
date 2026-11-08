# F-REL-1: a Release-only build failure the Debug preset could not show

## What happened

`cmake --build --preset release-ext` fails where `debug-ext` succeeds:

```text
tests/cfd/CfdMomentumTests.cpp:1915:17: error: array subscript -1 is outside
array bounds of 'double [1152921504606846975]' [-Werror=array-bounds=]
 1915 |         bad.back() = nan;
```

`bad` is a copy of a vector returned by `produceFlux`. Under `-O3`, after
inlining the producer, GCC's `-Warray-bounds` analysis cannot prove the copy is
non-empty, so it assumes the worst and reports the `size() - 1` index as `-1`.
Release builds with `-Werror`, so this BREAKS THE BUILD on a preset the Debug
run never exercises.

## Why it matters more than a warning

CLAUDE.md's adversarial review asks, in as many words, "could Debug and Release
differ". This is that, and it was found by BUILDING the second preset rather
than by reading the code — which is the only way it could have been found. Had
the qualification been launched on the strength of a passing `debug-ext` run,
the freeze would have been void and the run discarded, which is precisely the
three hours `P19-DIV-001` lost.

## What the compiler actually says, measured not guessed

All three candidate reformulations -- including the FAILING form -- compile
clean in a standalone translation unit under the same flags
(`-O3 -DNDEBUG -std=c++23 -Wall -Wextra -Wpedantic -Wconversion
-Wsign-conversion -Wdouble-promotion -Werror`). So the diagnostic is not a
property of the construct: it appears only once the producer is inlined and the
vector's provenance is visible. A local reformulation is therefore not
guaranteed to help, and the fix has to remove the unproven ACCESS rather than
reword it.

## The fix

A helper whose write cannot be out of range by construction:

```cpp
[[nodiscard]] std::vector<double> withLeadingNan(std::span<const double> values) {
    std::vector<double> out(values.begin(), values.end());
    for (double& entry : out) {
        entry = std::numeric_limits<double>::quiet_NaN();
        break;
    }
    return out;
}
```

A range-for that breaks after one iteration writes exactly one element and runs
only when there is one, so there is no subscript for the analysis to bound. The
poisoned element moves from the last to the FIRST, which is if anything a
stronger test: a finiteness scan that checked only the final entry would now
fail, and one that checked only the first would already have been caught by the
previous placement.

Five sites use `back()`/`front()` writes and four more use a bare `pop_back()`.
Only the bare `pop_back()` sites are safe as they stand -- no access follows
them. Every site is re-verified by building release-ext after the change,
because the diagnostic is inlining-dependent and reading the code is how it was
missed the first time.

---

## CORRECTION, after P19-MOM-001's first qualification attempt FAILED

**THE PARAGRAPH IMMEDIATELY ABOVE IS WRONG, AND IT IS WHY THE GATE FAILED.** It
is kept verbatim because the error in it is the finding.

`shortenedWithLeadingNan` used a bare `pop_back()`. It broke the `release-ext`
build 66m28s into the qualification, with the same diagnostic at
`CfdMomentumTests.cpp:629`.

Two things were wrong, and the second follows from the first:

```text
1  "no access follows them" -- pop_back() IS the access. It calls the
   destructor on *(end() - 1), and that is exactly the subscript GCC bounds
   at -1. There is no later read for anything to follow; the pop itself is
   the subscript. The claim is not a weak argument, it is a false one
2  Because the pop_back() sites had been reasoned safe, they were never
   COMPILED to find out -- while the very next sentence promises that every
   site is re-verified by building release-ext. The promise was written and
   not kept, on the sites the wrong argument had excused
```

What actually separates the four surviving `pop_back()` sites from the failing
one is not what follows the call. It is whether GCC still holds a proof that the
vector is non-empty at the point of the pop:

```text
lines 1939, 1948, 1957, 2228   in test bodies, on vectors constructed from a
                               known-non-empty source a few lines above, where
                               the construction is visible to the analysis
line 629 (the failure)         in a helper, inlined into its caller, where the
                               span's length is a parameter and the proof is
                               gone
```

And the four are not known safe by that argument either. They are known safe
because **the Release compile of that translation unit emitted exactly one
diagnostic**: `cc1plus` analyses the whole unit and reports every warning before
`-Werror` ends it, so all four `pop_back()` calls and all six `front()` calls
were analysed and passed. That is a measurement. The paragraph above was an
argument, and arguments of that shape are what this file exists to warn against.

The fix at line 629 copies an explicit range that is already one short, so
nothing is destroyed and no subscript exists to bound.


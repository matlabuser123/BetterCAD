# P17-DOF-001 — mutation protection

```text
SUBJECT:  ten deliberate defects introduced into the production sources, to
          establish that the suite would CATCH them rather than that it passes
PROBES:   10
KILLED:   10   (9 at run time, 1 at compile time)
SURVIVED: 1 on the first pass -- M7, resolved by adding the test that makes
          the branch reachable, after which it is killed
```

Harness: `dofmut/run.sh` and `dofmut/run2.sh` in the session scratchpad. Each
probe restores the pristine source first, applies **one** literal substitution
that is verified to be present exactly once (an ambiguous or missing anchor is
an error, not a silent no-op), rebuilds `bettercad_tests`, runs the 31 selected
tests, and restores.

## First pass

| Probe | Mutation | Verdict | Tests that failed |
| --- | --- | --- | --- |
| M1 | `DofIndex` numbering made zero-based (`+ 1` removed) | **KILLED** | 29 of 29 |
| M2 | node ordinal and component offset swapped in the index formula | **KILLED** | 12 of 29 |
| M3 | stamp check in `buildFreeEquationMap` disabled | **KILLED** | 4 of 29 |
| M4 | duplicate degrees of freedom deduplicated instead of refused | **KILLED** | 2 of 29 |
| M5 | `nodeOrdinal` returns `node.value() - 1` instead of a binary search | **KILLED** | 7 of 29 |
| M6 | `dofAt` divisor and modulus swapped | **KILLED** | 3 of 29 |
| M7 | `componentAt` modulo removed | *SURVIVED* | — |
| M8 | empty-mesh refusal removed | **KILLED** | 1 of 29 |

The named failures are in `dofmut/results.txt`. Two are worth quoting because
they say which test is the load-bearing one:

```text
M5  ordinal from the handle value
      MeshDofMap_NumbersSparseNodeHandlesDenselyRatherThanByHandleValue
      MeshDofMap_OrdinalsFollowTheMeshOwnNodeEnumeration
      MeshDofMap_IndexAndDofAreExactInversesForEveryDegreeOfFreedom
      FreeEquationMap_PartitionsEveryDofIntoFreeOrConstrained
      ... 7 in all

M3  no stamp check on the pair
      FreeEquationMap_RefusesAConstraintSetBuiltAgainstADifferentMesh
      MeshDofMap_ARemeshInvalidatesTheNumberingAndTheConstraintsBuiltOnIt
      StructuralDof_IsDeterministicAcrossARemeshOfTheSameReferenceModel
```

M5 is the central claim of the milestone — handle arithmetic instead of the
mesh's own ordinal — and it is caught by four independent tests, including the
sparse fixture written for exactly it. M3 is the remesh claim, caught by both a
synthetic pair and a real reference model.

M2 deserves a note: swapping the ordinal and the offset is the mutation that
turns interleaved numbering into something else, and it is caught by
`MeshDofMap_NumbersANodesThreeDofsConsecutivelyRatherThanInComponentBlocks` —
which is the test that asserts the convention **and its negation**. Without the
negation half, a formula check on node ordinal 0 would have passed, because
`3*0 + c + 1` and `3*c + 0 + 1` agree there.

## M7, and what a surviving mutation is for

`componentAt(offset)` reduces its argument modulo `kDofsPerNode`. Removing the
modulo changed nothing: every caller passed `zeroBased % kDofsPerNode`, already
reduced, so **the branch was unreachable**.

A surviving mutation can mean the test is missing or that the code is
unreachable, and the two have different fixes. Here the right reading was the
second, and it pointed at something better than a test: the modulo is **not a
defensive guard**. It is the cyclic component lookup over a flat element-vector
position — position 3 of `[ux1 uy1 uz1 ux2 ... uz4]` is the second node's Ux —
which is exactly what `P17-ELEM-001` will index a twelve-component Tet4 vector
with.

So the resolution was to make the use case explicit rather than to delete the
code or to contrive a test:

```text
StructuralDof_ComponentLookupIsCyclicOverAFlatElementPosition
  componentAt(0,1,2)    Ux Uy Uz        STATIC_REQUIRE
  componentAt(3,4)      Ux Uy           the second node
  componentAt(11)       Uz              the fourth node of a Tet4
  all twelve            == kDofComponents[flat % kDofsPerNode]
```

and the ordering `P17-ELEM-001` depends on is now fixed here, where the
convention lives, instead of there.

## Second pass, after the F1 fix

| Probe | Mutation | Verdict |
| --- | --- | --- |
| M7 | `componentAt` modulo removed | **KILLED — at compile time** |
| M9 | range check in `buildFreeEquationMap` disabled | **KILLED** |
| M10 | `describes()` ignores the node count | **KILLED** |

M7 is now killed by the **compiler**, which is a stronger kill than a failing
assertion: the new assertions are `STATIC_REQUIRE`, so without the modulo
`kDofComponents[11]` is an out-of-range `std::array::operator[]` in a constant
expression and libstdc++'s bounds assertion is not `constexpr`:

```text
StructuralDofTests.cpp:1053: error: non-constant condition for static assertion
array:219: error: call to non-'constexpr' function
          'void std::__glibcxx_assert_fail(...)'
```

M9 and M10 are the two halves of the F1 fix, and each is killed by
`FreeEquationMap_RefusesAConstraintSetThatOutgrowsTheNumbering` alone — which
is correct, since that test was written for this defect and nothing else in the
suite can reach the one-stamp-two-sizes state.

## A harness defect, recorded because it produced a false failure

After the second pass the harness restored the sources, **verified the
restoration by sha1**, and printed matching hashes — and the next
`ctest --repeat` run failed:

```text
CHECK_FALSE( smallMap.describes(large) )   with expansion: !true
```

which reads exactly like the F1 fix not working. It was not: restoring a source
does not relink a test executable, so the run was still using the **M10 mutant
binary**. The same filter in `debug-shared-ext`, freshly built from the
restored tree, passed 31 of 31 at the same moment, which located the problem in
the binary rather than the source.

Both scripts now end with a restore build. Noted here because a verdict
produced by a stale artifact is indistinguishable from a real one, and this one
was in the direction that would have had me "fixing" working code.

## Coverage of the claims

```text
the numbering is one-based                     M1
the numbering is interleaved, not blocked      M2
the ordinal is the mesh's, not the handle's    M5
dofAt inverts indexOf exactly                  M6
a remesh invalidates a numbering               M3
a constraint set is bound to its numbering     M3, M9, M10
a duplicate is refused, not deduplicated       M4
an empty mesh is refused                       M8
the component lookup is cyclic                 M7
```

Nine claims, ten probes, all killed.

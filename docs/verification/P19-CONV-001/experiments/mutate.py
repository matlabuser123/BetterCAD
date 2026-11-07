# -*- coding: utf-8 -*-
"""Mutation sweep for P19-CONV-001, run AFTER the two independent reviews.

Each probe is a mutation the reviews said survived the first draft of the
suite. A probe that is NOT killed is a gap that is still open.

A MUTATION THAT DOES NOT COMPILE IS NOT EVIDENCE. The first run of this sweep
reported 8 of 8 killed, but four of those were killed by `-Werror` because the
mutation left a variable unused -- which says nothing about the tests. Those
four carry a `(void)` cast now, on the SAME LINE so no escape sequence has to
survive the trip through a shell, and the verdict distinguishes
`KILLED(compile)` from `KILLED` so a compile kill can never be read as a test
kill again.

Every mutation is applied to a backup-restored tree, so the sweep cannot leave
the source changed: the `finally` restores unconditionally.

Usage: python -I mutate.py <probe-name|all>
"""
import io
import os
import shutil
import subprocess
import sys

REPO = "C:/Users/Hasib/Desktop/CAD"
BUILD = "C:/bettercad/bc-build/debug-ext"
EXE = BUILD + "/bin/bettercad_tests.exe"

CONV = REPO + "/src/cfd/CfdConvection.cpp"
DIFF = REPO + "/src/cfd/CfdDiffusion.cpp"

# name -> (file, old, new, test filter)
PROBES = {
    # --- the weight, which the reviews found completely unpinned ---
    # The hand fixture's weight IS 2/3, so only the reference test can kill a
    # constant 2/3; `[conv]` therefore includes the reference bodies here.
    "weight-constant-two-thirds": (
        DIFF,
        "    return (total - owner) / total;",
        "    (void)owner; return 2.0 / 3.0;",
        "[conv]",
    ),
    "weight-midpoint": (
        DIFF,
        "    return (total - owner) / total;",
        "    (void)owner; return 0.5;",
        "[conv]~[reference]",
    ),
    "weight-transposed": (
        CONV,
        "            value = rho * dotOf(stored, interpolated(owner, other, *weight));",
        "            value = rho * dotOf(stored, interpolated(other, owner, *weight));",
        "[conv]~[reference]",
    ),
    # --- the boundary rule ---
    "fixedvalue-before-sign": (
        CONV,
        "        if (phi >= 0.0) {",
        "        if (phi >= 0.0 && treatment->velocity != CfdBoundaryPolicy::FixedValue) {",
        "[conv]~[reference]",
    ),
    "boundary-through-scheme": (
        CONV,
        """        if (phi >= 0.0) {
            // OUTFLOW: the upwind value is the owner's, whatever the policy
            // says, because the flow is leaving and the condition describes what
            // arrives.
            ++summary.outflowFaces;""",
        """        if (phi >= 0.0 || central) {
            // MUTANT: routes a boundary face through the scheme branch.
            ++summary.outflowFaces;""",
        "[conv]~[reference]",
    ),
    # --- the transported component ---
    "component-always-x": (
        CONV,
        "            source[owner] -= phi * prescribed[componentIndex];",
        "            (void)componentIndex; source[owner] -= phi * prescribed[0];",
        "[conv]~[reference]",
    ),
    # --- the finiteness gates the review found missing ---
    "drop-total-finiteness": (
        CONV,
        """        if (!std::isfinite(value)) {
            return std::unexpected(failureOf(
                CfdConvectionProblem::ResultIsNotFinite,
                std::format("the reported {} total is {}, which is not finite; every coefficient may "
                            "still be finite, which is why this is checked separately",
                            name, value)));
        }""",
        "        (void)name; (void)value;",
        "[conv]~[reference]",
    ),
    # --- the wall's exact zero: compute it instead of taking the table's ---
    "wall-computes-its-flux": (
        CONV,
        """                // imbalance somewhere else.
                value = 0.0;
                break;""",
        """                // imbalance somewhere else.
                value = rho * dotOf(stored, view.velocityAt(topology.owner(face).value()));
                break;""",
        "[conv]~[reference]",
    ),
    # --- the prescribed-inflow sign, which the row-sum identity rests on ---
    "prescribed-inflow-sign": (
        CONV,
        "            source[owner] -= phi * prescribed[componentIndex];",
        "            source[owner] += phi * prescribed[componentIndex];",
        "[conv]~[reference]",
    ),
    # --- the scatter's exact negation ---
    "neighbour-not-negated": (
        CONV,
        "            if (const auto bad = add(other, owner, -phi * ownerShare)) {",
        "            if (const auto bad = add(other, owner, phi * ownerShare)) {",
        "[conv]~[reference]",
    ),
}


def run(probe):
    path, old, new, filt = PROBES[probe]
    backup = path + ".mutbak"
    shutil.copyfile(path, backup)
    try:
        text = io.open(path, encoding="utf-8", newline="").read()
        if text.count(old) != 1:
            return ("ANCHOR", "anchor found %d times" % text.count(old))
        io.open(path, "w", encoding="utf-8", newline="").write(text.replace(old, new, 1))

        build = subprocess.run(["cmake", "--build", BUILD, "--target", "bettercad_tests"],
                               capture_output=True, text=True,
                               env=dict(os.environ, CMAKE_BUILD_PARALLEL_LEVEL="4"))
        if build.returncode != 0:
            return ("KILLED(compile)", "NOT evidence about the suite")

        test = subprocess.run([EXE, filt], capture_output=True, text=True)
        if test.returncode != 0:
            tail = [l for l in test.stdout.splitlines() if "FAILED" in l]
            return ("KILLED", "%d failing assertion lines" % len(tail))
        return ("SURVIVED", "the suite still passes -- GAP")
    finally:
        shutil.copyfile(backup, path)
        os.remove(backup)


if __name__ == "__main__":
    which = sys.argv[1] if len(sys.argv) > 1 else "all"
    names = list(PROBES) if which == "all" else [which]
    results = []
    for name in names:
        verdict, note = run(name)
        print("  %-28s %-16s %s" % (name, verdict, note), flush=True)
        results.append((name, verdict))
    by_suite = sum(1 for _, v in results if v == "KILLED")
    by_compile = sum(1 for _, v in results if v == "KILLED(compile)")
    survived = [n for n, v in results if v == "SURVIVED"]
    anchor = [n for n, v in results if v == "ANCHOR"]
    print()
    print("probes %d | killed BY THE SUITE %d | killed by the compiler %d | "
          "survived %d | anchor-miss %d"
          % (len(results), by_suite, by_compile, len(survived), len(anchor)))
    if survived:
        print("SURVIVING PROBES (open gaps): %s" % survived)
    if anchor:
        print("ANCHOR MISSES (probe did not apply, proves nothing): %s" % anchor)
    subprocess.run(["cmake", "--build", BUILD, "--target", "bettercad_tests"],
                   capture_output=True, text=True,
                   env=dict(os.environ, CMAKE_BUILD_PARALLEL_LEVEL="4"))

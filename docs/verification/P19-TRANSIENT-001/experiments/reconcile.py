"""P19-TRANSIENT-001 -- reconcile the qualification gate's own logs.

Reads nothing but the logs the harness wrote and FREEZE.md's predicted figures,
and EXITS NON-ZERO on any mismatch rather than printing one. Runs no build and
no test: every check here is a read.

The predicted figures are not typed in twice. They are parsed out of FREEZE.md,
so a figure this script agrees with is a figure the freeze committed to BEFORE
the run.
"""

import io
import os
import re
import sys

ROOT = r"C:/Users/Hasib/Desktop/CAD/docs/verification/P19-TRANSIENT-001"
Q = ROOT + "/qualification"
PRESETS = ["debug-ext", "release-ext", "debug-shared-ext"]
REPEATS = ["release-ext", "debug-ext"]

FULL = 6965          # predicted; re-derived from FREEZE.md below
SELECTED = 3527
EXECUTIONS = 17635

failures = []


def check(ok, label, detail=""):
    print("  %-4s %s%s" % ("OK" if ok else "FAIL", label,
                           ("   " + detail) if detail else ""))
    if not ok:
        failures.append(label)


def read(path):
    with io.open(path, encoding="utf-8", errors="replace", newline="") as f:
        return f.read()


def count(text, pattern):
    return len(re.findall(pattern, text, re.MULTILINE))


# ---------------------------------------------------------------- FREEZE.md
print()
print("== the freeze's own predictions, parsed out of FREEZE.md ==")
freeze = read(ROOT + "/FREEZE.md")

m = re.search(r"full suite, per preset\s+(\d+) = (\d+) \+ (\d+)", freeze)
check(m is not None, "FREEZE.md states a full-suite figure")
if m:
    predicted_full = int(m.group(1))
    check(predicted_full == FULL, "predicted full suite == %d" % FULL,
          "FREEZE.md says %d" % predicted_full)

m = re.search(r"repeat set, per preset\s+(\d+) = (\d+)", freeze)
if m:
    check(int(m.group(1)) == SELECTED, "predicted repeat set == %d" % SELECTED,
          "FREEZE.md says %s" % m.group(1))

m = re.search(r"repeat EXECUTIONS\s+(\d+) = (\d+) x (\d+)", freeze)
if m:
    check(int(m.group(1)) == EXECUTIONS,
          "predicted executions == %d" % EXECUTIONS,
          "FREEZE.md says %s = %s x %s" % m.groups())

# the eight frozen identities, from the table under "as frozen"
block = freeze.split("## The eight identities, as frozen", 1)
check(len(block) == 2, "FREEZE.md holds the identity table")
frozen_ids = {}
if len(block) == 2:
    fenced = re.search(r"```text\n(.*?)```", block[1], re.S)
    if fenced:
        for ln in fenced.group(1).splitlines():
            parts = ln.split()
            if len(parts) == 2 and re.fullmatch(r"[0-9a-f]{40}", parts[1]):
                frozen_ids[parts[0]] = parts[1]
check(len(frozen_ids) == 8, "FREEZE.md's table holds EIGHT identities",
      "parsed %d" % len(frozen_ids))

# the per-tag figures FREEZE.md predicted
predicted_tags = {}
for ln in freeze.splitlines():
    m = re.match(r"\s*(\[[a-z\]\[]+\])\s+([\d,]+) assertions in\s+([\d,]+) test cases", ln)
    if m:
        predicted_tags[m.group(1)] = (int(m.group(2).replace(",", "")),
                                      int(m.group(3).replace(",", "")))
check(len(predicted_tags) >= 6, "FREEZE.md predicts at least six per-tag figures",
      "parsed %d: %s" % (len(predicted_tags), ", ".join(sorted(predicted_tags))))

# ---------------------------------------------------------------- the verdict
print()
print("== the verdict: the harness's own two statements ==")
out = read(Q + "/run-qualification.out")
times = read(Q + "/qualification-times.txt")

check("Qualification passed: every stage exited 0." in out,
      "run-qualification.out states a pass")
check("QUALIFICATION FAILED" not in out,
      "run-qualification.out states no failure")
m = re.search(r"qualification finished .*?, (\d+) stage\(s\) failed", times)
check(m is not None and m.group(1) == "0",
      "qualification-times.txt says 0 stage(s) failed",
      "says %s" % (m.group(1) if m else "nothing"))

exits = re.findall(r"exit (-?\d+)", times)
check(len(exits) == 17, "seventeen stages carry an exit code",
      "found %d" % len(exits))
check(all(e == "0" for e in exits), "every stage exited 0",
      "non-zero: %s" % [e for e in exits if e != "0"])

m = re.search(r"qualification candidate:\s*\n([0-9a-f]{40})", times)
check(m is not None, "the run records its candidate commit",
      m.group(1) if m else "")
candidate = m.group(1) if m else None

# ---------------------------------------------------------------- identities
print()
print("== the eight identities, before the first build and after the last test ==")


def parse_ids(text, marker):
    tail = text.split(marker, 1)
    if len(tail) != 2:
        return {}
    got = {}
    for ln in tail[1].splitlines()[1:]:
        parts = ln.split()
        if len(parts) == 2 and re.fullmatch(r"[0-9a-f]{40}", parts[1]):
            got[parts[0]] = parts[1]
        elif got:
            break
    return got


before = parse_ids(times, "qualified source trees before the first build:")
after = parse_ids(times, "qualified source trees after the last test run:")

# P19-STEADY-001's lesson: two one-element dicts compare equal just as well as
# two eight-element ones. Assert the SIZE before reporting the equality.
check(len(before) == 8, "the pre-build reading holds EIGHT paths",
      "holds %d" % len(before))
check(len(after) == 8, "the post-run reading holds EIGHT paths",
      "holds %d" % len(after))
check(len(before) == 8 and before == after,
      "pre-build == post-run, so nothing moved during the run")
check(len(frozen_ids) == 8 and before == frozen_ids,
      "pre-build == FREEZE.md's table, as a third party")
for k in sorted(frozen_ids):
    if before.get(k) != frozen_ids[k]:
        check(False, "identity %s" % k,
              "freeze %s, run %s" % (frozen_ids[k], before.get(k)))

# ---------------------------------------------------------------- the builds
print()
print("== the three builds, under -Werror ==")
for p in PRESETS:
    b = read(Q + "/build-%s.log" % p)
    check(count(b, r"error:") == 0, "%s: zero 'error:'" % p)
    check(count(b, r"warning:") == 0, "%s: zero 'warning:'" % p)
    # the only Error substrings allowed are source filenames
    bad = [ln for ln in b.splitlines()
           if re.search(r"\bError\b", ln)
           and not re.search(r"Error\.(cpp|hpp)", ln)]
    check(not bad, "%s: every 'Error' substring is a source filename" % p,
          "stray: %s" % bad[:2])
    rb = read(Q + "/rebuild-%s.log" % p).strip().splitlines()
    check(len(rb) == 1 and "Checking git revision" in rb[0],
          "%s: the no-op rebuild ran one edge, the git stamp" % p,
          rb[0] if rb else "(empty)")

# ---------------------------------------------------------------- full ctest
print()
print("== the three full ctest logs, counted four ways each ==")
GROUPS = {"architecture\\.": 205, "compile_fail\\.cfdtransient": 28,
          "unit\\.CfdTransient_": 24, "unit\\.CfdTimeGrid_": 10,
          "unit\\.CfdSteady_": 19}
for p in PRESETS:
    c = read(Q + "/ctest-%s.log" % p)
    counts = {
        "Start markers": count(c, r"^\s*Start\s+\d+:"),
        "progress-prefixed": count(c, r"^\s*\d+/%d " % FULL),
        "result lines": count(c, r"Test\s+#\d+:"),
        "Passed": count(c, r"\.\.\.   Passed"),
    }
    check(all(v == FULL for v in counts.values()),
          "%s: four counts all == %d" % (p, FULL), repr(counts))
    check("tests failed out of" not in c,
          "%s: the failure clause is absent" % p)
    check(count(c, r"\*\*\*Failed|\*\*\*Exception|\*\*\*Timeout|Exit code 0x") == 0,
          "%s: no failure marker of any kind" % p)
    check("100%% tests passed out of %d" % FULL in c,
          "%s: summary line reads 100%% of %d" % (p, FULL))
    for g, n in GROUPS.items():
        got = count(c, r"Test\s+#\d+: " + g)
        check(got == n, "%s: %s == %d" % (p, g.replace("\\", ""), n),
              "got %d" % got)

# ---------------------------------------------------------------- repeat
print()
print("== the two repeat logs: %d x 5 == %d ==" % (SELECTED, EXECUTIONS))
for p in REPEATS:
    c = read(Q + "/ctest-repeat-%s.log" % p)
    execs = {
        "Start markers": count(c, r"^\s*Start\s+\d+:"),
        "result lines": count(c, r"Test\s+#\d+:"),
        "Passed": count(c, r"\.\.\.   Passed"),
    }
    sel = count(c, r"^\s*\d+/%d " % SELECTED)
    check(all(v == EXECUTIONS for v in execs.values()),
          "repeat %s: three execution counts all == %d" % (p, EXECUTIONS),
          repr(execs))
    check(sel == SELECTED, "repeat %s: %d selected" % (p, SELECTED),
          "got %d" % sel)
    check(execs["Passed"] == sel * 5,
          "repeat %s: ratio is EXACTLY 5, so five runs happened not four" % p,
          "%d / %d" % (execs["Passed"], sel))
    check("tests failed out of" not in c,
          "repeat %s: the failure clause is absent" % p)
    check(count(c, r"Test\s+#\d+: compile_fail\.") == 0,
          "repeat %s: compile_fail stays excluded" % p)
    check(count(c, r"Test\s+#\d+: unit\.CfdTransient_") == 24 * 5,
          "repeat %s: all 24 transient cases ran five times" % p)
    check(count(c, r"Test\s+#\d+: unit\.CfdTimeGrid_") == 10 * 5,
          "repeat %s: all 10 grid cases ran five times" % p)

# ---------------------------------------------------------------- binaries
print()
print("== per-tag assertion identity, read FROM the three qualified binaries ==")
readout_path = Q + "/binary-readout.txt"
if not os.path.exists(readout_path):
    check(False, "binary-readout.txt exists", readout_path)
else:
    r = read(readout_path)
    per_preset = {}
    current = None
    tag = None
    for ln in r.splitlines():
        m = re.match(r"preset\s+(\S+)", ln)
        if m:
            current = m.group(1)
            per_preset[current] = {}
            continue
        m = re.match(r"--- (\[.*\])", ln)
        if m:
            tag = m.group(1)
            continue
        m = re.search(r"All tests passed \((\d+) assertions? in (\d+) test cases?\)", ln)
        if m and current and tag:
            per_preset[current][tag] = (int(m.group(1)), int(m.group(2)))
            tag = None
    for p in PRESETS:
        check(p in per_preset and len(per_preset[p]) == 6,
              "%s: six tags read off the binary" % p,
              "read %d" % len(per_preset.get(p, {})))
    if all(p in per_preset for p in PRESETS):
        base = per_preset[PRESETS[0]]
        for p in PRESETS[1:]:
            check(per_preset[p] == base,
                  "%s agrees with %s on every tag" % (p, PRESETS[0]),
                  "differs: %s" % {k: (base.get(k), per_preset[p].get(k))
                                   for k in set(base) | set(per_preset[p])
                                   if base.get(k) != per_preset[p].get(k)})
        for t, want in sorted(predicted_tags.items()):
            got = base.get(t)
            check(got == want, "%s == %d assertions in %d cases" % ((t,) + want),
                  "binary says %s" % (got,))
        # the arithmetic a passing suite cannot make
        cfd = base.get("[cfd]")
        tr = base.get("[cfd][transient]")
        if cfd and tr:
            check(cfd[0] - tr[0] == 753230 and cfd[1] - tr[1] == 663,
                  "[cfd] minus the new suite == the P19-STEADY-001 baseline",
                  "%d - %d = %d, %d - %d = %d" % (cfd[0], tr[0], cfd[0] - tr[0],
                                                  cfd[1], tr[1], cfd[1] - tr[1]))

# ---------------------------------------------------------------- verdict
print()
print("=" * 70)
if failures:
    print("RECONCILIATION FAILED: %d check(s)" % len(failures))
    for f in failures:
        print("   " + f)
    sys.exit(1)
print("RECONCILIATION PASSED: every check agrees with the gate's own logs")
print("candidate commit: %s" % candidate)
sys.exit(0)

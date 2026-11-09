import io
import re
import sys

Q = r"C:/Users/Hasib/Desktop/CAD/docs/verification/P19-STEADY-001/qualification/"
SINGLE = ["debug-ext", "release-ext", "debug-shared-ext"]
REPEAT = ["release-ext", "debug-ext"]
EXPECT_SINGLE = 6903
EXPECT_REPEAT_SELECTED = 3493
EXPECT_REPEAT_EXEC = 17465

bad = []


def check(label, got, want):
    ok = got == want
    print("    %-34s %-12s %s" % (label, got, "OK" if ok else "MISMATCH, expected %s" % (want,)))
    if not ok:
        bad.append("%s: got %s want %s" % (label, got, want))


def read(p):
    return io.open(Q + p, encoding="utf-8", errors="replace").read()


print("=" * 72)
print("STAGES")
print("=" * 72)
times = read("qualification-times.txt")
exits = re.findall(r"exit (\d+) Mon", times)
check("stage lines with an exit code", len(exits), 17)
check("stages that exited 0", sum(1 for e in exits if e == "0"), 17)
check("verdict: 0 stage(s) failed", times.rstrip().endswith("0 stage(s) failed"), True)
out = read("run-qualification.out")
check("harness sentence present", "Qualification passed: every stage exited 0." in out, True)
check("the word FAILED absent", "QUALIFICATION FAILED" in out, False)

print()
print("=" * 72)
print("THE THREE SINGLE-RUN CTEST LOGS -- expect %d four ways" % EXPECT_SINGLE)
print("=" * 72)
for p in SINGLE:
    s = read("ctest-%s.log" % p)
    print("  %s" % p)
    starts = len(re.findall(r"^ *Start +\d+: ", s, re.M))
    # ctest right-pads the counter to the width of the total, so a four-digit
    # counter has NO leading whitespace. P19-LINSOLVE-001 counted 999 by
    # requiring it.
    progress = len(re.findall(r"^ *\d+/%d Test +#\d+: " % EXPECT_SINGLE, s, re.M))
    results = len(re.findall(r"Test +#\d+: \S", s))
    passed = len(re.findall(r"\sPassed\s", s))
    check("Start markers", starts, EXPECT_SINGLE)
    check("progress-prefixed lines", progress, EXPECT_SINGLE)
    check("result lines", results, EXPECT_SINGLE)
    check("Passed", passed, EXPECT_SINGLE)
    check("***Failed markers", len(re.findall(r"\*\*\*Failed", s)), 0)
    check("***Exception markers", len(re.findall(r"\*\*\*Exception", s)), 0)
    check("***Timeout markers", len(re.findall(r"\*\*\*Timeout", s)), 0)
    check("tests-failed line absent", "tests failed out of" in s, False)
    m = re.search(r"(\d+)% tests passed, (\d+) tests failed out of (\d+)", s)
    if m:
        check("ctest summary", m.group(0), "100%% tests passed, 0 tests failed out of %d" % EXPECT_SINGLE)
    else:
        m2 = re.search(r"100% tests passed, 0 tests failed out of (\d+)", s)
        check("ctest summary total", m2.group(1) if m2 else "ABSENT", str(EXPECT_SINGLE))
    print()

print("=" * 72)
print("THE TWO REPEAT LOGS -- executions, not progress lines")
print("=" * 72)
for p in REPEAT:
    s = read("ctest-repeat-%s.log" % p)
    print("  %s" % p)
    starts = len(re.findall(r"^ *Start +\d+: ", s, re.M))
    results = len(re.findall(r"Test +#\d+: \S", s))
    passed = len(re.findall(r"\sPassed\s", s))
    progress = len(re.findall(r"^ *\d+/%d Test +#\d+: " % EXPECT_REPEAT_SELECTED, s, re.M))
    check("Start markers (executions)", starts, EXPECT_REPEAT_EXEC)
    check("result lines (executions)", results, EXPECT_REPEAT_EXEC)
    check("Passed (executions)", passed, EXPECT_REPEAT_EXEC)
    check("progress-prefixed (selected)", progress, EXPECT_REPEAT_SELECTED)
    ratio = starts / float(progress) if progress else 0.0
    check("ratio markers:progress", "%.3f" % ratio, "5.000")
    check("***Failed markers", len(re.findall(r"\*\*\*Failed", s)), 0)
    check("tests-failed line absent", "tests failed out of" in s, False)
    print()

print("=" * 72)
print("THE THREE BUILD LOGS -- 0 error:, 0 warning: under -Werror")
print("=" * 72)
for p in SINGLE:
    s = read("build-%s.log" % p)
    print("  %s" % p)
    err = re.findall(r"error:", s)
    warn = re.findall(r"warning:", s)
    check("error: occurrences", len(err), 0)
    check("warning: occurrences", len(warn), 0)
    # The only permitted substring hit for the WORD error is the filename.
    hits = set(re.findall(r"\S*[Ee]rror\S*", s))
    unexpected = sorted(h for h in hits if "Error.cpp" not in h and "ErrorTests" not in h
                        and "Error.hpp" not in h and "error" not in h.lower().replace("error.cpp", ""))
    check("substring hits beyond Error.* files", len(unexpected), 0)
    if unexpected:
        for h in unexpected[:10]:
            print("        %s" % h)
    print()

print("=" * 72)
print("THE EIGHT IDENTITIES -- harness before first build vs after last test")
print("=" * 72)
blocks = re.findall(r"qualified source trees (?:before the first build|after the last test run): \s*((?:\s+\S+ [0-9a-f]{40} \s*)+)", times)
check("identity blocks in the times file", len(blocks), 2)
if len(blocks) == 2:
    first = dict(re.findall(r"(\S+) ([0-9a-f]{40})", blocks[0]))
    last = dict(re.findall(r"(\S+) ([0-9a-f]{40})", blocks[1]))
    check("paths in the first reading", len(first), 8)
    check("paths in the last reading", len(last), 8)
    check("first reading == last reading", first == last, True)
    for k in sorted(first):
        print("    %-18s %s%s" % (k, first[k], "" if first.get(k) == last.get(k) else "  <-- MOVED"))

print()
print("=" * 72)
if bad:
    print("RECONCILIATION: %d MISMATCH(ES)" % len(bad))
    for b in bad:
        print("  %s" % b)
    sys.exit(1)
print("RECONCILIATION: every figure above matches the freeze.")

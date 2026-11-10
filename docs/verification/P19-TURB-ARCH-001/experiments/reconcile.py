"""Reconcile P19-TURB-ARCH-001's qualification logs against FREEZE.md.

A green ctest run says every assertion that ran held. It does not say the right
number of assertions ran, that the tree which ran was the tree that was frozen,
that the builds were clean under -Werror, or that the repeat stage ran five
times over a non-empty filter. This script reads every figure out of FREEZE.md
-- never out of a literal in this file -- and checks it against the harness's
own logs.

Run:  python -I reconcile.py
Exit: 0 only if every check holds. Any failure prints and sets exit 1.

Falsified by reconcile-falsification.py, which sabotages the inputs and
requires this script to catch each one.
"""

import hashlib
import io
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
EVID = os.path.dirname(HERE)
QUAL = os.path.join(EVID, 'qualification')
REPO = os.path.abspath(os.path.join(EVID, '..', '..', '..'))

FAILURES = []
CHECKS = [0]


def read(path):
    with io.open(path, encoding='utf-8', errors='replace', newline='') as f:
        return f.read()


def check(label, ok, detail=''):
    CHECKS[0] += 1
    if ok:
        print('  ok    %s' % label)
    else:
        print('  FAIL  %s   %s' % (label, detail))
        FAILURES.append(label)


def eq(label, got, want):
    check(label, got == want, 'got %r, want %r' % (got, want))


# --------------------------------------------------------------- FREEZE.md
FREEZE = read(os.path.join(EVID, 'FREEZE.md'))


def one(pattern, where=None, label=None):
    """Exactly one match of a single capturing group, or it is a failure."""
    text = FREEZE if where is None else where
    found = re.findall(pattern, text)
    if len(found) != 1:
        check(label or pattern, False,
              'pattern matched %d times, needs exactly 1' % len(found))
        return None
    return found[0]


def num(pattern, label=None):
    v = one(pattern, label=label)
    return None if v is None else int(v)


print('== figures parsed out of FREEZE.md ==')

F_FULL = num(r'full suite, per preset\s+(\d+) =')
F_FULL_BASE = num(r'full suite, per preset\s+\d+ = (\d+) \+')
F_UNIT = num(r'= \d+ \+ (\d+) unit\.CfdTurbulenceArchitecture_')
F_ARCHNEW = num(r'\+ (\d+) architecture\.turbulence-ownership')
F_CFNEW = num(r'\+ (\d+) compile_fail\.cfdturb')
F_REPEAT = num(r'repeat set, per preset\s+(\d+) =')
F_REPEAT_BASE = num(r'repeat set, per preset\s+\d+ = (\d+) \+')
F_EXEC = num(r'repeat EXECUTIONS\s+(\d+) =')
F_EXEC_MULT = num(r'repeat EXECUTIONS\s+\d+ = \d+ x (\d+)')
F_ARCH = num(r'architecture\s+(\d+) = \d+ \+ \d+')
F_ARCH_BASE = num(r'architecture\s+\d+ = (\d+) \+ \d+')
F_CF = num(r'compile_fail\s+(\d+) = \d+ \+ \d+')
F_CF_BASE = num(r'compile_fail\s+\d+ = (\d+) \+ \d+')
F_SCAN = num(r'real tree (\d+) files / 0 violations')
F_CANDIDATE = one(r'committed tree at\s*\n`([0-9a-f]{40})`')

for name, v in [('full suite', F_FULL), ('full base', F_FULL_BASE),
                ('new unit', F_UNIT), ('new architecture', F_ARCHNEW),
                ('new compile_fail', F_CFNEW), ('repeat set', F_REPEAT),
                ('repeat base', F_REPEAT_BASE), ('repeat executions', F_EXEC),
                ('repeat multiplier', F_EXEC_MULT),
                ('architecture total', F_ARCH), ('architecture base', F_ARCH_BASE),
                ('compile_fail total', F_CF), ('compile_fail base', F_CF_BASE),
                ('scanned files', F_SCAN)]:
    check('FREEZE.md states the %s figure' % name, v is not None)
check('FREEZE.md states the candidate commit', F_CANDIDATE is not None)

if FAILURES:
    print('\nFREEZE.md could not be parsed; the rest of the reconciliation '
          'would be meaningless.')
    sys.exit(1)

print('        full %d  repeat %d  executions %d  architecture %d  '
      'compile_fail %d' % (F_FULL, F_REPEAT, F_EXEC, F_ARCH, F_CF))

# the eight frozen identities, as the freeze records them
FROZEN = {}
for line in FREEZE.splitlines():
    m = re.match(r'^(apps|include|src|tests|examples|cmake|CMakeLists\.txt|'
                 r'CMakePresets\.json)\s+([0-9a-f]{40})\s*$', line.strip())
    if m:
        FROZEN[m.group(1)] = m.group(2)
EIGHT = ['apps', 'include', 'src', 'tests', 'examples', 'cmake',
         'CMakeLists.txt', 'CMakePresets.json']
eq('FREEZE.md records all eight identities', sorted(FROZEN), sorted(EIGHT))
if sorted(FROZEN) != sorted(EIGHT):
    print('\nthe freeze does not hold eight identities; stopping.')
    sys.exit(1)

# the per-tag table
TAGS = {}
for m in re.finditer(r'^\s*(\[[a-z\]\[]+\])\s+(\d+) assertions in\s+(\d+) test',
                     FREEZE, re.M):
    TAGS[m.group(1)] = (int(m.group(2)), int(m.group(3)))
for t in ['[cfd]', '[cfd][turbarch]', '[cfd][transient]', '[cfd][steady]',
          '[linalg]', '[structural]', '[thermal]']:
    check('FREEZE.md records the %s figures' % t, t in TAGS)

# the five byte-identical harness files and the sixth's hash
HARNESS_SHA1 = dict((f, h) for h, f in re.findall(
    r'^([0-9a-f]{40})  ([a-z\-]+\.(?:cmd|ps1))$', FREEZE, re.M))
eq('FREEZE.md lists five byte-identical harness files',
   len(HARNESS_SHA1), 5)
F_RUNQ_HASH = one(r'`git hash-object` is\s*\n`([0-9a-f]{40})`')
F_EXEC_BYTES = num(r'identical at\s*\n(\d+) bytes')

# ------------------------------------------------------------- arithmetic
print('\n== the arithmetic FREEZE.md asserts ==')
eq('full suite = base + the three new groups',
   F_FULL_BASE + F_UNIT + F_ARCHNEW + F_CFNEW, F_FULL)
eq('repeat set = base + unit + architecture, compile_fail excluded',
   F_REPEAT_BASE + F_UNIT + F_ARCHNEW, F_REPEAT)
eq('repeat executions = repeat set x multiplier',
   F_REPEAT * F_EXEC_MULT, F_EXEC)
eq('architecture total = base + the new ownership tests',
   F_ARCH_BASE + F_ARCHNEW, F_ARCH)
eq('compile_fail total = base + the new cases',
   F_CF_BASE + F_CFNEW, F_CF)
check('the new groups sum to the suite growth',
      F_UNIT + F_ARCHNEW + F_CFNEW == F_FULL - F_FULL_BASE)

m = re.search(r'\[cfd\]\s+(\d+) assertions in (\d+) test cases\s*\n\s*'
              r'= (\d+) \+ (\d+)\s+and\s+(\d+) \+ (\d+)', FREEZE)
check('FREEZE.md shows the [cfd] arithmetic', m is not None)
if m:
    tot_a, tot_c, base_a, add_a, base_c, add_c = [int(g) for g in m.groups()]
    eq('[cfd] assertions = baseline + the new suite', base_a + add_a, tot_a)
    eq('[cfd] cases = baseline + the new suite', base_c + add_c, tot_c)
    eq('the [cfd] growth is exactly the turbarch suite',
       (add_a, add_c), TAGS.get('[cfd][turbarch]'))
    eq('the [cfd] total matches the per-tag table', (tot_a, tot_c),
       TAGS.get('[cfd]'))
    eq('the new cases are the new unit tests', add_c, F_UNIT)

# ------------------------------------------------------- the times file
print('\n== qualification-times.txt, the harness\'s failed-stage count ==')
TIMES = read(os.path.join(QUAL, 'qualification-times.txt'))

failed = re.findall(r'(\d+) stage\(s\) failed', TIMES)
eq('the times file states its failed-stage count exactly once',
   len(failed), 1)
if failed:
    eq('ZERO stages failed', failed[0], '0')

cand = one(r'qualification candidate:\s*\n\s*([0-9a-f]{40})', TIMES,
           'the times file names one candidate')
eq('the candidate is the commit the freeze names', cand, F_CANDIDATE)

exits = re.findall(r'exit (\d+)', TIMES)
# 3 presets x 5 stages (configure, clean, build, no-op rebuild, ctest)
# + 2 repeat stages = 17. A stage with no recorded exit is a stage that did
# not run, which a failed-stage count of zero would not reveal.
MIN_EXITS = 3 * 5 + 2
check('the times file records every stage exit (%d or more)' % MIN_EXITS,
      len(exits) >= MIN_EXITS, 'found %d' % len(exits))
nonzero = [e for e in exits if e != '0']
eq('every recorded stage exit is 0', nonzero, [])

PRESETS = ['debug-ext', 'release-ext', 'debug-shared-ext']
for p in PRESETS:
    for stage in ['configure', 'clean attempt 1', 'build', 'no-op rebuild',
                  'ctest']:
        pat = r'%s %s exit 0' % (re.escape(p), re.escape(stage))
        check('%s %s exited 0' % (p, stage),
              re.search(pat, TIMES) is not None)

for p in ['release-ext', 'debug-ext']:
    sel = one(r'repeat %s selects (\d+) test\(s\)' % re.escape(p), TIMES,
              'repeat %s records its selection' % p)
    if sel is not None:
        eq('repeat %s selected the frozen repeat set' % p, int(sel), F_REPEAT)
    check('repeat %s exited 0' % p,
          re.search(r'repeat %s exit 0' % re.escape(p), TIMES) is not None)

# identity before and after, inside the times file itself
before = re.search(r'before the first build:\s*(.*?)\n[a-z]', TIMES, re.S)
after = re.search(r'after the last test run:\s*(.*?)\nqualification finished',
                  TIMES, re.S)
check('the times file reads identity before the first build',
      before is not None)
check('the times file reads identity after the last test run',
      after is not None)


def parse_ids(block):
    out = {}
    for mm in re.finditer(r'(apps|include|src|tests|examples|cmake|'
                          r'CMakeLists\.txt|CMakePresets\.json)\s+'
                          r'([0-9a-f]{40})', block):
        out[mm.group(1)] = mm.group(2)
    return out


if before and after:
    b, a = parse_ids(before.group(1)), parse_ids(after.group(1))
    eq('the pre-run reading holds eight paths', len(b), 8)
    eq('the post-run reading holds eight paths', len(a), 8)
    eq('NOTHING MOVED DURING THE RUN', b, a)
    eq('the run\'s tree is the frozen tree', b, FROZEN)

# ------------------------------------------------------ run-qualification.out
print('\n== run-qualification.out, the harness\'s own sentence ==')
OUT = read(os.path.join(QUAL, 'run-qualification.out'))
check('the harness states that qualification passed',
      'Qualification passed: every stage exited 0.' in OUT)
check('the harness states no failure', 'Qualification FAILED' not in OUT
      and 'stage(s) failed' not in OUT)
check('the disk-space preflight passed',
      re.search(r'RESULT: OK\. Every distinct volume has at least \d+ GB free\.',
                OUT) is not None)

# ---------------------------------------------------------- the ctest logs
print('\n== the three full-suite ctest logs ==')
PASSED_LINE = re.compile(r'^\s*\d+/\d+ Test\s+#\d+: .*?\s+Passed\s', re.M)
TESTNUM = re.compile(r'Test\s+#\d+:', re.M)
STARTLINE = re.compile(r'^\s*Start\s+\d+: ', re.M)


def started(log, name):
    return len(re.findall(r'^\s*Start\s+\d+: %s' % re.escape(name),
                          log, re.M))
BAD = re.compile(r'\*\*\*(Failed|Not Run|Timeout|Exception|Skipped)')

GROUPS = [('architecture.turbulence-ownership', F_ARCHNEW),
          ('unit.CfdTurbulenceArchitecture_', F_UNIT),
          ('compile_fail.cfdturb.', F_CFNEW)]

for p in PRESETS:
    log = read(os.path.join(QUAL, 'ctest-%s.log' % p))
    print('  -- %s' % p)
    check('%s: ctest printed 100%% passed out of %d' % (p, F_FULL),
          '100%% tests passed out of %d' % F_FULL in log)
    check('%s: the string "tests failed out of" is absent' % p,
          'tests failed out of' not in log)
    eq('%s: counted Passed lines' % p, len(PASSED_LINE.findall(log)), F_FULL)
    eq('%s: counted "Test #" reports' % p, len(TESTNUM.findall(log)), F_FULL)
    eq('%s: no failure markers' % p, BAD.findall(log), [])
    eq('%s: every test that reported also started' % p,
       len(STARTLINE.findall(log)), F_FULL)
    for name, n in GROUPS:
        got = len(re.findall(r'Test\s+#\d+: %s' % re.escape(name), log))
        eq('%s: %s ran %d' % (p, name, n), got, n)
        eq('%s: %s STARTED %d, not just reported' % (p, name, n),
           started(log, name), n)
    eq('%s: architecture group total' % p,
       len(re.findall(r'Test\s+#\d+: architecture\.', log)), F_ARCH)
    eq('%s: compile_fail group total' % p,
       len(re.findall(r'Test\s+#\d+: compile_fail\.', log)), F_CF)
    check('%s: the real-tree ownership scan ran and passed' % p,
          re.search(r'Test\s+#\d+: architecture\.turbulence-ownership '
                    r'\.+\s+Passed', log) is not None)

print('\n== the two repeat logs ==')
for p in ['release-ext', 'debug-ext']:
    log = read(os.path.join(QUAL, 'ctest-repeat-%s.log' % p))
    print('  -- repeat %s' % p)
    check('repeat %s: 100%% passed out of %d' % (p, F_REPEAT),
          '100%% tests passed out of %d' % F_REPEAT in log)
    check('repeat %s: "tests failed out of" absent' % p,
          'tests failed out of' not in log)
    eq('repeat %s: %d executions' % (p, F_EXEC),
       len(TESTNUM.findall(log)), F_EXEC)
    eq('repeat %s: no failure markers' % p, BAD.findall(log), [])
    eq('repeat %s: every execution that reported also started' % p,
       len(STARTLINE.findall(log)), F_EXEC)
    for name, n in [('architecture.turbulence-ownership', F_ARCHNEW),
                    ('unit.CfdTurbulenceArchitecture_', F_UNIT)]:
        got = len(re.findall(r'Test\s+#\d+: %s' % re.escape(name), log))
        eq('repeat %s: %s ran %d x %d' % (p, name, n, F_EXEC_MULT),
           got, n * F_EXEC_MULT)
        eq('repeat %s: %s STARTED %d x %d' % (p, name, n, F_EXEC_MULT),
           started(log, name), n * F_EXEC_MULT)
    eq('repeat %s: compile_fail stayed out of the repeat set' % p,
       len(re.findall(r'Test\s+#\d+: compile_fail\.', log)), 0)

# ---------------------------------------------------------- the build logs
print('\n== the three build logs, under -Werror ==')
for p in PRESETS:
    log = read(os.path.join(QUAL, 'build-%s.log' % p))
    errs = re.findall(r'(?im)^.*\berror:', log)
    warns = re.findall(r'(?im)^.*\bwarning:', log)
    eq('%s: zero compiler errors' % p, len(errs), 0)
    eq('%s: zero compiler warnings' % p, len(warns), 0)
    edges = re.findall(r'^\[(\d+)/(\d+)\]', log, re.M)
    check('%s: the build ran edges and finished the last one' % p,
          bool(edges) and edges[-1][0] == edges[-1][1],
          'last edge %s' % ((edges[-1] if edges else None),))
    rebuild = read(os.path.join(QUAL, 'rebuild-%s.log' % p))
    comp = re.findall(r'(?i)(Building|Linking|Compiling) ', rebuild)
    eq('%s: the no-op rebuild compiled and linked nothing' % p, comp, [])
    check('%s: the no-op rebuild is the git-revision edge only' % p,
          'Checking git revision' in rebuild)
    clean = read(os.path.join(QUAL, 'clean-%s.log' % p))
    check('%s: the clean stage logged' % p, len(clean.strip()) > 0)
    conf = read(os.path.join(QUAL, 'configure-%s.log' % p))
    check('%s: configure resolved OpenCASCADE under the deps prefix' % p,
          'bettercad-deps' in conf, conf.strip()[:120])

# ------------------------------------------------------------- the harness
print('\n== the harness files ==')
for fname, want in sorted(HARNESS_SHA1.items()):
    path = os.path.join(QUAL, fname)
    if not os.path.exists(path):
        check('%s exists' % fname, False)
        continue
    with open(path, 'rb') as fh:
        got = hashlib.sha1(fh.read()).hexdigest()
    eq('%s is the byte-identical carried-forward copy' % fname, got, want)

runq = os.path.join(QUAL, 'run-qualification.cmd')
got = subprocess.check_output(
    ['git', 'hash-object', runq], cwd=REPO).decode('ascii').strip()
eq('run-qualification.cmd is the object the freeze names', got, F_RUNQ_HASH)

body = read(runq)
check('run-qualification.cmd holds exactly one executable half',
      body.count('@echo off') == 1)
EXE = body[body.index('@echo off'):]
eq('its executable half is the size the freeze asserts',
   len(EXE.encode('utf-8')), F_EXEC_BYTES)

# The stronger reading: not a size, an identity against the carried-forward
# file. A harness change that happened to keep the byte count would pass a
# size check and fail this one.
prev_runq = os.path.join(EVID, '..', 'P19-TRANSIENT-001', 'qualification',
                         'run-qualification.cmd')
check('the previous launcher is on disk to compare against',
      os.path.exists(prev_runq))
if os.path.exists(prev_runq):
    prev = read(prev_runq)
    prev_exe = prev[prev.index('@echo off'):]
    check('THE EXECUTABLE HALF IS BYTE-IDENTICAL TO P19-TRANSIENT-001\'S',
          EXE == prev_exe,
          'this milestone %d bytes, previous %d'
          % (len(EXE.encode('utf-8')), len(prev_exe.encode('utf-8'))))
    check('and the two headers DO differ, so this really is a header change',
          prev[:prev.index('@echo off')] != body[:body.index('@echo off')])

# the launcher must carry THIS milestone's figures, not a stale prediction
hdr = body
for v in [F_FULL, F_REPEAT, F_ARCH]:
    check('the launcher header carries the figure %d' % v, str(v) in hdr)

# ------------------------------------------------------- the live tree
print('\n== the live tree, read a third time and independently ==')
idx = os.path.join(os.environ.get('TEMP', '.'), 'bettercad-reconcile-index')
if os.path.exists(idx):
    os.remove(idx)
env = dict(os.environ, GIT_INDEX_FILE=idx)
subprocess.check_call(['git', 'read-tree', 'HEAD'], cwd=REPO, env=env)
subprocess.check_call(['git', 'add', '-A'] + EIGHT, cwd=REPO, env=env,
                      stderr=subprocess.DEVNULL)
live = {}
for d in ['apps', 'include', 'src', 'tests', 'examples', 'cmake']:
    live[d] = subprocess.check_output(
        ['git', 'write-tree', '--prefix=%s/' % d],
        cwd=REPO, env=env).decode('ascii').strip()
for line in subprocess.check_output(
        ['git', 'ls-files', '-s', 'CMakeLists.txt', 'CMakePresets.json'],
        cwd=REPO, env=env).decode('ascii').splitlines():
    parts = line.split()
    live[parts[3]] = parts[1]
os.remove(idx)
eq('the live tree still holds eight paths', len(live), 8)
eq('THE QUALIFIED TREE IS STILL THE TREE ON DISK', live, FROZEN)

# ------------------------------------- the fifth reading, out of the commit
# This is the only place `git rev-parse HEAD:<path>` is used. Before the
# milestone's commit it reads the PREVIOUS commit's tree and would agree with
# itself for the wrong reason, which is why the other four readings go through
# an index. It is meaningful only once the commit exists.
print('\n== the commit object, the fifth reading ==')

# The commit is named by the evidence, not by HEAD. Anchoring to HEAD~1 made
# this section true for exactly one commit: the record commit that follows
# moved HEAD on and turned it false. See L15.
README = read(os.path.join(EVID, 'README.md'))
m = re.search(r'qualified and committed as\s+([0-9a-f]{40})', README)
check('README.md names the qualification commit', m is not None)
if m is None:
    print('\nwithout the commit hash the fifth reading cannot be made.')
    sys.exit(1)
QUALIFIED = m.group(1)
print('        qualification commit %s' % QUALIFIED[:12])


def tree_of(rev):
    out = {}
    for p8 in EIGHT:
        out[p8] = subprocess.check_output(
            ['git', 'rev-parse', '%s:%s' % (rev, p8)],
            cwd=REPO).decode('ascii').strip()
    return out


check('the qualification commit exists in this repository',
      subprocess.call(['git', 'cat-file', '-e', QUALIFIED + '^{commit}'],
                      cwd=REPO, stdout=subprocess.DEVNULL,
                      stderr=subprocess.DEVNULL) == 0)
committed = tree_of(QUALIFIED)
eq('the qualification commit holds eight paths', len(committed), 8)
eq('THE QUALIFIED TREE IS THE COMMITTED TREE', committed, FROZEN)

# the no-production-change claim is that commit against ITS OWN parent
parent = tree_of(QUALIFIED + '~1')
eq('its parent holds eight paths', len(parent), 8)
moved = [k for k in EIGHT if parent[k] != committed[k]]
eq('exactly one frozen path moved in the qualification commit',
   moved, ['tests'])
check('include is BIT-IDENTICAL across it -- no production type was added',
      parent['include'] == committed['include'],
      '%s -> %s' % (parent['include'], committed['include']))
check('src is BIT-IDENTICAL across it -- no production code was added',
      parent['src'] == committed['src'],
      '%s -> %s' % (parent['src'], committed['src']))

# and what HEAD can tell us, which is only that nothing has disturbed the
# frozen paths since -- commits after the qualification are allowed, but they
# must not touch the qualified surface
head = tree_of('HEAD')
eq('nothing since the qualification has touched the frozen surface',
   head, committed)
eq('and the working tree still agrees with HEAD', head, live)

# ----------------------------------------------- the checker, on this tree
print('\n== the ownership checker, re-run against the live tree ==')
cm = subprocess.run(
    ['cmake', '-DSOURCE_DIR=%s' % REPO, '-P',
     os.path.join(REPO, 'tests', 'architecture',
                  'CheckTurbulenceOwnership.cmake')],
    cwd=REPO, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
text = cm.stdout.decode('utf-8', 'replace')
eq('the checker exits 0 on the qualified tree', cm.returncode, 0)
m = re.search(r'check passed: (\d+) files scanned, (\d+) violations', text)
check('the checker reports a scan it can be held to', m is not None, text[:200])
if m:
    eq('it scanned the number of files the freeze records',
       int(m.group(1)), F_SCAN)
    eq('it found zero violations', int(m.group(2)), 0)
    check('the count is the 3+ digits its ctest regex requires',
          len(m.group(1)) >= 3)

# ------------------------------------------------------------------ verdict
print('\n' + '=' * 70)
print('%d checks run' % CHECKS[0])
if FAILURES:
    print('%d FAILED:' % len(FAILURES))
    for f in FAILURES:
        print('   - %s' % f)
    sys.exit(1)
print('ALL CHECKS HOLD.  The qualification logs, the freeze and the tree on '
      'disk agree.')
sys.exit(0)

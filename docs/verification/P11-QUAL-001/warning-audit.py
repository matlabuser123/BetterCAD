"""Audits the qualification build logs for compiler warnings.

"Zero compiler warnings" means the compiler emitted none, not that the word
"warning" is absent from a log. This separates the kinds:

* compiler diagnostics, in GCC's format `<file>:<line>:<col>: warning: ...`
  (or `error:`), which the presets turn into errors anyway
  (-Werror, BETTERCAD_WARNINGS_AS_ERRORS=ON);
* CMake warnings, which begin with `CMake Warning`;
* any other line that merely contains the word, e.g. a file or test name,
  listed so nothing is dismissed silently.

    python warning-audit.py <build log> [more logs ...]
"""
import re
import sys

COMPILER = re.compile(r"^.*?:\d+:\d+: (warning|error): ", re.M)
CMAKE = re.compile(r"^CMake (Warning|Error)", re.M)
MENTIONS = re.compile(r"^.*\bwarning\b.*$", re.I | re.M)


def main(*logs):
    print("P11-QUAL-001 compiler warning audit")
    print("Compiler diagnostics are counted as GCC prints them: <file>:<line>:<column>: warning|error:")
    print("CMake warnings and any other mention of the word are counted separately.")
    print()
    total = 0
    for log in logs:
        text = open(log, encoding="utf-8", errors="replace").read()
        compiled = len(re.findall(r"^\[\d+/\d+\] Building CXX object", text, re.M))
        compiler = COMPILER.findall(text)
        cmake = CMAKE.findall(text)
        other = [line for line in MENTIONS.findall(text)
                 if not COMPILER.match(line) and not CMAKE.match(line)]
        total += len(compiler)
        print(f"== {log}")
        print(f"   translation units compiled : {compiled}")
        print(f"   compiler warnings or errors: {len(compiler)}")
        print(f"   CMake warnings or errors   : {len(cmake)}")
        print(f"   other lines mentioning it  : {len(other)}")
        for line in compiler[:20]:
            print(f"     compiler: {line}")
        for line in other[:20]:
            print(f"     other: {line.strip()[:160]}")
        print()
    print(f"Compiler warnings over all logs: {total}")
    return 0


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))

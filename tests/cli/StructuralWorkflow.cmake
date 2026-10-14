# The scripted headless structural workflow (P17-CLI-001).
#
# WHAT THIS IS FOR, and why it is not a unit test. The in-process tests call
# cli::run() directly, which proves the command logic. They cannot prove that
# the EXECUTABLE works: argument parsing from a real command line, document
# loading from a real path, the runtime closure actually loading, the exit code
# reaching the shell, and stdout being clean enough to parse. Every one of
# those has broken something in this project before.
#
# AND IT IS GENUINELY MULTI-PROCESS. Each step is a separate process that
# reloads the document from disk, which is the only way to prove the thing the
# brief's §56-58 ask for: that a structural result is NOT persisted, so a fresh
# process must solve again rather than finding one.
#
# IT PARSES, IT DOES NOT GREP. CMake's `string(JSON)` is a real JSON parser and
# a malformed payload is a fatal error, so a backend warning, a progress line
# or a debug banner anywhere in stdout fails this script rather than slipping
# past a regex that still happened to match. That is the brief's §122 and §161,
# and it is stronger than the P16 sibling, which asserts fields with
# `string(MATCHES)`.
#
# IT FAILS FAST. Each step asserts its exit code before the next runs, and any
# mismatch is a fatal error: a script that carried on after a failure and then
# reported success would be worse than no script.
#
# Inputs:
#   CLI         the executable to run -- an ABSOLUTE path, never a bare name,
#               so a stale binary on PATH cannot be tested by accident
#   SOURCE      a .bcad to copy and work on
#   WORK        a scratch directory
#   SOURCE_NO_ANALYSIS
#               a .bcad with no structural analysis, for the negative case
cmake_minimum_required(VERSION 3.28)

foreach(required CLI SOURCE WORK SOURCE_NO_ANALYSIS)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "StructuralWorkflow.cmake: ${required} is required")
    endif()
endforeach()
if(NOT EXISTS "${CLI}")
    message(FATAL_ERROR "the CLI does not exist: ${CLI}")
endif()

# FRESH-BINARY PROOF (the brief's §113 and §163). Recorded in the log so the
# evidence names the executable that actually ran rather than the one that was
# meant to. The path is absolute because CMake gave it as $<TARGET_FILE:...>,
# which is a build-tree path and cannot resolve to an installed copy.
file(SIZE "${CLI}" _cli_size)
file(SHA256 "${CLI}" _cli_hash)
message(STATUS "CLI:    ${CLI}")
message(STATUS "size:   ${_cli_size} bytes")
message(STATUS "sha256: ${_cli_hash}")

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
set(DOC "${WORK}/workflow.bcad")
configure_file("${SOURCE}" "${DOC}" COPYONLY)

set(_steps 0)

# Runs the CLI as its own process and asserts the exit code.
#
# THE EXIT CODE IS CAPTURED SEPARATELY FROM THE OUTPUT (the brief's §119 and
# §120): execute_process gives RESULT_VARIABLE and OUTPUT_VARIABLE
# independently, so there is no pipeline whose exit status could be a parser's
# instead of BetterCAD's. Both are asserted.
function(step expected_code out_var)
    set(_args ${ARGN})
    execute_process(
        COMMAND "${CLI}" ${_args}
        RESULT_VARIABLE _code
        OUTPUT_VARIABLE _out
        ERROR_VARIABLE _err)
    math(EXPR _n "${_steps} + 1")
    set(_steps ${_n} PARENT_SCOPE)
    if(NOT _code STREQUAL "${expected_code}")
        message(FATAL_ERROR
            "step ${_n} FAILED: expected exit ${expected_code}, got ${_code}\n"
            "  command: ${_args}\n  stdout: ${_out}\n  stderr: ${_err}")
    endif()
    message(STATUS "step ${_n}: exit ${_code}  ${_args}")
    set(${out_var} "${_out}" PARENT_SCOPE)
    set(_last_err "${_err}" PARENT_SCOPE)
endfunction()

# PARSES the payload and returns one member. A malformed payload is fatal
# here, which is the whole gate: `string(JSON)` without an ERROR_VARIABLE
# stops the script, so any contamination of stdout fails rather than being
# filtered away. No tail, no sed, no grep -- the brief's §121 and §174.
function(member json out_var)
    string(JSON _value GET "${json}" ${ARGN})
    message(STATUS "  ${ARGN} = ${_value}")
    set(${out_var} "${_value}" PARENT_SCOPE)
endfunction()

# Asserts that stdout parses as one JSON object and nothing else.
function(parses json)
    string(JSON _type TYPE "${json}")
    if(NOT _type STREQUAL "OBJECT")
        message(FATAL_ERROR "stdout is not a JSON object but a ${_type}:\n${json}")
    endif()
endfunction()

function(same_number what a b)
    if(NOT a STREQUAL "${b}")
        message(FATAL_ERROR "${what} differs between processes: '${a}' vs '${b}'")
    endif()
    message(STATUS "  ${what} agrees across processes: ${a}")
endfunction()

# ---------------------------------------------------------------------------
# The valid multi-process workflow
# ---------------------------------------------------------------------------

# 1. PROCESS 1: inspect the analysis the hand-written document carries.
step(0 _settings structural-settings "${DOC}" --json)
parses("${_settings}")
member("${_settings}" _mode mode)
member("${_settings}" _loads_before loads)
string(JSON _load_count LENGTH "${_settings}" loads)
string(JSON _restraint_count LENGTH "${_settings}" restraints)
message(STATUS "  loads=${_load_count} restraints=${_restraint_count}")
if(NOT _load_count EQUAL 2)
    message(FATAL_ERROR "expected the fixture's two loads, found ${_load_count}")
endif()

# 2. PROCESS 2: add a load. A separate process, which loads and saves.
step(0 _out structural-load-add "${DOC}" 4 --traction "0,0,-1.5MPa"
        --face face:Extrude001:side:6)

# 3. PROCESS 3: add a restraint. Another separate process.
step(0 _out structural-restraint-add "${DOC}" 2 face:Extrude001:side:7 uy,uz)

# 4. PROCESS 4: a FRESH process sees both edits, from the file alone.
step(0 _after structural-settings "${DOC}" --json)
parses("${_after}")
string(JSON _load_count LENGTH "${_after}" loads)
string(JSON _restraint_count LENGTH "${_after}" restraints)
if(NOT _load_count EQUAL 3)
    message(FATAL_ERROR "the added load did not persist: ${_load_count} loads")
endif()
if(NOT _restraint_count EQUAL 2)
    message(FATAL_ERROR "the added restraint did not persist: ${_restraint_count}")
endif()
# The traction's value survived the save/load, in SI, through the unit parser.
member("${_after}" _tz loads 2 traction z)
if(NOT _tz STREQUAL "-1500000")
    message(FATAL_ERROR "1.5MPa did not round-trip as -1500000 Pa but as ${_tz}")
endif()

# 5. PROCESS 5: validate, without solving.
step(0 _validation structural-validate "${DOC}" --json)
parses("${_validation}")
member("${_validation}" _acceptable acceptable)
if(NOT _acceptable STREQUAL "ON" AND NOT _acceptable STREQUAL "true")
    message(FATAL_ERROR "the workflow document is not acceptable: ${_validation}")
endif()

# ---------------------------------------------------------------------------
# A solve changes nothing on disk
# ---------------------------------------------------------------------------

file(SHA256 "${DOC}" _hash_before)
file(SIZE "${DOC}" _size_before)
message(STATUS "before the solve: ${_size_before} bytes, sha256 ${_hash_before}")

# 6. PROCESS 6: solve.
step(0 _solve structural-solve "${DOC}" --json)
parses("${_solve}")
member("${_solve}" _nodes nodeCount)
member("${_solve}" _tets tetrahedronCount)
member("${_solve}" _free freeEquations)
member("${_solve}" _energy strainEnergy value)
member("${_solve}" _maxu largestDisplacement value)
member("${_solve}" _maxvm largestVonMises value)
member("${_solve}" _energy_unit strainEnergy unit)
# UNITS ARE PART OF THE PAYLOAD (the brief's §178): automation never has to
# guess whether a stress is Pa or MPa.
member("${_solve}" _vm_unit largestVonMises unit)
if(NOT _vm_unit STREQUAL "Pa")
    message(FATAL_ERROR "the von Mises unit is '${_vm_unit}', not Pa")
endif()

file(SHA256 "${DOC}" _hash_after)
file(SIZE "${DOC}" _size_after)
message(STATUS "after the solve:  ${_size_after} bytes, sha256 ${_hash_after}")
if(NOT _hash_before STREQUAL "${_hash_after}")
    message(FATAL_ERROR
        "THE SOLVE CHANGED THE DOCUMENT. P17-PERSIST-001 forbids a result "
        "reaching the file (ADR-048).\n  before: ${_hash_before}\n  after:  ${_hash_after}")
endif()
message(STATUS "the document is byte-identical after the solve")

# ---------------------------------------------------------------------------
# The three summaries, each its own process, each solving for itself
# ---------------------------------------------------------------------------

# 7. PROCESS 7: displacements.
step(0 _displacement structural-displacement "${DOC}" --json)
parses("${_displacement}")
member("${_displacement}" _dmax magnitude max)
member("${_displacement}" _dnode largest node)
member("${_displacement}" _dunit magnitude unit)
same_number("max |u|" "${_dmax}" "${_maxu}")

# 8. PROCESS 8: stresses.
step(0 _stress structural-stress "${DOC}" --json)
parses("${_stress}")
member("${_stress}" _domain domain)
if(NOT _domain STREQUAL "element")
    message(FATAL_ERROR "the stress domain is '${_domain}', not element")
endif()
member("${_stress}" _svm vonMises max)
member("${_stress}" _selement largest element)
same_number("max von Mises" "${_svm}" "${_maxvm}")

# 9. PROCESS 9: reactions.
step(0 _reactions structural-reactions "${DOC}" --json)
parses("${_reactions}")
member("${_reactions}" _rz totalForce z)
member("${_reactions}" _runit totalForce unit)
member("${_reactions}" _nodes_constrained constrainedNodes)
if(NOT _runit STREQUAL "N")
    message(FATAL_ERROR "the reaction unit is '${_runit}', not N")
endif()

# ---------------------------------------------------------------------------
# A fresh process must solve again, and must agree
# ---------------------------------------------------------------------------

# 10. PROCESS 10: the same file, a new process, solved again from scratch.
#     Nothing was carried over: the result is not in the file and the previous
#     processes are gone.
step(0 _resolve structural-solve "${DOC}" --json)
parses("${_resolve}")
member("${_resolve}" _nodes2 nodeCount)
member("${_resolve}" _free2 freeEquations)
member("${_resolve}" _energy2 strainEnergy value)
member("${_resolve}" _maxu2 largestDisplacement value)
member("${_resolve}" _maxvm2 largestVonMises value)
same_number("node count" "${_nodes}" "${_nodes2}")
same_number("free equations" "${_free}" "${_free2}")
same_number("strain energy" "${_energy}" "${_energy2}")
same_number("max |u|" "${_maxu}" "${_maxu2}")
same_number("max von Mises" "${_maxvm}" "${_maxvm2}")
# The whole payload is byte-identical, which is the strong form of the same
# claim: the summary is deterministic across processes, not merely close.
if(NOT _solve STREQUAL "${_resolve}")
    message(FATAL_ERROR "two processes solved the same file and printed different payloads")
endif()
message(STATUS "two fresh processes produced byte-identical solve payloads")

# ---------------------------------------------------------------------------
# The negative half: every refusal class, through the real binary
# ---------------------------------------------------------------------------

# 11. An under-constrained analysis. Remove both restraints, then solve.
set(UNDER "${WORK}/under.bcad")
configure_file("${DOC}" "${UNDER}" COPYONLY)
step(0 _out structural-restraint-remove "${UNDER}" 1)
step(0 _out structural-restraint-remove "${UNDER}" 2)
step(1 _out structural-solve "${UNDER}" --json)
# "COULD NOT ANSWER" PRINTS NOTHING TO STDOUT, which is the stream discipline
# P15-CLI-001 established and which a pipeline depends on.
if(NOT _out STREQUAL "")
    message(FATAL_ERROR "a refused solve printed to stdout: ${_out}")
endif()
if(NOT _last_err MATCHES "insufficient_restraint")
    message(FATAL_ERROR "the diagnostic code is missing from stderr: ${_last_err}")
endif()
message(STATUS "  under-constrained: exit 1, stdout empty, insufficient_restraint on stderr")

# And every summary command refuses it too, because each solves through the
# same validated entry (the brief's §196).
foreach(_command structural-displacement structural-stress structural-reactions)
    step(1 _out ${_command} "${UNDER}" --json)
    if(NOT _out STREQUAL "")
        message(FATAL_ERROR "${_command} printed to stdout on a refused model: ${_out}")
    endif()
endforeach()

# 12. An unresolved target. A well-formed face reference naming no face loads
#     fine, is carried as intent, and then refuses the solve.
set(UNRESOLVED "${WORK}/unresolved.bcad")
configure_file("${DOC}" "${UNRESOLVED}" COPYONLY)
step(0 _out structural-restraint-add "${UNRESOLVED}" 9 face:Extrude001:side:9991 fixed)
step(0 _settings structural-settings "${UNRESOLVED}" --json)
parses("${_settings}")
# PRESERVED AND REPORTED AS UNRESOLVED, not dropped and not rebound.
string(JSON _restraints LENGTH "${_settings}" restraints)
if(NOT _restraints EQUAL 3)
    message(FATAL_ERROR "the unresolved restraint was dropped: ${_restraints} restraints")
endif()
member("${_settings}" _resolves restraints 2 faceResolves)
if(_resolves)
    message(FATAL_ERROR "a restraint on a face that does not exist reports as resolving")
endif()
step(1 _out structural-validate "${UNRESOLVED}" --json)
# THE REPORT IS THE ANSWER HERE, so the payload IS on stdout and the exit is
# still 1 -- which is the brief's §120, and is why both are asserted.
parses("${_out}")
member("${_out}" _code findings 0 code)
if(NOT _code MATCHES "restraints_unresolvable")
    message(FATAL_ERROR "expected restraints_unresolvable, got ${_code}")
endif()
step(1 _out structural-solve "${UNRESOLVED}" --json)

# 13. A document with no structural analysis at all.
step(1 _out structural-settings "${SOURCE_NO_ANALYSIS}" --json)
if(NOT _out STREQUAL "")
    message(FATAL_ERROR "a failed query printed to stdout: ${_out}")
endif()
if(NOT _last_err MATCHES "no_structural_analysis")
    message(FATAL_ERROR "the diagnostic code is missing: ${_last_err}")
endif()

# 14. A file that is not there.
step(1 _out structural-solve "${WORK}/does-not-exist.bcad" --json)
if(NOT _out STREQUAL "")
    message(FATAL_ERROR "a failed load printed to stdout: ${_out}")
endif()

# 15. Usage errors, which are exit 2 and never 1.
step(2 _out structural-solve)
step(2 _out structural-load-add "${DOC}" 7 --pressure abc --face face:Extrude001:end_cap)
step(2 _out structural-load-add "${DOC}" 7 --pressure nan --face face:Extrude001:end_cap)
step(2 _out structural-restraint-add "${DOC}" 7 face:Extrude001:end_cap rx)

# 16. A rejected edit writes nothing.
file(SHA256 "${DOC}" _hash_now)
step(1 _out structural-load-remove "${DOC}" 99)
file(SHA256 "${DOC}" _hash_still)
if(NOT _hash_now STREQUAL "${_hash_still}")
    message(FATAL_ERROR "a rejected edit changed the document")
endif()
message(STATUS "  a rejected edit left the file byte-identical")

# ---------------------------------------------------------------------------
# ZERO-MATCH PROTECTION for the script itself (the brief's §118)
# ---------------------------------------------------------------------------
#
# A workflow that skipped every step and exited 0 would be the same defect as a
# ctest filter that matched nothing. So the step COUNT is asserted: if a step
# is removed or an early return is introduced, this fails.
message(STATUS "steps executed: ${_steps}")
if(_steps LESS 25)
    message(FATAL_ERROR
        "only ${_steps} steps ran; the workflow is incomplete. A script that "
        "exits 0 having done nothing is the defect this check exists for.")
endif()

message(STATUS "StructuralWorkflow: ${_steps} steps, every exit code asserted, every payload parsed")

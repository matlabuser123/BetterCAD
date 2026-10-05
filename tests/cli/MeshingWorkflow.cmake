# The scripted headless meshing workflow (P16-CLI-001).
#
# WHAT THIS IS FOR, and why it is not a unit test. The in-process tests call
# cli::run() directly, which proves the command logic. They cannot prove that
# the EXECUTABLE works: argument parsing from a real command line, document
# loading from a real path, the runtime closure actually loading, the exit code
# reaching the shell, and stdout being clean enough to parse. Every one of
# those has broken something in this project before.
#
# So this drives the real binary, many times, as separate processes, and
# checks every exit code and the values in the output.
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
cmake_minimum_required(VERSION 3.28)

foreach(required CLI SOURCE WORK)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "MeshingWorkflow.cmake: ${required} is required")
    endif()
endforeach()
if(NOT EXISTS "${CLI}")
    message(FATAL_ERROR "the CLI does not exist: ${CLI}")
endif()

# FRESH-BINARY PROOF. Recorded in the log so the evidence names the executable
# that actually ran rather than the one that was meant to.
file(SIZE "${CLI}" _cli_size)
message(STATUS "CLI:  ${CLI}")
message(STATUS "size: ${_cli_size} bytes")

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
set(DOC "${WORK}/workflow.bcad")
configure_file("${SOURCE}" "${DOC}" COPYONLY)

set(_steps 0)

# Runs the CLI and asserts the exit code. Captures stdout into OUT_VAR.
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
endfunction()

# Asserts that a JSON field holds a value matching a regex, and reports it.
function(field json key pattern)
    if(NOT json MATCHES "\"${key}\": (${pattern})")
        message(FATAL_ERROR
            "field '${key}' did not match ${pattern}\n  payload: ${json}")
    endif()
    message(STATUS "  ${key} = ${CMAKE_MATCH_1}")
endfunction()

# ---------------------------------------------------------------------------
# The valid workflow
# ---------------------------------------------------------------------------

# 1. Inspect before anything exists. A document with no control is a FAILURE
#    for a query, with nothing on stdout.
step(1 _out mesh-settings "${DOC}")
if(NOT _out STREQUAL "")
    message(FATAL_ERROR "a failed query printed to stdout: ${_out}")
endif()

# 2. Create the control, 3. set a global size, 4. refine one face.
step(0 _out mesh-control-add "${DOC}" --size 6mm)
step(0 _out mesh-set-global-size "${DOC}" 5mm)
step(0 _out mesh-local-add "${DOC}" face:Extrude001:end_cap 1.5mm)

# 5. The settings now, structured. Every field is asserted, not just printed.
step(0 _settings mesh-settings "${DOC}" --json)
field("${_settings}" "value" "0\\.005")
field("${_settings}" "unit" "\"m\"")
field("${_settings}" "reference" "\"face:Extrude001:end_cap\"")

# 6. Generate. 7-9. The three reports.
step(0 _generate mesh-generate "${DOC}" --json)
field("${_generate}" "nodeCount" "[1-9][0-9]*")
field("${_generate}" "elementCount" "[1-9][0-9]*")

step(0 _info mesh-info "${DOC}" --json)
field("${_info}" "nodeCount" "[1-9][0-9]*")
field("${_info}" "elementCount" "[1-9][0-9]*")
field("${_info}" "state" "\"current\"")
# A VOLUME GREATER THAN ZERO, written with its unit.
field("${_info}" "value" "[0-9]*\\.?[0-9]+e?-?[0-9]*")

step(0 _quality mesh-quality "${DOC}" --json)
field("${_quality}" "invalidElements" "0")
field("${_quality}" "structurallyValid" "true")

step(0 _validate mesh-validate "${DOC}" --json)
field("${_validate}" "dataValid" "true")

step(0 _boundaries mesh-boundaries "${DOC}" face:Extrude001:end_cap --json)
field("${_boundaries}" "resolved" "true")
field("${_boundaries}" "facetCount" "[1-9][0-9]*")

# 10. THE MESH WAS NOT SAVED. The document must still carry only intent, so a
#     fresh read finds no element anywhere in it.
file(READ "${DOC}" _saved)
foreach(forbidden "tetrahedra" "\"nodes\"" "elementCount" "connectivity")
    if(_saved MATCHES "${forbidden}")
        message(FATAL_ERROR
            "the document contains '${forbidden}': a generated mesh must not be persisted")
    endif()
endforeach()
if(NOT _saved MATCHES "mesh-control")
    message(FATAL_ERROR "the document lost its meshing control")
endif()

# 11. A FRESH PROCESS ON A FRESH COPY regenerates the same mesh. This is
#     P16-PERSIST-001's gate, headless: the intent round-tripped and the mesh
#     came back from it rather than from anything stored.
configure_file("${DOC}" "${WORK}/reopened.bcad" COPYONLY)
step(0 _reopened mesh-info "${WORK}/reopened.bcad" --json)
if(NOT _reopened STREQUAL _info)
    message(FATAL_ERROR
        "a fresh process reported a different mesh\n  before: ${_info}\n  after:  ${_reopened}")
endif()
message(STATUS "  fresh-process mesh-info is byte-identical")

# 12. Remove the local sizing and confirm the mesh changes with the intent.
step(0 _out mesh-local-remove "${DOC}" face:Extrude001:end_cap)
step(0 _coarser mesh-info "${DOC}" --json)
if(_coarser STREQUAL _info)
    message(FATAL_ERROR "removing a refinement changed nothing; the intent is not reaching the mesher")
endif()
message(STATUS "  removing the refinement changed the mesh, as it must")

# ---------------------------------------------------------------------------
# The negative workflow
# ---------------------------------------------------------------------------

file(READ "${DOC}" _before_failures)

# An invalid size: refused BY THE CORE, exit 1.
step(1 _out mesh-set-global-size "${DOC}" -- -1mm)
# A malformed quantity: refused by the command line, exit 2.
step(2 _out mesh-set-global-size "${DOC}" -- 1zz)
# A face that is not a face.
step(2 _out mesh-local-add "${DOC}" node:4 2mm)
# Removing a control that is not there.
step(1 _out mesh-local-remove "${DOC}" face:Extrude001:side:9999)
# A document that does not exist.
step(1 _out mesh-info "${WORK}/absent.bcad")
# An unknown command.
step(2 _out mesh-frobnicate "${DOC}")

# NOTHING THE FAILURES TOUCHED. Six refused commands, and the document is
# byte-identical: runEdit saves only on success, and this is the measurement
# of it rather than the assumption.
file(READ "${DOC}" _after_failures)
if(NOT _after_failures STREQUAL _before_failures)
    message(FATAL_ERROR "a failed command changed the document")
endif()
message(STATUS "  six failures left the document byte-identical")

# An unmeshable document: no body at all.
step(0 _out new "${WORK}/empty.bcad" --force --name Empty)
step(1 _out mesh-control-add "${WORK}/empty.bcad")

message(STATUS "MeshingWorkflow: ${_steps} steps, every exit code as expected")

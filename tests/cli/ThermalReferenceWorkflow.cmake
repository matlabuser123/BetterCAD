# The thermal reference models through the real executables (P18-REFMOD-001: "CLI validation").
#
# TWO PROCESSES AND NOTHING BETWEEN THEM BUT BYTES. `bettercad_example_reference_models --thermal --out <dir>` builds, saves,
# regenerates, meshes and solves the reference documents and writes them; `bettercad-cli` -- a different executable, a different
# process, never having seen the model built -- then LOADS what was written, regenerates from the intent in the file, meshes, solves and
# prints. That is the only way to prove that the saved file alone reproduces the answer, and that the command line reports it correctly.
#
# WHAT IT CHECKS HERE AND WHAT IT HANDS ON. CMake cannot do floating-point arithmetic, so this script asserts everything that is
# DISCRETE -- exit codes, streams, stages, codes, booleans, counts, byte identity -- and records every raw payload under
# ${WORK}/payloads. The NUMBERS are compared with the closed forms by the hidden test `ThermalRefmodCli_*` (tests/reference/
# ThermalCliReferenceTests.cpp), which this script runs at the end over those same bytes with the strict RFC 8259 reader. Neither half
# asks BetterCAD what the answer should be.
#
# FRESH-BINARY PROOF. Both binaries are given as ABSOLUTE build-tree paths ($<TARGET_FILE:...>), asserted to lie inside the build
# directory, and recorded with their size and SHA-256; the CLI's runtime closure is resolved against its own directory (a shared preset
# must find every project library beside it, a static one none; the headless CLI may load no GUI toolkit). What makes the binary the one
# built from THIS source is the qualification harness's clean build and the frozen fingerprints, as in ThermalWorkflow.cmake.
#
# Inputs: CLI RUNNER BUILD_DIR EXPECT_SHARED TESTS WORK
cmake_minimum_required(VERSION 3.28)
cmake_policy(SET CMP0207 NEW)

foreach(required CLI RUNNER BUILD_DIR TESTS WORK)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "ThermalReferenceWorkflow.cmake: ${required} is required")
    endif()
endforeach()
foreach(binary CLI RUNNER TESTS)
    if(NOT EXISTS "${${binary}}")
        message(FATAL_ERROR "${binary} does not exist: ${${binary}}")
    endif()
    if(NOT IS_ABSOLUTE "${${binary}}")
        message(FATAL_ERROR "${binary} must be an absolute path so a PATH lookup cannot choose the binary: ${${binary}}")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}" "${WORK}/docs" "${WORK}/payloads")
set(LEDGER "${WORK}/ledger.txt")
file(WRITE "${LEDGER}" "")

file(REAL_PATH "${BUILD_DIR}" _build_real)
set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL objdump)

# Records and asserts the provenance of one binary.
function(prove label binary headless)
    file(REAL_PATH "${binary}" _real)
    string(FIND "${_real}" "${_build_real}/" _inside)
    if(NOT _inside EQUAL 0)
        message(FATAL_ERROR "${label}: the binary under test is not inside the build tree.\n  binary: ${_real}\n  build:  ${_build_real}")
    endif()
    file(SIZE "${_real}" _size)
    file(SHA256 "${_real}" _hash)
    get_filename_component(_dir "${_real}" DIRECTORY)
    file(APPEND "${LEDGER}" "${label} binary ${_real}\n${label} size ${_size}\n${label} sha256 ${_hash}\n")
    message(STATUS "${label}: ${_real} (${_size} bytes, sha256 ${_hash})")
    file(GET_RUNTIME_DEPENDENCIES
        EXECUTABLES "${_real}"
        RESOLVED_DEPENDENCIES_VAR _deps
        UNRESOLVED_DEPENDENCIES_VAR _unresolved
        CONFLICTING_DEPENDENCIES_PREFIX _conflict
        DIRECTORIES "${_dir}"
        PRE_EXCLUDE_REGEXES "^api-ms-" "^ext-ms-"
        POST_EXCLUDE_REGEXES "/[Ww][Ii][Nn][Dd][Oo][Ww][Ss]/[Ss][Yy][Ss][Tt][Ee][Mm]32/")
    set(_project 0)
    foreach(_dep IN LISTS _deps)
        file(APPEND "${LEDGER}" "${label} runtime ${_dep}\n")
        if(headless AND _dep MATCHES "[Qq]t[0-9]")
            message(FATAL_ERROR "${label}: the headless binary depends on a GUI library: ${_dep}")
        endif()
        if(_dep MATCHES "libbettercad_")
            math(EXPR _project "${_project} + 1")
            get_filename_component(_dep_dir "${_dep}" DIRECTORY)
            file(REAL_PATH "${_dep_dir}" _dep_real)
            if(NOT _dep_real STREQUAL "${_dir}")
                message(FATAL_ERROR "${label}: a project library resolves outside the binary's directory: ${_dep}")
            endif()
        endif()
    endforeach()
    if(_unresolved)
        message(FATAL_ERROR "${label}: unresolved runtime dependencies: ${_unresolved}")
    endif()
    if(EXPECT_SHARED AND _project EQUAL 0)
        message(FATAL_ERROR "${label}: a shared-library preset resolved no project library; the proof is vacuous")
    endif()
    if(NOT EXPECT_SHARED AND NOT _project EQUAL 0)
        message(FATAL_ERROR "${label}: a static preset resolved ${_project} project libraries")
    endif()
    file(APPEND "${LEDGER}" "${label} project-libraries-beside-binary ${_project}\n")
endfunction()
prove(cli "${CLI}" TRUE)
prove(runner "${RUNNER}" FALSE)
prove(tests "${TESTS}" FALSE)
file(APPEND "${LEDGER}" "\nname | exit | stdout-bytes | stderr-bytes | stdout-sha256 | command\n")

set_property(GLOBAL PROPERTY REFMOD_STEPS 0)

# One process, raw bytes to files named for the step, the exit code asserted and the ledger line appended. Sets OUT and ERR in the
# caller. THE EXIT CODE IS CAPTURED SEPARATELY FROM THE OUTPUT: there is no pipeline whose status could be a parser's.
function(run name expected binary)
    set(_args ${ARGN})
    list(FIND _args "--json" _json_at)
    if(_json_at EQUAL -1)
        set(_extension out)
    else()
        set(_extension json)
    endif()
    set(_out_file "${WORK}/payloads/${name}.${_extension}")
    set(_err_file "${WORK}/payloads/${name}.err")
    execute_process(
        COMMAND "${binary}" ${_args}
        WORKING_DIRECTORY "${WORK}"
        RESULT_VARIABLE _code
        OUTPUT_FILE "${_out_file}"
        ERROR_FILE "${_err_file}")
    get_property(_steps GLOBAL PROPERTY REFMOD_STEPS)
    math(EXPR _n "${_steps} + 1")
    set_property(GLOBAL PROPERTY REFMOD_STEPS ${_n})
    file(SIZE "${_out_file}" _out_bytes)
    file(SIZE "${_err_file}" _err_bytes)
    file(SHA256 "${_out_file}" _out_hash)
    file(READ "${_out_file}" _out)
    file(READ "${_err_file}" _err)
    string(JOIN " " _joined ${_args})
    file(APPEND "${LEDGER}" "${name} | ${_code} | ${_out_bytes} | ${_err_bytes} | ${_out_hash} | ${_joined}\n")
    if(NOT _code STREQUAL "${expected}")
        message(FATAL_ERROR "${name} FAILED: expected exit ${expected}, got ${_code}\n  command: ${_joined}\n  stdout: ${_out}\n  stderr: ${_err}")
    endif()
    message(STATUS "${name}: exit ${_code}  ${_joined}")
    set(OUT "${_out}" PARENT_SCOPE)
    set(ERR "${_err}" PARENT_SCOPE)
endfunction()

function(member json out_var)
    string(JSON _value GET "${json}" ${ARGN})
    set(${out_var} "${_value}" PARENT_SCOPE)
endfunction()

function(expect what actual wanted)
    if(NOT "${actual}" STREQUAL "${wanted}")
        message(FATAL_ERROR "${what}: expected '${wanted}', got '${actual}'")
    endif()
endfunction()

# ONE JSON object and nothing else, as far as string(JSON) can tell; the strict half is the C++ re-parse at the end.
function(clean_payload json)
    string(REPLACE "\r" "" _lf "${json}")
    string(JSON _type TYPE "${_lf}")
    if(NOT _type STREQUAL "OBJECT")
        message(FATAL_ERROR "stdout is not a JSON object but a ${_type}:\n${json}")
    endif()
    string(LENGTH "${_lf}" _length)
    math(EXPR _last_at "${_length} - 2")
    string(SUBSTRING "${_lf}" 0 1 _first)
    string(SUBSTRING "${_lf}" ${_last_at} 2 _last)
    if(NOT _first STREQUAL "{" OR NOT _last STREQUAL "}\n")
        message(FATAL_ERROR "stdout is not exactly one object and a line ending:\n${json}")
    endif()
    string(ASCII 27 _esc)
    string(FIND "${json}" "${_esc}" _at)
    if(NOT _at EQUAL -1)
        message(FATAL_ERROR "stdout holds an ANSI escape")
    endif()
endfunction()

function(silent_stdout out)
    if(NOT "${out}" STREQUAL "")
        message(FATAL_ERROR "a command that could not answer printed to stdout:\n${out}")
    endif()
endfunction()

function(code_on_stderr err code)
    string(FIND "${err}" ": ${code}: " _at)
    if(_at EQUAL -1)
        message(FATAL_ERROR "the diagnostic code '${code}' is missing from stderr:\n${err}")
    endif()
endfunction()

function(unchanged what file hash)
    file(SHA256 "${file}" _now)
    if(NOT _now STREQUAL "${hash}")
        message(FATAL_ERROR "${what}: the document changed on disk.\n  before: ${hash}\n  after:  ${_now}")
    endif()
endfunction()

# ===========================================================================
# PROCESS A: the example program builds, saves, regenerates, meshes, solves and judges every reference document
# ===========================================================================

run(runner 0 "${RUNNER}" --thermal --out "${WORK}/docs")
foreach(model slab convection lumped surface_flux transient_slab singular singular_balanced stale_mesh unresolved_load unresolved_temperature unresolved_convection)
    if(NOT EXISTS "${WORK}/docs/thermal_${model}.bcad")
        message(FATAL_ERROR "the runner did not write thermal_${model}.bcad")
    endif()
endforeach()
# its own assertions held (it exits non-zero otherwise); and it re-judged every one of the 15 accepted documents stale after a main-parameter edit
string(REGEX MATCHALL "mesh_stale_finding yes" _stale_gates "${OUT}")
list(LENGTH _stale_gates _stale_count)
if(NOT _stale_count EQUAL 15)
    message(FATAL_ERROR "the runner re-judged ${_stale_count} documents stale after an edit; the 15 documents that solve are expected")
endif()
string(REGEX MATCHALL "expected_outcome refusal actual refusal published_result no" _refusals "${OUT}")
list(LENGTH _refusals _refusal_count)
if(NOT _refusal_count EQUAL 5)
    message(FATAL_ERROR "the runner reported ${_refusal_count} refusals; RM-THERM-09 (2) and RM-THERM-10 (3) make 5")
endif()
string(FIND "${OUT}" "== RM-THERM-10 ThermalStaleMesh" _stale_model)
if(_stale_model EQUAL -1)
    message(FATAL_ERROR "the runner did not run the stale-mesh reference model")
endif()

# ===========================================================================
# PROCESS B..: the CLI over the saved files, one process per question, three accepted models
# ===========================================================================

function(accepted_model tag file)
    set(_doc "${WORK}/docs/${file}.bcad")
    file(SHA256 "${_doc}" _hash)
    run(${tag}.validate 0 "${CLI}" thermal-validate "${_doc}" --json)
    clean_payload("${OUT}")
    member("${OUT}" _accepted accepted)
    string(JSON _stopped TYPE "${OUT}" stoppedAt)
    member("${OUT}" _status validation status)
    expect("${tag} accepted" "${_accepted}" "ON")
    expect("${tag} ran to the end (stoppedAt is null)" "${_stopped}" "NULL")
    expect("${tag} validation status" "${_status}" "PASS")
    run(${tag}.solve 0 "${CLI}" thermal-solve "${_doc}" --json)
    clean_payload("${OUT}")
    set(_first "${OUT}")
    member("${OUT}" _balanced heatBalance balanced)
    expect("${tag} balanced" "${_balanced}" "ON")
    # a second, fresh process: the answer is deterministic ACROSS processes, byte for byte
    run(${tag}.solve-again 0 "${CLI}" thermal-solve "${_doc}" --json)
    expect("${tag} two processes, one payload" "${OUT}" "${_first}")
    unchanged("${tag}: after the solves" "${_doc}" "${_hash}")
endfunction()

accepted_model(rm02 thermal_slab)
run(rm02.heatflux 0 "${CLI}" thermal-heat-flux "${WORK}/docs/thermal_slab.bcad" --json)
clean_payload("${OUT}")
run(rm02.balance 0 "${CLI}" thermal-balance "${WORK}/docs/thermal_slab.bcad" --json)
clean_payload("${OUT}")

accepted_model(rm05 thermal_convection)
run(rm05.heatflux 0 "${CLI}" thermal-heat-flux "${WORK}/docs/thermal_convection.bcad" --json)
clean_payload("${OUT}")
run(rm05.balance 0 "${CLI}" thermal-balance "${WORK}/docs/thermal_convection.bcad" --json)
clean_payload("${OUT}")

accepted_model(rm02b thermal_surface_flux)
run(rm02b.heatflux 0 "${CLI}" thermal-heat-flux "${WORK}/docs/thermal_surface_flux.bcad" --json)
clean_payload("${OUT}")
run(rm02b.balance 0 "${CLI}" thermal-balance "${WORK}/docs/thermal_surface_flux.bcad" --json)
clean_payload("${OUT}")

accepted_model(rm08b thermal_transient_slab)
run(rm08b.temperature 0 "${CLI}" thermal-temperature "${WORK}/docs/thermal_transient_slab.bcad" --json)
clean_payload("${OUT}")
run(rm08b.balance 0 "${CLI}" thermal-balance "${WORK}/docs/thermal_transient_slab.bcad" --json)
clean_payload("${OUT}")

accepted_model(rm08 thermal_lumped)
run(rm08.temperature 0 "${CLI}" thermal-temperature "${WORK}/docs/thermal_lumped.bcad" --json)
clean_payload("${OUT}")
run(rm08.balance 0 "${CLI}" thermal-balance "${WORK}/docs/thermal_lumped.bcad" --json)
clean_payload("${OUT}")
member("${OUT}" _mode mode)
expect("rm08 is transient" "${_mode}" "transient")

# ===========================================================================
# THE EXPECTED FAILURES, through the real binary: non-zero exit, NOTHING on stdout, the real cause on stderr,
# the document untouched. The validation report is the one command whose payload IS the answer.
# ===========================================================================

function(refused_model tag file stage code)
    set(_doc "${WORK}/docs/${file}.bcad")
    file(SHA256 "${_doc}" _hash)
    foreach(_command thermal-solve thermal-temperature thermal-gradient thermal-heat-flux thermal-balance)
        run(${tag}.${_command} 1 "${CLI}" ${_command} "${_doc}" --json)
        silent_stdout("${OUT}")
        code_on_stderr("${ERR}" "${code}")
    endforeach()
    run(${tag}.validate 1 "${CLI}" thermal-validate "${_doc}" --json)
    clean_payload("${OUT}")
    member("${OUT}" _accepted accepted)
    member("${OUT}" _stopped stoppedAt)
    expect("${tag} refused" "${_accepted}" "OFF")
    expect("${tag} stops at the right link" "${_stopped}" "${stage}")
    unchanged("${tag}: after the refused commands" "${_doc}" "${_hash}")
endfunction()

refused_model(rm09 thermal_singular solve no_temperature_datum)
refused_model(rm09b thermal_singular_balanced solve no_temperature_datum)
refused_model(rm10load thermal_unresolved_load loads loads_invalid)
refused_model(rm10temperature thermal_unresolved_temperature boundary boundary_conditions_invalid)
refused_model(rm10convection thermal_unresolved_convection boundary boundary_conditions_invalid)
# no nearest-face rebinding, said by the diagnostic itself
foreach(_tag rm10load rm10temperature rm10convection)
    file(READ "${WORK}/payloads/${_tag}.thermal-solve.err" _text)
    string(FIND "${_text}" "Nothing nearby is substituted" _at)
    if(_at EQUAL -1)
        message(FATAL_ERROR "${_tag}: the refusal does not say that no face was substituted:\n${_text}")
    endif()
endforeach()

# ===========================================================================
# THE STALE-MESH MODEL: a one-shot process cannot be handed a stale mesh, but the file CAN be edited between two processes, and the
# second must not remember the first. 30 mm -> 36 mm in the saved intent: a fresh process regenerates, remeshes and gives the NEW
# flux (k dT / L), never the old one.
# ===========================================================================

set(STALE "${WORK}/docs/thermal_stale_mesh.bcad")
run(rm10stale.before.heatflux 0 "${CLI}" thermal-heat-flux "${STALE}" --json)
clean_payload("${OUT}")
set(EDITED "${WORK}/docs/thermal_stale_mesh_edited.bcad")
file(READ "${STALE}" _stale_text)
string(REGEX REPLACE "(\"name\": \"stale_l\",[ \r\n]*\"si_value\": )0\\.03," "\\10.036," _edited_text "${_stale_text}")
if(_edited_text STREQUAL _stale_text)
    message(FATAL_ERROR "the stale-mesh document's parameter could not be edited")
endif()
file(WRITE "${EDITED}" "${_edited_text}")
run(rm10stale.after.heatflux 0 "${CLI}" thermal-heat-flux "${EDITED}" --json)
clean_payload("${OUT}")
file(READ "${WORK}/payloads/rm10stale.before.heatflux.json" _before)
if(OUT STREQUAL _before)
    message(FATAL_ERROR "a fresh process gave the OLD answer for an edited model")
endif()

# ===========================================================================
# THE NUMBERS: strict re-parse of the raw bytes, compared with the closed forms (see the header).
# ===========================================================================

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "BETTERCAD_REFMOD_CLI_DIR=${WORK}" "${TESTS}" "[refmod-cli]"
    RESULT_VARIABLE _numbers_code
    OUTPUT_VARIABLE _numbers_out
    ERROR_VARIABLE _numbers_err)
file(APPEND "${LEDGER}" "\nnumeric comparison with the closed forms: exit ${_numbers_code}\n${_numbers_out}\n")
if(NOT _numbers_code EQUAL 0)
    message(FATAL_ERROR "the comparison of the CLI numbers with the closed forms FAILED (exit ${_numbers_code}):\n${_numbers_out}\n${_numbers_err}")
endif()
# ZERO-MATCH PROTECTION: the comparison must have run its five cases.
if(NOT _numbers_out MATCHES "All tests passed \\([0-9]+ assertions in 6 test cases\\)")
    message(FATAL_ERROR "the numeric comparison did not run its 6 test cases:\n${_numbers_out}")
endif()
message(STATUS "${_numbers_out}")

# every non-empty .json payload is exactly one strict JSON object (the same reader, over every byte written)
set(_strict_name "ThermalRefmodCli_EveryPayloadIsStrictJson")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "BETTERCAD_REFMOD_CLI_DIR=${WORK}" "${TESTS}" "${_strict_name}"
    RESULT_VARIABLE _strict_code
    OUTPUT_VARIABLE _strict_out
    ERROR_VARIABLE _strict_err)
file(APPEND "${LEDGER}" "\nstrict re-parse of every payload: exit ${_strict_code}\n${_strict_out}\n")
if(NOT _strict_code EQUAL 0)
    message(FATAL_ERROR "the strict re-parse of the payloads FAILED (exit ${_strict_code}):\n${_strict_out}\n${_strict_err}")
endif()

get_property(_steps GLOBAL PROPERTY REFMOD_STEPS)
# EXACT step count: a step removed and a step added both change the evidence, and either must be a deliberate edit to this number.
message(STATUS "processes run: ${_steps}")
if(NOT _steps EQUAL 58)
    message(FATAL_ERROR "${_steps} processes ran; the workflow has 58. A script that exits 0 having skipped steps is the defect this check exists for.")
endif()
file(APPEND "${LEDGER}" "\nprocesses ${_steps}\n")
message(STATUS "ThermalReferenceWorkflow: ${_steps} processes, every exit code asserted, every payload parsed twice")

# The scripted headless thermal workflow (P18-CLI-001).
#
# WHAT THIS IS FOR, and why it is not a unit test. The in-process tests call cli::run() directly, which proves the command logic. They
# cannot prove that the EXECUTABLE works: argument parsing from a real command line, document loading from a real path, the runtime
# closure actually loading, the exit code reaching the shell, and stdout being clean enough to parse. Every one of those has broken
# something in this project before.
#
# AND IT IS GENUINELY MULTI-PROCESS. Each step is a separate OS process (execute_process) that reloads the document from disk, which
# is the only way to prove what the brief asks for: that a thermal result is NOT persisted, so a fresh process must run the analysis
# again, and that nothing a previous process held in memory is needed -- no pointer, no cached mesh, no solver state, no singleton.
#
# IT PARSES, IT DOES NOT GREP -- IN TWO LAYERS, because neither alone is strict. CMake's `string(JSON)` is a real parser but a LENIENT one: it
# accepts text after the object, a repeated key, a trailing comma and a leading zero (measured by the independent review), and rejects only
# `nan` and `inf`. So (1) every payload goes through `string(JSON)` here, which catches a banner before the object or an unparseable
# value, and (2) at the end the RAW BYTES each process wrote are re-read by the strict reader the in-process tests use
# (tests/cli/MiniJson.hpp, run as the hidden test ThermalCliWorkflow_EveryPayloadIsStrictJson), which rejects everything RFC 8259 does.
# Both are independent of the CLI's writer.
#
# IT FAILS FAST. Each step asserts its exit code before the next runs, and any mismatch is a fatal error: a script that carried on
# after a failure and then reported success would be worse than no script.
#
# IT LEAVES A LEDGER. Every process appends one line to ${WORK}/ledger.txt -- number, exit, the RAW BYTE counts of stdout and stderr, the
# SHA-256 of stdout, and the command -- and its stdout and stderr are kept as files under ${WORK}/payloads, so the evidence names what ran
# rather than describing it. (The documents' hashes are asserted in the script, at the points that matter, not recorded per line.)
#
# Inputs:
#   CLI           the executable to run -- an ABSOLUTE path, never a bare name, so a stale binary on PATH cannot be tested by accident
#   BUILD_DIR     the build tree this binary must live in (the fresh-binary proof)
#   EXPECT_SHARED ON when the preset links the project libraries as DLLs
#   BARE          a .bcad with a mesh control and a material and NO thermal analysis
#   TRANSIENT     a .bcad whose thermal analysis is transient (the hand-written reference)
#   TESTS         the test executable, which runs the strict re-parse of the payloads at the end
#   WORK          a scratch directory
cmake_minimum_required(VERSION 3.28)
cmake_policy(SET CMP0207 NEW)

foreach(required CLI BUILD_DIR BARE TRANSIENT TESTS WORK)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "ThermalWorkflow.cmake: ${required} is required")
    endif()
endforeach()
if(NOT EXISTS "${CLI}")
    message(FATAL_ERROR "the CLI does not exist: ${CLI}")
endif()
if(NOT IS_ABSOLUTE "${CLI}")
    message(FATAL_ERROR "the CLI path must be absolute so a PATH lookup cannot choose the binary: ${CLI}")
endif()

# ---------------------------------------------------------------------------
# THE FRESH-BINARY PROOF
# ---------------------------------------------------------------------------
#
# Recorded so the evidence names the executable that actually ran rather than the one that was meant to. WHAT IT PROVES, STATED HONESTLY:
# the path is the build tree's (given by CMake as $<TARGET_FILE:...>, which makes that true by construction), is absolute (so PATH cannot
# choose), lies inside BUILD_DIR (so it is not an installed or copied binary), and has the size and SHA-256 recorded; its runtime
# libraries resolve beside it. What makes it the binary built from THIS source is not in this script: it is the harness's clean, rebuild
# and no-op rebuild of the preset, and the source fingerprints frozen before and read back after (FREEZE.md). Its size and SHA-256 are recorded, and its RUNTIME DEPENDENCIES are resolved against its own directory.
file(REAL_PATH "${CLI}" _cli_real)
file(REAL_PATH "${BUILD_DIR}" _build_real)
string(FIND "${_cli_real}" "${_build_real}/" _inside)
if(NOT _inside EQUAL 0)
    message(FATAL_ERROR "the binary under test is not inside the build tree.\n  binary: ${_cli_real}\n  build:  ${_build_real}")
endif()
file(SIZE "${_cli_real}" _cli_size)
file(SHA256 "${_cli_real}" _cli_hash)
get_filename_component(_cli_dir "${_cli_real}" DIRECTORY)
message(STATUS "binary:   ${_cli_real}")
message(STATUS "size:     ${_cli_size} bytes")
message(STATUS "sha256:   ${_cli_hash}")

set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL objdump)
file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${_cli_real}"
    RESOLVED_DEPENDENCIES_VAR _deps
    UNRESOLVED_DEPENDENCIES_VAR _unresolved
    CONFLICTING_DEPENDENCIES_PREFIX _conflict
    DIRECTORIES "${_cli_dir}"
    PRE_EXCLUDE_REGEXES "^api-ms-" "^ext-ms-"
    POST_EXCLUDE_REGEXES "/[Ww][Ii][Nn][Dd][Oo][Ww][Ss]/[Ss][Yy][Ss][Tt][Ee][Mm]32/")
set(_project_dlls 0)
foreach(_dep IN LISTS _deps)
    message(STATUS "runtime:  ${_dep}")
    # THE CLI IS HEADLESS: no GUI toolkit is in its runtime closure.
    if(_dep MATCHES "[Qq]t[0-9]")
        message(FATAL_ERROR "the headless CLI depends on a GUI library: ${_dep}")
    endif()
    # EVERY PROJECT LIBRARY RESOLVES TO THE BINARY'S OWN DIRECTORY, never to an installed copy elsewhere on the machine.
    if(_dep MATCHES "libbettercad_")
        math(EXPR _project_dlls "${_project_dlls} + 1")
        get_filename_component(_dep_dir "${_dep}" DIRECTORY)
        file(REAL_PATH "${_dep_dir}" _dep_real)
        if(NOT _dep_real STREQUAL "${_cli_dir}")
            message(FATAL_ERROR "a project library resolves outside the binary's directory: ${_dep}")
        endif()
    endif()
endforeach()
foreach(_missing IN LISTS _unresolved)
    message(STATUS "UNRESOLVED runtime dependency: ${_missing}")
endforeach()
if(_unresolved)
    message(FATAL_ERROR "the binary has unresolved runtime dependencies: ${_unresolved}")
endif()
if(EXPECT_SHARED AND _project_dlls EQUAL 0)
    message(FATAL_ERROR "a shared-library preset resolved no project library; the proof is vacuous")
endif()
if(NOT EXPECT_SHARED AND NOT _project_dlls EQUAL 0)
    message(FATAL_ERROR "a static preset resolved ${_project_dlls} project libraries")
endif()
message(STATUS "project libraries resolved beside the binary: ${_project_dlls}")

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}" "${WORK}/elsewhere" "${WORK}/with space" "${WORK}/payloads")
set(LEDGER "${WORK}/ledger.txt")
file(WRITE "${LEDGER}" "binary ${_cli_real}\nsize ${_cli_size}\nsha256 ${_cli_hash}\nproject-libraries-beside-binary ${_project_dlls}\n")
foreach(_dep IN LISTS _deps)
    file(APPEND "${LEDGER}" "runtime ${_dep}\n")
endforeach()
file(APPEND "${LEDGER}" "\nprocess | exit | stdout-bytes | stderr-bytes | stdout-sha256 | command      (byte counts are of the raw files)\n")

set(_steps 0)
set(_cwd "${WORK}")

# Runs the CLI as its own process and asserts the exit code.
#
# THE EXIT CODE IS CAPTURED SEPARATELY FROM THE OUTPUT: execute_process gives RESULT_VARIABLE and OUTPUT_VARIABLE independently, so
# there is no pipeline whose exit status could be a parser's instead of BetterCAD's. Both are asserted.
function(step expected_code out_var)
    set(_args ${ARGN})
    math(EXPR _n "${_steps} + 1")
    # THE RAW BYTES GO TO FILES (execute_process OUTPUT_FILE is binary), so a size is the bytes the process wrote -- CRLF included, as the
    # real console would see them -- and the strict re-parse at the end reads exactly what was written.
    list(FIND _args "--json" _json_at)
    if(_json_at EQUAL -1)
        set(_extension out)
    else()
        set(_extension json)
    endif()
    set(_out_file "${WORK}/payloads/${_n}.${_extension}")
    set(_err_file "${WORK}/payloads/${_n}.err")
    execute_process(
        COMMAND "${_cli_real}" ${_args}
        WORKING_DIRECTORY "${_cwd}"
        RESULT_VARIABLE _code
        OUTPUT_FILE "${_out_file}"
        ERROR_FILE "${_err_file}")
    set(_steps ${_n} PARENT_SCOPE)
    file(SIZE "${_out_file}" _out_bytes)
    file(SIZE "${_err_file}" _err_bytes)
    file(SHA256 "${_out_file}" _out_hash)
    file(READ "${_out_file}" _out)
    file(READ "${_err_file}" _err)
    string(JOIN " " _joined ${_args})
    file(APPEND "${LEDGER}" "${_n} | ${_code} | ${_out_bytes} | ${_err_bytes} | ${_out_hash} | ${_joined}\n")
    if(NOT _code STREQUAL "${expected_code}")
        message(FATAL_ERROR
            "step ${_n} FAILED: expected exit ${expected_code}, got ${_code}\n"
            "  command: ${_args}\n  stdout: ${_out}\n  stderr: ${_err}")
    endif()
    message(STATUS "step ${_n}: exit ${_code}  ${_joined}")
    set(${out_var} "${_out}" PARENT_SCOPE)
    set(_last_err "${_err}" PARENT_SCOPE)
endfunction()
# PARSES the payload and returns one member. A malformed payload is fatal here, which is the whole gate: `string(JSON)` without an
# ERROR_VARIABLE stops the script, so any contamination of stdout fails rather than being filtered away.
function(member json out_var)
    string(JSON _value GET "${json}" ${ARGN})
    set(${out_var} "${_value}" PARENT_SCOPE)
endfunction()

# Asserts that stdout is ONE JSON object and nothing else, as far as string(JSON) can tell: it parses, it is an object, it starts with
# `{`, ends with `}` and a line ending, and holds no ANSI escape. (A line ending is LF or CRLF: the real process writes CRLF on Windows.)
# The strict half -- no second object, no repeated key -- is the re-parse at the end of the script.
function(clean_payload json)
    string(REPLACE "\r" "" _lf "${json}")
    string(JSON _type TYPE "${_lf}")
    if(NOT _type STREQUAL "OBJECT")
        message(FATAL_ERROR "stdout is not a JSON object but a ${_type}:\n${json}")
    endif()
    string(SUBSTRING "${_lf}" 0 1 _first)
    string(LENGTH "${_lf}" _length)
    math(EXPR _last_at "${_length} - 2")
    string(SUBSTRING "${_lf}" ${_last_at} 2 _last)
    if(NOT _first STREQUAL "{")
        message(FATAL_ERROR "stdout does not start with {:\n${json}")
    endif()
    if(NOT _last STREQUAL "}\n")
        message(FATAL_ERROR "stdout does not end with a closing brace and a newline:\n${json}")
    endif()
    string(ASCII 27 _esc)
    string(FIND "${json}" "${_esc}" _at)
    if(NOT _at EQUAL -1)
        message(FATAL_ERROR "stdout holds an ANSI escape")
    endif()
endfunction()

# CMake's string(JSON) re-prints every number through a double with 17 significant digits, so the text of `0.3` comes back as
# `0.29999999999999999`. Two NUMBERS are therefore compared as numbers (CMake's EQUAL reads both as doubles), and everything else
# as text. Whole PAYLOADS are still compared as the raw text the process wrote, which is the exact comparison.
set(_number_pattern "^-?[0-9]+(\\.[0-9]+)?([eE][-+]?[0-9]+)?$")

function(same what a b)
    if("${a}" MATCHES "${_number_pattern}" AND "${b}" MATCHES "${_number_pattern}")
        if(NOT a EQUAL b)
            message(FATAL_ERROR "${what} differs: '${a}' vs '${b}'")
        endif()
    elseif(NOT "${a}" STREQUAL "${b}")
        message(FATAL_ERROR "${what} differs: '${a}' vs '${b}'")
    endif()
endfunction()

function(differs what a b)
    if("${a}" MATCHES "${_number_pattern}" AND "${b}" MATCHES "${_number_pattern}")
        if(a EQUAL b)
            message(FATAL_ERROR "${what} did not change: '${a}'")
        endif()
    elseif("${a}" STREQUAL "${b}")
        message(FATAL_ERROR "${what} did not change: '${a}'")
    endif()
endfunction()

# A refused command prints NOTHING to stdout (the stream discipline) and its stable code on stderr.
function(expect_silent_stdout out)
    if(NOT "${out}" STREQUAL "")
        message(FATAL_ERROR "a command that could not answer printed to stdout:\n${out}")
    endif()
endfunction()

function(expect_code_on_stderr err code)
    string(FIND "${err}" ": ${code}: " _at)
    if(_at EQUAL -1)
        message(FATAL_ERROR "the diagnostic code '${code}' is missing from stderr:\n${err}")
    endif()
endfunction()

function(hash_of file out_var)
    file(SHA256 "${file}" _h)
    set(${out_var} "${_h}" PARENT_SCOPE)
endfunction()

function(same_file_bytes what file expected_hash)
    file(SHA256 "${file}" _now)
    if(NOT _now STREQUAL "${expected_hash}")
        message(FATAL_ERROR "${what}: the document changed on disk.\n  before: ${expected_hash}\n  after:  ${_now}")
    endif()
endfunction()

# ===========================================================================
# STEADY: build an analysis through the CLI, one process per step
# ===========================================================================

set(DOC "${WORK}/steady.bcad")
configure_file("${BARE}" "${DOC}" COPYONLY)

# 0. The build the binary reports being: its version, compiler, configuration and embedded source revision go into the ledger, beside the
#    path and the hash, so the evidence says which build it ran.
step(0 _version version)

# 1. The starting document has no thermal analysis, and says so by code.
step(1 _out thermal-settings "${DOC}" --json)
expect_silent_stdout("${_out}")
expect_code_on_stderr("${_last_err}" "no_thermal_analysis")

# 2-5. Four processes, each loading and saving the document.
step(0 _out thermal-analysis-add "${DOC}")
step(0 _out thermal-load-add "${DOC}" 1 --generation 1e5)
step(0 _out thermal-boundary-add "${DOC}" 1 face:Extrude001:start_cap --temperature 300)
step(0 _out thermal-boundary-add "${DOC}" 2 face:Extrude001:end_cap --convection 25 --ambient 293.15)

# 6. A FRESH process sees every edit, from the file alone, with the stable ids and the canonical references.
step(0 _settings thermal-settings "${DOC}" --json)
clean_payload("${_settings}")
member("${_settings}" _mode mode)
same("mode" "${_mode}" "steady")
string(JSON _load_count LENGTH "${_settings}" loads)
string(JSON _bc_count LENGTH "${_settings}" boundaryConditions)
same("load count" "${_load_count}" 1)
same("boundary condition count" "${_bc_count}" 2)
member("${_settings}" _load_id loads 0 loadId)
member("${_settings}" _bc1_id boundaryConditions 0 boundaryConditionId)
member("${_settings}" _bc2_id boundaryConditions 1 boundaryConditionId)
same("load id" "${_load_id}" 1)
same("boundary condition 1" "${_bc1_id}" 1)
same("boundary condition 2" "${_bc2_id}" 2)
member("${_settings}" _bc1_face boundaryConditions 0 face)
member("${_settings}" _bc2_face boundaryConditions 1 face)
same("start cap reference" "${_bc1_face}" "face:Extrude001:start_cap")
same("end cap reference" "${_bc2_face}" "face:Extrude001:end_cap")
member("${_settings}" _bc1_unit boundaryConditions 0 temperature unit)
same("temperature unit" "${_bc1_unit}" "K")
member("${_settings}" _stored resultStored)
if(_stored)
    message(FATAL_ERROR "the settings claim a result is stored")
endif()

# ---------------------------------------------------------------------------
# A solve changes nothing on disk
# ---------------------------------------------------------------------------

hash_of("${DOC}" _hash_before)
file(SIZE "${DOC}" _size_before)
message(STATUS "before the solve: ${_size_before} bytes, sha256 ${_hash_before}")

# 7. Validation, which runs the analysis (it must, to judge a result).
step(0 _validation thermal-validate "${DOC}" --json)
clean_payload("${_validation}")
member("${_validation}" _accepted accepted)
if(NOT _accepted)
    message(FATAL_ERROR "the workflow document is not accepted: ${_validation}")
endif()
string(JSON _checks LENGTH "${_validation}" validation checks)
same("rule count" "${_checks}" 15)
member("${_validation}" _status validation status)
same("validation status" "${_status}" "PASS")
same_file_bytes("after thermal-validate" "${DOC}" "${_hash_before}")

# 8. Solve.
step(0 _solve thermal-solve "${DOC}" --json)
clean_payload("${_solve}")
member("${_solve}" _nodes nodeCount)
member("${_solve}" _tets tetrahedronCount)
member("${_solve}" _free freeEquations)
member("${_solve}" _tmin temperature min value)
member("${_solve}" _tmax temperature max value)
member("${_solve}" _tunit temperature min unit)
member("${_solve}" _tmin_node temperature min node)
member("${_solve}" _balanced heatBalance balanced)
member("${_solve}" _accepted validation accepted)
same("temperature unit" "${_tunit}" "K")
if(NOT _balanced OR NOT _accepted)
    message(FATAL_ERROR "the solve did not balance or was not accepted: ${_solve}")
endif()
same_file_bytes("after thermal-solve" "${DOC}" "${_hash_before}")
message(STATUS "the document is byte-identical after the solve (${_size_before} bytes)")

# 9. The five summaries, each its own process, each running the analysis for itself.
step(0 _temperature thermal-temperature "${DOC}" --json)
clean_payload("${_temperature}")
member("${_temperature}" _t_min temperature min value)
member("${_temperature}" _t_max temperature max value)
same("min T agrees with the solve" "${_t_min}" "${_tmin}")
same("max T agrees with the solve" "${_t_max}" "${_tmax}")

step(0 _gradient thermal-gradient "${DOC}" --json)
clean_payload("${_gradient}")
member("${_gradient}" _g_unit unit)
member("${_gradient}" _g_count count)
member("${_gradient}" _g_max magnitude max)
member("${_gradient}" _g_elem largest element)
same("gradient unit" "${_g_unit}" "K/m")
same("element count" "${_g_count}" "${_tets}")
member("${_gradient}" _g_largest largest magnitude value)
same("the largest gradient is the top of the range" "${_g_largest}" "${_g_max}")
if(NOT _g_elem GREATER 0)
    message(FATAL_ERROR "the element carrying the largest gradient is ${_g_elem}")
endif()
if(NOT _g_max GREATER 0)
    message(FATAL_ERROR "a heated plate with a held cap has a zero gradient")
endif()

step(0 _flux thermal-heat-flux "${DOC}" --json)
clean_payload("${_flux}")
member("${_flux}" _q_unit unit)
member("${_flux}" _q_quantity quantity)
member("${_flux}" _q_count count)
same("heat flux unit" "${_q_unit}" "W/m^2")
same("heat flux quantity" "${_q_quantity}" "conductive_heat_flux")
same("element count" "${_q_count}" "${_tets}")
member("${_flux}" _q_max magnitude max)
member("${_flux}" _q_largest largest magnitude value)
member("${_flux}" _q_elem largest element)
same("the largest flux is the top of the range" "${_q_largest}" "${_q_max}")
if(NOT _q_elem GREATER 0)
    message(FATAL_ERROR "the element carrying the largest heat flux is ${_q_elem}")
endif()

step(0 _balance thermal-balance "${DOC}" --json)
clean_payload("${_balance}")
member("${_balance}" _b_balanced balanced)
member("${_balance}" _b_states stateCount)
member("${_balance}" _b_gen final generationInto value)
if(NOT _b_balanced)
    message(FATAL_ERROR "the heat balance is out of tolerance: ${_balance}")
endif()
same("a steady balance has one state" "${_b_states}" 1)
# AN INDEPENDENT ORACLE, which is physics and not plumbing. The heat generated is q''' x V: 1e5 W/m^3 over the volume of the MESH, which a
# different command (P16's `mesh-info`, which meshes and sums the tetrahedra) reports on its own. (It is the mesh''s volume, not the CAD
# body''s: the plate has a circular hole, the mesh approximates it with chords, and the two differ by 0.03 percent -- the first version of
# this check used the CAD volume and the check itself found that.) Nothing in the thermal CLI or the thermal core was asked.
step(0 _meshinfo mesh-info "${DOC}" --json)
clean_payload("${_meshinfo}")
member("${_meshinfo}" _mesh_volume meshVolume value)
member("${_meshinfo}" _mesh_volume_unit meshVolume unit)
same("mesh volume unit" "${_mesh_volume_unit}" "m^3")
# x 1e5 is a shift of the exponent: 9.37...e-05 m^3 x 1e5 W/m^3 = 9.37... W. The comparison is on the first twelve characters (eleven
# significant digits), because the generation is summed element by element and need not equal q''' x V to the last bit.
if(NOT _mesh_volume MATCHES "^[0-9]\\.[0-9]+e-05$")
    message(FATAL_ERROR "the mesh volume is not in the expected form: ${_mesh_volume}")
endif()
string(REPLACE "e-05" "" _expected_generated "${_mesh_volume}")
string(SUBSTRING "${_expected_generated}" 0 12 _expected_digits)
string(SUBSTRING "${_b_gen}" 0 12 _balance_digits)
same("the generated heat is generation x the mesh volume, to eleven digits" "${_balance_digits}" "${_expected_digits}")
same_file_bytes("after the summaries" "${DOC}" "${_hash_before}")
# ---------------------------------------------------------------------------
# A fresh process must solve again, and must agree
# ---------------------------------------------------------------------------

# 10. The same file, a new process, run again from scratch. Nothing was carried over: the result is not in the file and the previous
#     processes are gone. The WHOLE PAYLOAD is byte-identical, which is the strong form of the claim: the answer is deterministic
#     across processes, not merely close.
step(0 _resolve thermal-solve "${DOC}" --json)
clean_payload("${_resolve}")
if(NOT _solve STREQUAL "${_resolve}")
    message(FATAL_ERROR "two processes ran the same file and printed different payloads")
endif()
message(STATUS "two fresh processes produced byte-identical solve payloads")

# ---------------------------------------------------------------------------
# An edit invalidates, a save reloads, a removal restores
# ---------------------------------------------------------------------------

# 11. Add a surface load through a process, then run in another. The answer must change: nothing stale survives.
step(0 _out thermal-load-add "${DOC}" 2 --flux -1500 --face face:Extrude001:end_cap)
step(0 _edited thermal-solve "${DOC}" --json)
clean_payload("${_edited}")
member("${_edited}" _tmax_edited temperature max value)
differs("the peak temperature after adding a load" "${_tmax_edited}" "${_tmax}")

# 12. Remove it by its id: the answer returns to the original, byte for byte.
step(0 _out thermal-load-remove "${DOC}" 2)
step(0 _restored thermal-solve "${DOC}" --json)
if(NOT _restored STREQUAL "${_solve}")
    message(FATAL_ERROR "removing the load did not restore the original result")
endif()
same_file_bytes("after add and remove of a load" "${DOC}" "${_hash_before}")
message(STATUS "adding and removing a load restores the document byte for byte")

# ---------------------------------------------------------------------------
# Failure paths: every one a REAL document state, driven through the real binary
# ---------------------------------------------------------------------------

# 13. A pure-Neumann steady model: take the convection and the held cap away.
set(SINGULAR "${WORK}/singular.bcad")
configure_file("${DOC}" "${SINGULAR}" COPYONLY)
step(0 _out thermal-boundary-remove "${SINGULAR}" 1)
step(0 _out thermal-boundary-remove "${SINGULAR}" 2)
hash_of("${SINGULAR}" _singular_hash)
foreach(_command thermal-solve thermal-temperature thermal-gradient thermal-heat-flux thermal-balance)
    step(1 _out ${_command} "${SINGULAR}" --json)
    expect_silent_stdout("${_out}")
    expect_code_on_stderr("${_last_err}" "no_temperature_datum")
endforeach()
# The validation report IS the answer here: the payload is on stdout AND the exit status says no.
step(1 _out thermal-validate "${SINGULAR}" --json)
clean_payload("${_out}")
member("${_out}" _accepted accepted)
member("${_out}" _stopped stoppedAt)
member("${_out}" _vstatus validation status)
if(_accepted)
    message(FATAL_ERROR "a pure-Neumann steady model was accepted")
endif()
same("the chain stops at the solve" "${_stopped}" "solve")
same("validation status" "${_vstatus}" "FAIL")
same_file_bytes("after the refused solves" "${SINGULAR}" "${_singular_hash}")

# 14. A missing material: unassign it through the CLI's own material verb.
set(NOMAT "${WORK}/no-material.bcad")
configure_file("${DOC}" "${NOMAT}" COPYONLY)
step(0 _out material-unassign "${NOMAT}")
hash_of("${NOMAT}" _nomat_hash)
step(1 _out thermal-solve "${NOMAT}" --json)
expect_silent_stdout("${_out}")
expect_code_on_stderr("${_last_err}" "material_missing")
step(1 _out thermal-validate "${NOMAT}" --json)
clean_payload("${_out}")
member("${_out}" _first validation findings 0 code)
same("the first finding" "${_first}" "material_missing")
same_file_bytes("after the refused solve (no material)" "${NOMAT}" "${_nomat_hash}")

# 15. An unresolved target: a well-formed face that names no face loads fine, is preserved, and refuses the solve.
set(UNRESOLVED "${WORK}/unresolved.bcad")
configure_file("${DOC}" "${UNRESOLVED}" COPYONLY)
step(0 _out thermal-load-add "${UNRESOLVED}" 7 --flux 250 --face face:Extrude001:side:9991)
step(0 _settings thermal-settings "${UNRESOLVED}" --json)
clean_payload("${_settings}")
string(JSON _loads LENGTH "${_settings}" loads)
same("the unresolved load is kept" "${_loads}" 2)
member("${_settings}" _ref loads 1 face)
member("${_settings}" _resolves loads 1 faceResolves)
same("the reference is exactly as written" "${_ref}" "face:Extrude001:side:9991")
if(_resolves)
    message(FATAL_ERROR "a load on a face that does not exist reports as resolving")
endif()
hash_of("${UNRESOLVED}" _unresolved_hash)
step(1 _out thermal-solve "${UNRESOLVED}" --json)
expect_silent_stdout("${_out}")
expect_code_on_stderr("${_last_err}" "loads_invalid")
string(FIND "${_last_err}" "Nothing nearby is substituted" _at)
if(_at EQUAL -1)
    message(FATAL_ERROR "the refusal does not say that no face was substituted: ${_last_err}")
endif()
same_file_bytes("after the refused solve (unresolved)" "${UNRESOLVED}" "${_unresolved_hash}")

# 16. Conflicting prescribed temperatures are refused, not ordered.
set(CONFLICT "${WORK}/conflict.bcad")
configure_file("${DOC}" "${CONFLICT}" COPYONLY)
step(0 _out thermal-boundary-add "${CONFLICT}" 5 face:Extrude001:start_cap --temperature 350)
step(1 _out thermal-solve "${CONFLICT}" --json)
expect_silent_stdout("${_out}")
expect_code_on_stderr("${_last_err}" "boundary_conditions_invalid")

# 17. Invalid edits: the core refuses (exit 1) or the command line is unreadable (exit 2), and the file is the same bytes.
hash_of("${DOC}" _hash_edit)
step(1 _out thermal-load-add "${DOC}" 1 --generation 5)
expect_code_on_stderr("${_last_err}" "duplicate_id")
step(1 _out thermal-load-remove "${DOC}" 99)
expect_code_on_stderr("${_last_err}" "id_not_found")
step(1 _out thermal-boundary-add "${DOC}" 9 face:Extrude001:end_cap --temperature -5)
expect_code_on_stderr("${_last_err}" "invalid_value")
step(1 _out thermal-boundary-add "${DOC}" 9 face:Extrude001:end_cap --convection 0 --ambient 290)
expect_code_on_stderr("${_last_err}" "invalid_value")
step(2 _out thermal-load-add "${DOC}" 9 --flux nan --face face:Extrude001:end_cap)
step(2 _out thermal-load-add "${DOC}" 9 --flux inf --face face:Extrude001:end_cap)
step(2 _out thermal-boundary-add "${DOC}" 9 face:Extrude001:end_cap --temperature 20degC)
step(2 _out thermal-load-add "${DOC}" 9 --node 12 --flux 5)
step(2 _out thermal-solve "${DOC}" --force)
step(2 _out thermal-solve)
step(1 _out thermal-solve "${WORK}/does-not-exist.bcad" --json)
expect_silent_stdout("${_out}")
expect_code_on_stderr("${_last_err}" "document_not_loaded")
same_file_bytes("after the rejected edits" "${DOC}" "${_hash_edit}")
message(STATUS "every rejected edit left the file byte-identical")

# ===========================================================================
# TRANSIENT: build one through the CLI, then the hand-written reference
# ===========================================================================

set(TDOC "${WORK}/transient.bcad")
configure_file("${BARE}" "${TDOC}" COPYONLY)
step(0 _out thermal-analysis-add "${TDOC}" --mode transient --initial-temperature 295.5 --time-step 0.3 --end-time 1.0 --output every-step)
step(0 _out thermal-load-add "${TDOC}" 1 --generation 2e5)
step(0 _out thermal-boundary-add "${TDOC}" 1 face:Extrude001:start_cap --temperature 300)
step(0 _out thermal-boundary-add "${TDOC}" 2 face:Extrude001:end_cap --convection 25 --ambient 293.15)

step(0 _settings thermal-settings "${TDOC}" --json)
clean_payload("${_settings}")
member("${_settings}" _tmode mode)
member("${_settings}" _tinit initialTemperature value)
member("${_settings}" _tstep transient timeStep value)
member("${_settings}" _tend transient endTime value)
member("${_settings}" _tout transient output)
same("mode" "${_tmode}" "transient")
same("initial temperature" "${_tinit}" "295.5")
same("time step" "${_tstep}" "0.3")
same("end time" "${_tend}" "1")
same("output" "${_tout}" "every_step")

hash_of("${TDOC}" _thash)
step(0 _tsolve thermal-solve "${TDOC}" --json)
clean_payload("${_tsolve}")
member("${_tsolve}" _tsteps stepCount)
member("${_tsolve}" _tstates storedStateCount)
member("${_tsolve}" _tstart startTime value)
member("${_tsolve}" _tendtime endTime value)
same("steps (0.3 does not divide 1.0: three whole steps and a remainder)" "${_tsteps}" 4)
same("states" "${_tstates}" 5)
same("start time" "${_tstart}" 0)
same("THE FINAL TIME IS THE END TIME, not 1.2" "${_tendtime}" 1)
same_file_bytes("after the transient solve" "${TDOC}" "${_thash}")

# The history: the core's stored times, in order, the last one exactly the end time.
step(0 _history thermal-temperature "${TDOC}" --json)
clean_payload("${_history}")
string(JSON _hist_len LENGTH "${_history}" history)
same("stored states" "${_hist_len}" 5)
member("${_history}" _h0 history 0 time value)
member("${_history}" _h4 history 4 time value)
same("first stored time" "${_h0}" 0)
same("last stored time" "${_h4}" 1)
member("${_history}" _final state time value)
same("the summary's state is the last one" "${_final}" 1)

step(0 _tbalance thermal-balance "${TDOC}" --json)
clean_payload("${_tbalance}")
string(JSON _step_count LENGTH "${_tbalance}" steps)
same("balance steps" "${_step_count}" 4)
member("${_tbalance}" _last_time steps 3 time value)
member("${_tbalance}" _first_size steps 0 stepSize value)
same("the last step ends at the end time" "${_last_time}" 1)
same("the first step is the nominal one" "${_first_size}" "0.3")
member("${_tbalance}" _t_balanced balanced)
if(NOT _t_balanced)
    message(FATAL_ERROR "the transient balance is out of tolerance")
endif()

step(0 _tvalidate thermal-validate "${TDOC}" --json)
clean_payload("${_tvalidate}")
member("${_tvalidate}" _t_acc accepted)
if(NOT _t_acc)
    message(FATAL_ERROR "the transient analysis is not accepted: ${_tvalidate}")
endif()

# A new process, the same transient run, the same payload.
step(0 _tsolve2 thermal-solve "${TDOC}" --json)
if(NOT _tsolve STREQUAL "${_tsolve2}")
    message(FATAL_ERROR "two processes ran the same transient analysis and printed different payloads")
endif()

# A transient pure-Neumann body is well posed by its capacity: take the boundary conditions away and it still runs.
set(TNEUMANN "${WORK}/transient-neumann.bcad")
configure_file("${TDOC}" "${TNEUMANN}" COPYONLY)
step(0 _out thermal-boundary-remove "${TNEUMANN}" 1)
step(0 _out thermal-boundary-remove "${TNEUMANN}" 2)
step(0 _neumann thermal-solve "${TNEUMANN}" --json)
clean_payload("${_neumann}")
member("${_neumann}" _constrained constrainedCount)
same("nothing is held" "${_constrained}" 0)

# A transient analysis needs a density and a specific heat; a steady one needs only a conductivity.
set(KONLY "${WORK}/k-only.bcad")
configure_file("${TDOC}" "${KONLY}" COPYONLY)
step(0 _out material-unset "${KONLY}" Steel specific_heat_capacity)
step(1 _out thermal-solve "${KONLY}" --json)
expect_silent_stdout("${_out}")
expect_code_on_stderr("${_last_err}" "material_incomplete_for_mode")
set(KONLY_STEADY "${WORK}/k-only-steady.bcad")
configure_file("${DOC}" "${KONLY_STEADY}" COPYONLY)
step(0 _out material-unset "${KONLY_STEADY}" Steel specific_heat_capacity)
step(0 _ksteady thermal-solve "${KONLY_STEADY}" --json)
clean_payload("${_ksteady}")

# The hand-written reference document (a plate with a hole, every kind of load and condition).
set(PLATE "${WORK}/reference.bcad")
configure_file("${TRANSIENT}" "${PLATE}" COPYONLY)
hash_of("${PLATE}" _plate_hash)
step(0 _plate thermal-solve "${PLATE}" --json)
clean_payload("${_plate}")
member("${_plate}" _plate_accepted validation accepted)
if(NOT _plate_accepted)
    message(FATAL_ERROR "the hand-written reference is not accepted")
endif()
same_file_bytes("after solving the reference" "${PLATE}" "${_plate_hash}")

# ===========================================================================
# THE BACKEND'S OWN OUTPUT must not reach the structured channel. A local size restriction makes Netgen write a warning to the standard
# output stream ("RestrictLocalH called, creating mesh-size tree"); it is contained in the meshing adapter, and nothing in the thermal CLI
# filters it. This document has one, so the path is live in this very process, and the payload must still be strict JSON.
# ===========================================================================

set(LOCAL "${WORK}/local-size.bcad")
configure_file("${DOC}" "${LOCAL}" COPYONLY)
# (0.8 mm on this face is the setting that visibly changes the mesh: OCCT triangulates a planar face with two triangles whatever the target,
# a P16-SIZE-001 finding, so a coarser local size would leave the node count where it was and prove nothing about the restriction.)
step(0 _out mesh-local-add "${LOCAL}" face:Extrude001:side:5 0.8mm)
step(0 _local thermal-solve "${LOCAL}" --json)
clean_payload("${_local}")
member("${_local}" _local_nodes nodeCount)
differs("the local size changed the mesh" "${_local_nodes}" "${_nodes}")
member("${_local}" _local_ok validation accepted)
if(NOT _local_ok)
    message(FATAL_ERROR "the locally refined analysis is not accepted")
endif()
string(FIND "${_local}" "RestrictLocalH" _chatter)
if(NOT _chatter EQUAL -1)
    message(FATAL_ERROR "the backend's warning reached the structured output")
endif()

# A MODEL THAT WILL NOT REGENERATE is the CLI's analogue of stale geometry: the thickness is zero, the extrusion cannot be built, and
# nothing that needs the model is answered. (A stale geometry or mesh proper cannot be presented to a one-shot process; see FAILURE_PATHS.md.)
set(BROKEN "${WORK}/broken.bcad")
file(READ "${DOC}" _broken_text)
string(REPLACE "\"si_value\": 0.02," "\"si_value\": 0.0," _broken_text "${_broken_text}")
file(WRITE "${BROKEN}" "${_broken_text}")
hash_of("${BROKEN}" _broken_hash)
step(1 _out thermal-solve "${BROKEN}" --json)
expect_silent_stdout("${_out}")
expect_code_on_stderr("${_last_err}" "regeneration_failed")
step(1 _out thermal-settings "${BROKEN}" --json)
expect_code_on_stderr("${_last_err}" "regeneration_failed")
same_file_bytes("after the refused commands on a model that will not regenerate" "${BROKEN}" "${_broken_hash}")

# ===========================================================================
# The CLI works from anywhere, on any path, and needs nothing but its own file
# ===========================================================================

# A different working directory, an absolute path.
set(_cwd "${WORK}/elsewhere")
step(0 _from_elsewhere thermal-solve "${DOC}" --json)
if(NOT _from_elsewhere STREQUAL "${_solve}")
    message(FATAL_ERROR "the payload depends on the working directory")
endif()

# A path with spaces.
set(SPACED "${WORK}/with space/plate doc.bcad")
configure_file("${DOC}" "${SPACED}" COPYONLY)
step(0 _spaced thermal-solve "${SPACED}" --json)
if(NOT _spaced STREQUAL "${_solve}")
    message(FATAL_ERROR "the payload depends on the path")
endif()
set(_cwd "${WORK}")

# ===========================================================================
# Prose mode and the batch driver
# ===========================================================================

step(0 _prose thermal-solve "${DOC}")
if(NOT _prose MATCHES "^Solved steady: [0-9]+ nodes, [0-9]+ tetrahedra")
    message(FATAL_ERROR "the prose form is not the documented one:\n${_prose}")
endif()
string(FIND "${_prose}" "{" _brace)
if(NOT _brace EQUAL -1)
    message(FATAL_ERROR "the prose form carries JSON")
endif()

set(BATCHDOC "${WORK}/batch.bcad")
configure_file("${BARE}" "${BATCHDOC}" COPYONLY)
file(WRITE "${WORK}/batch.txt"
    "# one transaction\nthermal-analysis-add\nthermal-load-add 1 --generation 1e5\nthermal-boundary-add 1 face:Extrude001:start_cap --temperature 300\nthermal-boundary-add 2 face:Extrude001:end_cap --convection 25 --ambient 293.15\n")
step(0 _out batch "${BATCHDOC}" "${WORK}/batch.txt")
# The batch built the same analysis the step-by-step workflow built, so it runs to the same answer.
step(0 _batched thermal-solve "${BATCHDOC}" --json)
if(NOT _batched STREQUAL "${_solve}")
    message(FATAL_ERROR "a batch and the same edits one at a time produced different analyses")
endif()
# A transaction with one bad line writes nothing.
hash_of("${BATCHDOC}" _batch_hash)
file(WRITE "${WORK}/bad-batch.txt" "thermal-load-add 5 --generation 7\nthermal-load-add 1 --generation 9\n")
step(1 _out batch "${BATCHDOC}" "${WORK}/bad-batch.txt")
same_file_bytes("after the failed batch" "${BATCHDOC}" "${_batch_hash}")

# ---------------------------------------------------------------------------
# THE STRICT RE-PARSE of the raw bytes every process wrote
# ---------------------------------------------------------------------------
#
# string(JSON) is lenient (see the header), so the payload files are handed to the strict reader the in-process tests use. A hidden test
# case, run by name: it fails if any non-empty .json payload is not exactly one RFC 8259 object followed by a line ending, and if fewer
# than twenty-five were parsed (a workflow that wrote nothing must not pass).
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "BETTERCAD_THERMAL_WORKFLOW_DIR=${WORK}" "${TESTS}" "ThermalCliWorkflow_EveryPayloadIsStrictJson"
    RESULT_VARIABLE _strict_code
    OUTPUT_VARIABLE _strict_out
    ERROR_VARIABLE _strict_err)
file(APPEND "${LEDGER}" "\nstrict re-parse of the raw payloads: exit ${_strict_code}\n${_strict_out}\n")
if(NOT _strict_code EQUAL 0)
    message(FATAL_ERROR "the strict re-parse of the payloads FAILED (exit ${_strict_code}):\n${_strict_out}\n${_strict_err}")
endif()
message(STATUS "every payload is strict JSON: ${_strict_out}")

# ---------------------------------------------------------------------------
# ZERO-MATCH PROTECTION for the script itself
# ---------------------------------------------------------------------------
#
# A workflow that skipped every step and exited 0 would be the same defect as a ctest filter that matched nothing. So the step COUNT is
# asserted: if a step is removed or an early return is introduced, this fails.
message(STATUS "steps executed: ${_steps}")
# EXACT, not a floor: a step removed and a step added both change the evidence, and either must be a deliberate edit to this number.
if(NOT _steps EQUAL 74)
    message(FATAL_ERROR
        "${_steps} steps ran; the workflow has 74. A script that exits 0 having skipped steps is the defect this check exists for.")
endif()
file(APPEND "${LEDGER}" "\nsteps ${_steps}\n")

message(STATUS "ThermalWorkflow: ${_steps} processes, every exit code asserted, every payload parsed twice")
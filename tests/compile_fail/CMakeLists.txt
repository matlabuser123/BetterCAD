# Build-failure tests: prove that misuse is rejected by the compiler.
#
# Each group has one source file. The file compiled without any
# BETTERCAD_CF_* macro is the group's control target: it is part of the normal
# build and must compile cleanly. Each case compiles the same file with one
# BETTERCAD_CF_<CASE> macro and expects the build to fail with a case-specific
# diagnostic. Because the control compiles, every failure is attributable to
# the selected line.

# bettercad_compile_fail_group(<group> <source>)
function(bettercad_compile_fail_group group source)
    add_library(bettercad_cf_${group}_control OBJECT "${source}")
    target_link_libraries(bettercad_cf_${group}_control PRIVATE
        BetterCAD::core BetterCAD::compiler_options)
endfunction()

# bettercad_compile_fail_case(<group> <source> <CASE> <gnu_regex>)
#
# Diagnostics are matched for GCC (the verified toolchain); other compilers
# only need to report an error. Regexes avoid quote characters because GCC
# prints Unicode quotes in UTF-8 locales.
function(bettercad_compile_fail_case group source case gnu_regex)
    string(TOLOWER "${case}" name)
    string(REPLACE "_" "-" name "${name}")
    set(target "bettercad_cf_${group}_${name}")

    add_library(${target} OBJECT EXCLUDE_FROM_ALL "${source}")
    target_compile_definitions(${target} PRIVATE "BETTERCAD_CF_${case}")
    target_link_libraries(${target} PRIVATE BetterCAD::core BetterCAD::compiler_options)
    target_compile_options(${target} PRIVATE
        "$<$<CXX_COMPILER_ID:GNU>:-fdiagnostics-plain-output>")

    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set(regex "${gnu_regex}")
    else()
        set(regex "error")
    endif()
    bettercad_add_process_test(compile_fail.${group}.${name}
        COMMAND "${CMAKE_COMMAND}"
        ARGS "--build|${PROJECT_BINARY_DIR}|--config|$<CONFIG>|--target|${target}"
        EXIT_CODE 1
        STDOUT_REGEX "${regex}"
    )
    # Tests that drive the build tree must not run concurrently.
    set_tests_properties(compile_fail.${group}.${name} PROPERTIES RESOURCE_LOCK bettercad_build_tree)
endfunction()

# --- Units (P1-001) ---------------------------------------------------------
set(_units "${CMAKE_CURRENT_SOURCE_DIR}/UnitsMisuse.cpp")
bettercad_compile_fail_group(units "${_units}")
bettercad_compile_fail_case(units "${_units}" ADD_LENGTH_ANGLE "no match for [^ ]*operator\\+")
bettercad_compile_fail_case(units "${_units}" ASSIGN_MASS_TO_LENGTH "no match for [^ ]*operator=")
bettercad_compile_fail_case(units "${_units}" ASSIGN_DOUBLE_TO_LENGTH "no match for [^ ]*operator=.*double")
bettercad_compile_fail_case(units "${_units}" LENGTH_TO_DOUBLE "cannot convert .* to [^ ]*double")
bettercad_compile_fail_case(units "${_units}" FORCE_PER_LENGTH_AS_PRESSURE "conversion from .* to non-scalar type")
bettercad_compile_fail_case(units "${_units}" COMPARE_LENGTH_TIME "no match for [^ ]*operator<")
bettercad_compile_fail_case(units "${_units}" SQRT_OF_VOLUME "no matching function for call to [^ ]*sqrt")

# P15-UNITS-001: the engineering-data quantities.
bettercad_compile_fail_case(units "${_units}" ADD_DENSITY_PRESSURE "no match for [^ ]*operator\+")
bettercad_compile_fail_case(units "${_units}" MODULUS_AS_CONDUCTIVITY "could not convert")
bettercad_compile_fail_case(units "${_units}" LENGTH_AS_DENSITY "could not convert")
bettercad_compile_fail_case(units "${_units}" SPECIFIC_HEAT_AS_EXPANSION "conversion from .* to non-scalar type")
bettercad_compile_fail_case(units "${_units}" MASS_PER_AREA_AS_DENSITY "conversion from .* to non-scalar type")
bettercad_compile_fail_case(units "${_units}" TEMPERATURE_AS_EXPANSION "could not convert")
bettercad_compile_fail_case(units "${_units}" POISSON_FROM_DOUBLE "conversion from [^ ]*double[^ ]* to non-scalar type")
bettercad_compile_fail_case(units "${_units}" POISSON_TO_DOUBLE "cannot convert")
bettercad_compile_fail_case(units "${_units}" POISSON_AS_KINEMATIC_VISCOSITY "could not convert")
bettercad_compile_fail_case(units "${_units}" DYNAMIC_AS_KINEMATIC_VISCOSITY "could not convert")
bettercad_compile_fail_case(units "${_units}" CONDUCTIVITY_AS_SPECIFIC_HEAT "conversion from .* to non-scalar type")

# --- Strong IDs (P1-002) ----------------------------------------------------
set(_ids "${CMAKE_CURRENT_SOURCE_DIR}/IdsMisuse.cpp")
bettercad_compile_fail_group(ids "${_ids}")
bettercad_compile_fail_case(ids "${_ids}" SKETCH_ID_AS_FEATURE_ID "could not convert .*SketchId.* to .*FeatureId")
bettercad_compile_fail_case(ids "${_ids}" INTEGER_TO_ID "conversion from [^ ]*int[^ ]* to non-scalar type")
bettercad_compile_fail_case(ids "${_ids}" ID_TO_INTEGER "cannot convert .*SketchId.* in initialization")
bettercad_compile_fail_case(ids "${_ids}" COMPARE_DIFFERENT_KINDS "no match for [^ ]*operator==")
bettercad_compile_fail_case(ids "${_ids}" OBJECT_TO_SKETCH_ID "conversion from .*ObjectIdTag.* to non-scalar type")
bettercad_compile_fail_case(ids "${_ids}" OBJECT_TO_COMPONENT_ID "conversion from .*ObjectIdTag.* to non-scalar type")
bettercad_compile_fail_case(ids "${_ids}" COMPONENT_ID_AS_FEATURE_ID "conversion from .*ComponentIdTag.* to non-scalar type .*FeatureIdTag")
bettercad_compile_fail_case(ids "${_ids}" OBJECT_TO_MATE_ID "conversion from .*ObjectIdTag.* to non-scalar type")
bettercad_compile_fail_case(ids "${_ids}" MATE_ID_AS_COMPONENT_ID "conversion from .*MateIdTag.* to non-scalar type .*ComponentIdTag")
bettercad_compile_fail_case(ids "${_ids}" OBJECT_TO_SHEET_ID "conversion from .*ObjectIdTag.* to non-scalar type")
bettercad_compile_fail_case(ids "${_ids}" SHEET_ID_AS_COMPONENT_ID "conversion from .*SheetIdTag.* to non-scalar type .*ComponentIdTag")
bettercad_compile_fail_case(ids "${_ids}" ENTITY_ID_AS_OBJECT_ID "could not convert .*EntityId")

# Helpers that give every BetterCAD target the same conventions:
#   * public headers live in include/bettercad/<module>/
#   * private headers live next to the sources in src/<module>/
#   * generated headers live in ${BETTERCAD_GENERATED_INCLUDE_DIR}
#   * shared-library symbol export via a generated <module>/Export.hpp
include_guard(GLOBAL)

include(GenerateExportHeader)

# bettercad_add_library(<target>
#     ALIAS <name>                 # creates BetterCAD::<name>
#     EXPORT_BASE <MACRO_BASE>     # e.g. BETTERCAD_CORE -> BETTERCAD_CORE_EXPORT
#     EXPORT_HEADER <path>         # relative to the generated include dir
#     [SOURCES ...] [PUBLIC_LINK ...] [PRIVATE_LINK ...])
function(bettercad_add_library target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "ALIAS;EXPORT_BASE;EXPORT_HEADER"
        "SOURCES;PUBLIC_LINK;PRIVATE_LINK")
    foreach(required ALIAS EXPORT_BASE EXPORT_HEADER)
        if(NOT arg_${required})
            message(FATAL_ERROR "bettercad_add_library(${target}): ${required} is required")
        endif()
    endforeach()

    add_library(${target} ${arg_SOURCES})
    add_library(BetterCAD::${arg_ALIAS} ALIAS ${target})

    target_include_directories(${target}
        PUBLIC
            "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
            "$<BUILD_INTERFACE:${BETTERCAD_GENERATED_INCLUDE_DIR}>"
            "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
        PRIVATE
            "${PROJECT_SOURCE_DIR}/src"
    )
    target_compile_features(${target} PUBLIC cxx_std_23)

    generate_export_header(${target}
        BASE_NAME ${arg_EXPORT_BASE}
        EXPORT_FILE_NAME "${BETTERCAD_GENERATED_INCLUDE_DIR}/${arg_EXPORT_HEADER}"
    )
    if(NOT BUILD_SHARED_LIBS)
        target_compile_definitions(${target} PUBLIC ${arg_EXPORT_BASE}_STATIC_DEFINE)
    endif()

    target_link_libraries(${target}
        PUBLIC ${arg_PUBLIC_LINK}
        PRIVATE ${arg_PRIVATE_LINK} "$<BUILD_INTERFACE:BetterCAD::compiler_options>"
    )
    set_target_properties(${target} PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
    )
endfunction()

# bettercad_add_executable(<target> [OUTPUT_NAME <name>] [SOURCES ...] [LINK ...])
function(bettercad_add_executable target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "OUTPUT_NAME" "SOURCES;LINK")
    add_executable(${target} ${arg_SOURCES})
    target_link_libraries(${target} PRIVATE ${arg_LINK} BetterCAD::compiler_options)
    if(arg_OUTPUT_NAME)
        set_target_properties(${target} PROPERTIES OUTPUT_NAME ${arg_OUTPUT_NAME})
    endif()
endfunction()

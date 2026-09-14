# Compiler options applied to BetterCAD's own targets (never to third-party
# code). Link BetterCAD::compiler_options PRIVATE; bettercad_add_library() and
# bettercad_add_executable() do this automatically.
include_guard(GLOBAL)

add_library(bettercad_compiler_options INTERFACE)
add_library(BetterCAD::compiler_options ALIAS bettercad_compiler_options)

if(MSVC)
    set(_bettercad_warnings
        /W4 /permissive-
        /w14242 /w14254 /w14263 /w14265 /w14287 /we4289 /w14296 /w14311
        /w14545 /w14546 /w14547 /w14549 /w14555 /w14619 /w14640 /w14826
        /w14905 /w14906 /w14928
    )
    set(_bettercad_options /utf-8 /Zc:__cplusplus /Zc:preprocessor /bigobj)
    if(BETTERCAD_WARNINGS_AS_ERRORS)
        list(APPEND _bettercad_warnings /WX)
    endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(_bettercad_warnings
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
        -Wunused
        -Woverloaded-virtual
        -Wconversion
        -Wsign-conversion
        -Wdouble-promotion
        -Wformat=2
        -Wimplicit-fallthrough
        -Wextra-semi
        -Wundef
        -Wmissing-declarations
        -Wsuggest-override
    )
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        list(APPEND _bettercad_warnings
            -Wmisleading-indentation
            -Wduplicated-cond
            -Wduplicated-branches
            -Wlogical-op
        )
    endif()
    set(_bettercad_options)
    if(MINGW)
        # Template-heavy translation units exceed the default COFF section limit.
        list(APPEND _bettercad_options -Wa,-mbig-obj)
    endif()
    if(BETTERCAD_WARNINGS_AS_ERRORS)
        list(APPEND _bettercad_warnings -Werror)
    endif()
else()
    message(WARNING "Unrecognised compiler '${CMAKE_CXX_COMPILER_ID}': no warning flags applied")
endif()

target_compile_options(bettercad_compiler_options INTERFACE
    "$<$<COMPILE_LANGUAGE:CXX>:${_bettercad_warnings};${_bettercad_options}>"
)

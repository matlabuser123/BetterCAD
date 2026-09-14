# Runtime layout of the build tree.
#
# MinGW: copy the C++ runtime DLLs of the compiler into the runtime output
# directory. Executables then start without the compiler on PATH, and always
# load the runtime they were compiled against rather than an older
# libstdc++-6.dll that happens to appear earlier on PATH (the loader searches
# the executable's directory first).
include_guard(GLOBAL)

# bettercad_copy_runtime_dlls(<target>)
#
# Windows: after building <target>, copy the DLLs of the imported shared
# libraries it links (for example Open CASCADE) next to it, so the build tree
# runs without extra PATH setup.
function(bettercad_copy_runtime_dlls target)
    if(WIN32)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "$<$<BOOL:$<TARGET_RUNTIME_DLLS:${target}>>:${CMAKE_COMMAND};-E;copy_if_different;$<TARGET_RUNTIME_DLLS:${target}>;$<TARGET_FILE_DIR:${target}>>"
            COMMAND_EXPAND_LISTS
            VERBATIM
        )
    endif()
endfunction()

set(BETTERCAD_RUNTIME_DLLS)
if(MINGW)
    get_filename_component(_bettercad_compiler_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    foreach(_dll IN ITEMS libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll)
        if(EXISTS "${_bettercad_compiler_bin}/${_dll}")
            list(APPEND BETTERCAD_RUNTIME_DLLS "${_bettercad_compiler_bin}/${_dll}")
        else()
            message(WARNING "MinGW runtime library not found next to the compiler: ${_dll}")
        endif()
    endforeach()
    if(BETTERCAD_RUNTIME_DLLS)
        file(COPY ${BETTERCAD_RUNTIME_DLLS} DESTINATION "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
        install(FILES ${BETTERCAD_RUNTIME_DLLS} DESTINATION "${CMAKE_INSTALL_BINDIR}")
    endif()
endif()

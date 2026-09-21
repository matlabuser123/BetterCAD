# Command implementations live in a library so the test suite can drive the
# CLI in-process; the executable is a thin argv adapter.
add_library(bettercad_cli_lib STATIC
    Arguments.cpp
    Arguments.hpp
    AssemblyEdits.cpp
    AssemblyReports.cpp
    BatchCommand.cpp
    Cli.cpp
    Cli.hpp
    Commands.hpp
    DocumentCommands.cpp
    Edits.hpp
    ExportCommands.cpp
    Selectors.cpp
    Selectors.hpp
)
add_library(BetterCAD::cli_lib ALIAS bettercad_cli_lib)
target_include_directories(bettercad_cli_lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(bettercad_cli_lib
    PUBLIC BetterCAD::core
    PRIVATE
        BetterCAD::assembly
        BetterCAD::compiler_options
        BetterCAD::features
        BetterCAD::io
        BetterCAD::sketch
)

bettercad_add_executable(bettercad_cli
    OUTPUT_NAME bettercad-cli
    SOURCES main.cpp
    LINK BetterCAD::cli_lib
)
# The Windows entry point is wmain() (UTF-16 command line); MinGW needs
# -municode to use it.
if(MINGW)
    target_link_options(bettercad_cli PRIVATE -municode)
endif()
bettercad_copy_runtime_dlls(bettercad_cli)

install(TARGETS bettercad_cli RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

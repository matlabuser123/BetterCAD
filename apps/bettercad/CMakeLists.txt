bettercad_add_executable(bettercad
    SOURCES
        main.cpp
        MainWindow.cpp
        MainWindow.hpp
    LINK
        BetterCAD::core
        Qt6::Widgets
)
set_target_properties(bettercad PROPERTIES
    AUTOMOC ON
    WIN32_EXECUTABLE ON
)

# Make the build tree runnable on Windows: deploy the Qt libraries and plugins
# (platform integration, styles) next to the executable. The compiler runtime
# is provided by cmake/BetterCADRuntime.cmake.
if(WIN32)
    # windeployqt resolves the Qt binary directory through its 8.3 short name,
    # so a prefix whose short name is ambiguous deploys the wrong files or
    # none. Refuse that here rather than at the end of a GUI build
    # (INFRA-QT-DEPLOY-001).
    bettercad_check_deps_prefix_resolvable("${BETTERCAD_DEPS_PREFIX}")

    find_program(BETTERCAD_WINDEPLOYQT windeployqt
        HINTS "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}"
        NO_DEFAULT_PATH
    )
    if(BETTERCAD_WINDEPLOYQT)
        add_custom_command(TARGET bettercad POST_BUILD
            COMMAND "${CMAKE_COMMAND}"
                "-DWINDEPLOYQT=${BETTERCAD_WINDEPLOYQT}"
                "-DEXECUTABLE=$<TARGET_FILE:bettercad>"
                "-DQT_BIN_DIR=${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}"
                -P "${PROJECT_SOURCE_DIR}/cmake/DeployQtRuntime.cmake"
            COMMENT "Deploying Qt runtime for bettercad"
            VERBATIM
        )
    else()
        message(WARNING "windeployqt not found; bettercad will need Qt on PATH to run")
    endif()
endif()

install(TARGETS bettercad RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

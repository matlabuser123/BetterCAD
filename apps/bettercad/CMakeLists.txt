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
    find_program(BETTERCAD_WINDEPLOYQT windeployqt
        HINTS "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}"
        NO_DEFAULT_PATH
    )
    if(BETTERCAD_WINDEPLOYQT)
        add_custom_command(TARGET bettercad POST_BUILD
            COMMAND "${CMAKE_COMMAND}"
                "-DWINDEPLOYQT=${BETTERCAD_WINDEPLOYQT}"
                "-DEXECUTABLE=$<TARGET_FILE:bettercad>"
                -P "${PROJECT_SOURCE_DIR}/cmake/DeployQtRuntime.cmake"
            COMMENT "Deploying Qt runtime for bettercad"
            VERBATIM
        )
    else()
        message(WARNING "windeployqt not found; bettercad will need Qt on PATH to run")
    endif()
endif()

install(TARGETS bettercad RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

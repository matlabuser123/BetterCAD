if(BETTERCAD_BUILD_CLI)
    add_subdirectory(bettercad_cli)
endif()

if(BETTERCAD_GUI_ENABLED)
    add_subdirectory(bettercad)
endif()

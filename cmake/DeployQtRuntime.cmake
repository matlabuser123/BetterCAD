# Script mode (cmake -P): deploy the Qt runtime next to an executable.
#
# Wraps windeployqt so that its advisory messages (for example about the
# optional qttranslations catalog) do not clutter successful builds; the full
# output is shown only if deployment fails.
#
# Inputs: WINDEPLOYQT, EXECUTABLE
foreach(required WINDEPLOYQT EXECUTABLE)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "DeployQtRuntime.cmake: ${required} is not set")
    endif()
endforeach()

execute_process(
    COMMAND "${WINDEPLOYQT}"
        --no-translations
        --no-compiler-runtime
        --no-system-d3d-compiler
        --no-system-dxc-compiler
        # Headless platform used by automated GUI tests (-platform offscreen).
        --include-plugins qoffscreen
        --verbose 1
        "${EXECUTABLE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "windeployqt failed (${result}):\n${output}")
endif()

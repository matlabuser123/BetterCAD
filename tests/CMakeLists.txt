include(Catch)

# ---------------------------------------------------------------------------
# Verification tooling that needs the geometry kernel directly (reading
# exported STEP files back). Test-only: STEP import is not a product feature
# yet. Like product code, only an occt/ directory talks to Open CASCADE.
# ---------------------------------------------------------------------------
add_library(bettercad_test_occt STATIC
    support/occt/StepReadBack.cpp
    support/occt/StepReadBack.hpp
)
target_include_directories(bettercad_test_occt PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(bettercad_test_occt PUBLIC cxx_std_23)
target_compile_definitions(bettercad_test_occt PRIVATE _USE_MATH_DEFINES)
target_link_libraries(bettercad_test_occt
    PRIVATE
        BetterCAD::compiler_options
        TKernel TKMath TKG3d TKGeomBase TKBRep TKTopAlgo TKXSBase TKDESTEP TKLCAF TKXCAF
)

# ---------------------------------------------------------------------------
# Unit tests (Catch2), one CTest test per TEST_CASE
# ---------------------------------------------------------------------------
bettercad_add_executable(bettercad_tests
    SOURCES
        TestHelpers.hpp
        assembly/CommandTests.cpp
        drawing/AnnotationTests.cpp
        drawing/AssemblyDrawingTests.cpp
        drawing/BomTests.cpp
        drawing/StableReferenceTests.cpp
        drawing/DrawingCommandTests.cpp
        drawing/DrawingPersistenceTests.cpp
        drawing/DrawingRegenerationTests.cpp
        drawing/DimensionTests.cpp
        drawing/HiddenLineViewTests.cpp
        drawing/Section2dTests.cpp
        drawing/SectionGeometryTests.cpp
        drawing/SheetFileTests.cpp
        drawing/SheetTests.cpp
        drawing/ToleranceTests.cpp
        drawing/ViewAssemblyTests.cpp
        drawing/ViewKindTests.cpp
        drawing/ViewProjectionTests.cpp
        drawing/ViewTests.cpp
        assembly/ComponentFileTests.cpp
        assembly/ComponentTests.cpp
        assembly/ConfigurationTests.cpp
        assembly/MateFileTests.cpp
        assembly/MateTests.cpp
        assembly/MechanicalMateTests.cpp
        assembly/MechanicalSolveTests.cpp
        assembly/SolverDerivativeTests.cpp
        assembly/SolverTests.cpp
        assembly/StableReferenceTests.cpp
        assembly/RegenerationTests.cpp
        assembly/ReferenceFileTests.cpp
        assembly/ReferenceTests.cpp
        assembly/PersistenceTests.cpp
        assembly/PlacementFileTests.cpp
        assembly/PlacementTests.cpp
        core/BuildInfoTests.cpp
        core/IdTests.cpp
        core/UuidTests.cpp
        core/document/CommandTests.cpp
        core/document/ConfigurationTests.cpp
        core/document/DependencyGraphTests.cpp
        core/document/DocumentTests.cpp
        core/document/ParameterExpressionTests.cpp
        core/document/TestObjects.hpp
        core/geometry/BooleanTests.cpp
        core/geometry/ChamferTests.cpp
        core/geometry/CurvedProfileTests.cpp
        core/geometry/DraftTests.cpp
        core/geometry/FaceNameTests.cpp
        core/geometry/FilletTests.cpp
        core/geometry/GeometryTestSupport.hpp
        core/geometry/HiddenLineTests.cpp
        core/geometry/HoleTests.cpp
        core/geometry/LoftTests.cpp
        core/geometry/LoftShapeTests.cpp
        core/geometry/MeshTests.cpp
        core/geometry/PrimitiveTests.cpp
        core/geometry/PropertyTests.cpp
        core/geometry/RevolutionTests.cpp
        core/geometry/RibTests.cpp
        core/geometry/VariableFilletTests.cpp
        core/geometry/ShellTests.cpp
        core/geometry/SplitTests.cpp
        core/geometry/SweepTests.cpp
        core/geometry/SweptPathTests.cpp
        core/geometry/TransformTests.cpp
        core/math/BSplineTests.cpp
        core/math/FrameTests.cpp
        core/math/RigidTransformTests.cpp
        core/parameters/ExpressionTests.cpp
        core/parameters/ParameterTableTests.cpp
        core/parameters/ParameterTests.cpp
        core/standards/HoleStandardsTests.cpp
        core/materials/MaterialLibraryTests.cpp
        core/units/ConversionTests.cpp
        core/units/FormatAndCatalogTests.cpp
        core/units/EngineeringQuantityTests.cpp
        core/units/QuantityTests.cpp
        features/ChamferFeatureTests.cpp
        features/MaterialTests.cpp
        features/DatumTests.cpp
        features/DraftFeatureTests.cpp
        features/FaceReferenceTests.cpp
        features/SketchOnFaceTests.cpp
        features/ThroughAllExtrudeTests.cpp
        features/BodyOpsTests.cpp
        features/CircularPatternFeatureTests.cpp
        features/ConfigurationFeatureTests.cpp
        features/ExpressionRegenerationTests.cpp
        features/ExtrudeTests.cpp
        features/FeatureTestSupport.hpp
        features/FilletFeatureTests.cpp
        features/HoleFeatureTests.cpp
        features/HoleStandardTests.cpp
        features/LinearPatternFeatureTests.cpp
        features/LoftFeatureTests.cpp
        features/LoftShapeFeatureTests.cpp
        features/MirrorFeatureTests.cpp
        features/PatternDistributionTests.cpp
        features/PatternNestingTests.cpp
        features/ProfileTests.cpp
        features/SpatialSweepTests.cpp
        features/SweepFeatureTests.cpp
        features/RegenerationTests.cpp
        features/RegeneratorTests.cpp
        features/RevolveTests.cpp
        features/RibFeatureTests.cpp
        features/VariableFilletFeatureTests.cpp
        features/ShellFeatureTests.cpp
        features/ValidationTests.cpp
        io/ChamferFileTests.cpp
        io/CircularPatternFileTests.cpp
        io/ConfigurationFileTests.cpp
        io/DatumFileTests.cpp
        io/FaceReferenceFileTests.cpp
        io/FaceKindFileTests.cpp
        io/ThroughAllFileTests.cpp
        io/BodyOpsFileTests.cpp
        io/DrawingExportSupport.hpp
        io/DrawingExportTests.cpp
        io/DocumentFileTests.cpp
        io/DraftFileTests.cpp
        io/ExampleModelTests.cpp
        io/ExpressionFileTests.cpp
        io/FilletFileTests.cpp
        io/HoleFileTests.cpp
        io/HoleStandardFileTests.cpp
        io/LinearPatternFileTests.cpp
        io/LoftFileTests.cpp
        io/MirrorFileTests.cpp
        io/ParameterJsonTests.cpp
        io/PatternInstanceFileTests.cpp
        io/SpatialSweepFileTests.cpp
        io/SweepFileTests.cpp
        io/RevolveFileTests.cpp
        io/RibFileTests.cpp
        io/VariableFilletFileTests.cpp
        io/ShellFileTests.cpp
        io/StepExportTests.cpp
        io/StlTests.cpp
        sketch/AdvancedConstraintDocumentTests.cpp
        sketch/AdvancedConstraintSolverTests.cpp
        sketch/ConstraintTests.cpp
        sketch/EllipseSplineDocumentTests.cpp
        sketch/EllipseSplineTests.cpp
        sketch/SketchDocumentTests.cpp
        sketch/SketchTests.cpp
        sketch/SolverTests.cpp
        support/BlockModel.hpp
        support/ConfigurationModels.hpp
        support/BodyPrint.hpp
        support/BodyOpsModels.hpp
        support/BracketModel.hpp
        support/ChamferBlockModel.hpp
        support/DatumModels.hpp
        support/DraftModels.hpp
        support/FaceKindModels.hpp
        support/FaceModels.hpp
        support/DrivenPlateModel.hpp
        support/FilletModels.hpp
        support/HoleModels.hpp
        support/HoleStandardModels.hpp
        support/LoftModels.hpp
        support/MeshAnalysis.hpp
        support/MirrorModels.hpp
        support/RibModels.hpp
        support/VariableFilletModels.hpp
        support/ShellModels.hpp
        support/PatternModels.hpp
        support/SweepModels.hpp
        support/TestFiles.hpp
        support/ThroughCutModels.hpp
        support/TurnedPartModel.hpp
    LINK
        BetterCAD::assembly
        BetterCAD::core
        BetterCAD::drawing
        BetterCAD::features
        BetterCAD::geometry
        BetterCAD::io
        BetterCAD::sketch
        bettercad_test_occt
        Catch2::Catch2WithMain
)
bettercad_copy_runtime_dlls(bettercad_tests)
# The solver's derivative tests compare its analytic Jacobian against finite
# differences, which needs the solver's own System. That is the one place a
# test reaches past a public API in this repository; see
# docs/verification/P13-SOLVE-001/ for why it is a deliberate exception.
target_include_directories(bettercad_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(bettercad_tests PRIVATE BetterCAD::eigen)
target_compile_definitions(bettercad_tests PRIVATE
    "BETTERCAD_EXPECTED_VERSION=\"${PROJECT_VERSION}\""
    "BETTERCAD_EXPECTED_BUILD_CONFIG=\"$<CONFIG>\""
    "$<$<BOOL:${BUILD_SHARED_LIBS}>:BETTERCAD_EXPECT_SHARED_LIBS>"
    "BETTERCAD_EXAMPLE_MODELS_DIR=\"${PROJECT_SOURCE_DIR}/examples/models\""
)
if(TARGET bettercad_cli_lib)
    target_sources(bettercad_tests PRIVATE
        cli/CliRunner.hpp
        cli/CliTests.cpp
        cli/ConfigurationCliTests.cpp
        cli/DatumCliTests.cpp
        cli/DraftCliTests.cpp
        cli/FaceReferenceCliTests.cpp
        cli/HoleStandardCliTests.cpp
        cli/PatternInstanceCliTests.cpp
        cli/ThroughAllCliTests.cpp
        cli/BodyOpsCliTests.cpp
        cli/RibCliTests.cpp
        cli/VariableFilletCliTests.cpp
        cli/ShellCliTests.cpp
        cli/SpatialSweepCliTests.cpp
        cli/ExpressionCliTests.cpp
        cli/ComponentCliTests.cpp
        cli/SheetCliTests.cpp
        cli/AssemblyCliTests.cpp
        cli/DrawingCliTests.cpp
        reference/AssemblyWorkflowTests.cpp
        io/AssemblyStepTests.cpp
    )
    target_link_libraries(bettercad_tests PRIVATE BetterCAD::cli_lib)
endif()
# The mechanical reference models (P11-REF-001) are built by the examples'
# library, with the same public APIs as any client.
if(TARGET bettercad_reference_models)
    target_sources(bettercad_tests PRIVATE
        reference/AllModelsTests.cpp
        reference/AssemblyModelsTests.cpp
        reference/Analytic.hpp
        reference/DrawingTestSupport.hpp
        reference/DrawingStepPlateTests.cpp
        reference/DrawingModelsTests.cpp
        reference/DrawingAngleBracketTests.cpp
        reference/DrawingPocketBlockTests.cpp
        reference/DrawingHolePlateTests.cpp
        reference/DrawingToleranceBlockTests.cpp
        reference/DrawingClampSetTests.cpp
        reference/DrawingBomTests.cpp
        reference/DrawingEquivalenceTests.cpp
        reference/ReferenceTestSupport.hpp
        reference/FlangeTests.cpp
        reference/GearboxCoverTests.cpp
        reference/BearingHousingTests.cpp
        reference/ManifoldTubeTests.cpp
        reference/IndexPlateTests.cpp
        reference/MotorMountTests.cpp
        reference/MountingBracketTests.cpp
        reference/P12ModelsTests.cpp
        reference/PulleyTests.cpp
        reference/RibbedBracketTests.cpp
        reference/ShaftTests.cpp
        reference/TransitionDuctTests.cpp
        reference/UBoltTests.cpp
    )
    target_link_libraries(bettercad_tests PRIVATE BetterCAD::reference_models)
endif()

# PRE_TEST discovery runs at ctest time, so a missing runtime DLL shows up as a
# failing test rather than a broken build.
catch_discover_tests(bettercad_tests
    DISCOVERY_MODE PRE_TEST
    TEST_PREFIX "unit."
)

# ---------------------------------------------------------------------------
# Process-level tests: launch the real executables
# ---------------------------------------------------------------------------
set(_run_and_check "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunAndCheck.cmake")

# bettercad_add_process_test(<name> COMMAND <exe> [ARGS a|b|c]
#     EXIT_CODE <n> [STDOUT_REGEX <re>] [STDERR_REGEX <re>]
#     [FILE <path> [FILE_REGEX <re>]]
#     [COPY_FROM <src> COPY_TO <dst>])
#
# COPY_FROM/COPY_TO copies a pristine input into place before the run, which
# is what lets a test that EDITS a document survive `ctest --repeat`.
function(bettercad_add_process_test name)
    cmake_parse_arguments(PARSE_ARGV 1 arg ""
        "COMMAND;ARGS;EXIT_CODE;STDOUT_REGEX;STDERR_REGEX;FILE;FILE_REGEX;COPY_FROM;COPY_TO" "")
    add_test(NAME ${name}
        COMMAND "${CMAKE_COMMAND}"
            "-DCOMMAND=${arg_COMMAND}"
            "-DARGS=${arg_ARGS}"
            "-DEXPECTED_EXIT_CODE=${arg_EXIT_CODE}"
            "-DEXPECTED_STDOUT_REGEX=${arg_STDOUT_REGEX}"
            "-DEXPECTED_STDERR_REGEX=${arg_STDERR_REGEX}"
            "-DEXPECTED_FILE=${arg_FILE}"
            "-DEXPECTED_FILE_REGEX=${arg_FILE_REGEX}"
            "-DCOPY_FROM=${arg_COPY_FROM}"
            "-DCOPY_TO=${arg_COPY_TO}"
            -P "${_run_and_check}"
    )
endfunction()

if(TARGET bettercad_cli)
    string(REPLACE "." "\\." _version_regex "${PROJECT_VERSION}")
    bettercad_add_process_test(cli.launch.version
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "--version"
        EXIT_CODE 0 STDOUT_REGEX "^bettercad-cli ${_version_regex}$")
    bettercad_add_process_test(cli.launch.version-command
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "version"
        EXIT_CODE 0 STDOUT_REGEX "BetterCAD ${_version_regex}.*compiler   : [A-Za-z]+ [0-9]")
    bettercad_add_process_test(cli.launch.help
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "--help"
        EXIT_CODE 0 STDOUT_REGEX "^Usage: bettercad-cli <command>")
    bettercad_add_process_test(cli.launch.no-arguments
        COMMAND "$<TARGET_FILE:bettercad_cli>"
        EXIT_CODE 2 STDERR_REGEX "^Usage: bettercad-cli")
    bettercad_add_process_test(cli.launch.unknown-command
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "frobnicate"
        EXIT_CODE 2 STDERR_REGEX "unknown command 'frobnicate'")

    # P10 commands on the real executable, with the hand-written example.
    set(_example "${PROJECT_SOURCE_DIR}/examples/models/plate.bcad")
    set(_out "${CMAKE_CURRENT_BINARY_DIR}/cli-output")
    file(MAKE_DIRECTORY "${_out}")
    bettercad_add_process_test(cli.new
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "new|${_out}/new.bcad|--force|--name|Bracket"
        EXIT_CODE 0 STDOUT_REGEX "^Created .*new\\.bcad \\(document 'Bracket', ID [0-9a-f-]+\\)$"
        FILE "${_out}/new.bcad" FILE_REGEX "^{\n  \"format\": \"bettercad-document\"")
    bettercad_add_process_test(cli.new.unicode-path
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "new|${_out}/Plåt ✓.bcad|--force"
        EXIT_CODE 0 STDOUT_REGEX "\\(document 'Plåt ✓'"
        FILE "${_out}/Plåt ✓.bcad" FILE_REGEX "\"name\": \"Plåt ✓\"")
    bettercad_add_process_test(cli.info.example
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "info|${_example}"
        EXIT_CODE 0
        STDOUT_REGEX "^Document: Plate\n.*Parameters \\(4\\):\n  width        100 mm\n.*object:6  extrude  Extrude001  profile Sketch001, depth thickness, normal, new body$")
    bettercad_add_process_test(cli.validate.example
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "validate|${_example}"
        EXIT_CODE 0
        STDOUT_REGEX "geometry              ok, 1 result body\nResult bodies \\(1\\):\n  Extrude001 \\(object:6\\): 1 solid, volume 93716\\.815 mm\\^3.*\nResult: valid$")
    bettercad_add_process_test(cli.validate.missing-file
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "validate|${_out}/does-not-exist.bcad"
        EXIT_CODE 1 STDOUT_REGEX "Result: invalid \\(1 error\\)$")
    bettercad_add_process_test(cli.export-step.example
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "export-step|${_example}|${_out}/plate.step"
        EXIT_CODE 0 STDOUT_REGEX "^Wrote .*plate\\.step: STEP AP214 \\(mm\\), 1 body \\(Extrude001\\), [0-9]+ bytes$"
        FILE "${_out}/plate.step" FILE_REGEX "^ISO-10303-21;\nHEADER;")
    bettercad_add_process_test(cli.export-stl.example
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "export-stl|${_example}|${_out}/plate.stl"
        EXIT_CODE 0 STDOUT_REGEX "^Wrote .*plate\\.stl: binary STL \\(mm\\), 1 body \\(Extrude001: [0-9]+ triangles\\)"
        FILE "${_out}/plate.stl" FILE_REGEX "^BetterCAD [^ ]+ binary STL, units mm: Plate")
    bettercad_add_process_test(cli.export-stl.ascii
        COMMAND "$<TARGET_FILE:bettercad_cli>"
        ARGS "export-stl|${_example}|${_out}/plate-ascii.stl|--ascii|--tolerance|0.01mm"
        EXIT_CODE 0 STDOUT_REGEX "ASCII STL \\(mm\\).*tolerance 0\\.01 mm and 20 deg"
        FILE "${_out}/plate-ascii.stl" FILE_REGEX "^solid Plate\n  facet normal ")
    bettercad_add_process_test(cli.export-stl.bad-tolerance
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "export-stl|${_example}|${_out}/bad.stl|--tolerance|1kg"
        EXIT_CODE 2 STDERR_REGEX "--tolerance: '1kg' has dimension mass; expected length")

    # The mechanical reference models (P11-REF-001) through the real CLI:
    # every part loads, validates and exports. The volumes are the ones the
    # reference-model tests check against independent geometry.
    set(_reference "${PROJECT_SOURCE_DIR}/examples/models/reference")
    foreach(_model_volume
        "shaft|Shaft|EndChamfers|97773\\.099"
        "flange|Flange|RimFillet|81674\\.894"
        "pulley|Pulley|HubChamfers|212874\\.606"
        "bearing_housing|BearingHousing|BaseChamfers|261382\\.165"
        "mounting_bracket|MountingBracket|Gusset|123546\\.715"
        "u_bolt|UBolt|EndChamfers|15563\\.959"
        # P12-REF-001: the production reference models.
        "motor_mount|MotorMount|Pilot|64838\\.229"
        "gearbox_cover|GearboxCover|FixingHoles|86671\\.930"
        "manifold_tube|ManifoldTube|Flange|38840\\.706"
        "transition_duct|TransitionDuct|Flange|488642\\.810"
        "index_plate|IndexPlate|Assembly|262235\\.022"
        "ribbed_bracket|RibbedBracket|CornerRound|125252\\.693")
        string(REPLACE "|" ";" _parts "${_model_volume}")
        list(GET _parts 0 _stem)
        list(GET _parts 1 _name)
        list(GET _parts 2 _result)
        list(GET _parts 3 _volume)
        bettercad_add_process_test(cli.validate.reference.${_stem}
            COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "validate|${_reference}/${_stem}.bcad"
            EXIT_CODE 0
            STDOUT_REGEX "geometry              ok, 1 result body\nResult bodies \\(1\\):\n  ${_result} \\(object:[0-9]+\\): 1 solid, volume ${_volume} mm\\^3.*\nResult: valid$")
    endforeach()

    # The assembly reference models (P13-REFMOD-001) through the real CLI, as
    # a separate process: argv, exit codes and stdout as a script driver
    # actually sees them. The solve status, DOF and equation counts are the
    # ones AssemblyModelsTests.cpp derives by hand from the mate equation
    # table, so a change to the solver shows up here as well as in the suite.
    foreach(_assembly
        "assembly_grounded_pair|GroundedPair|FULLY_CONSTRAINED|0|0"
        "assembly_constrained_stack|ConstrainedStack|FULLY_CONSTRAINED|0|12"
        "assembly_shaft_in_bore|ShaftInBore|UNDER_CONSTRAINED|2|4"
        "assembly_joint_set|JointSet|UNDER_CONSTRAINED|7|17"
        "assembly_configured_frame|ConfiguredFrame|FULLY_CONSTRAINED|0|18"
        "assembly_driven_cover|DrivenCover|FULLY_CONSTRAINED|0|6"
        "assembly_machine|Machine|UNDER_CONSTRAINED|2|40")
        string(REPLACE "|" ";" _parts "${_assembly}")
        list(GET _parts 0 _stem)
        list(GET _parts 1 _name)
        list(GET _parts 2 _status)
        list(GET _parts 3 _dof)
        list(GET _parts 4 _equations)
        bettercad_add_process_test(cli.solve.reference.${_stem}
            COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "solve|${_reference}/${_stem}.bcad"
            EXIT_CODE 0
            STDOUT_REGEX "^Solving .*${_stem}\\.bcad\nSolve: ${_status}, ${_dof} degrees of freedom, ${_equations} equations")
        bettercad_add_process_test(cli.status.reference.${_stem}
            COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "status|${_reference}/${_stem}.bcad"
            EXIT_CODE 0
            STDOUT_REGEX "^Assembly of ${_name}\n.*Regeneration: ok.*\nSolve: ${_status}, ${_dof} degrees of freedom")
    endforeach()
    # The one that is committed broken. A script driver must be told, by the
    # exit code, that nothing usable came out -- and told which mates are at
    # fault, rather than a boolean.
    bettercad_add_process_test(cli.solve.reference.assembly_fault_cases
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "solve|${_reference}/assembly_fault_cases.bcad"
        EXIT_CODE 1
        STDOUT_REGEX "Solve: INCONSISTENT, 5 degrees of freedom, 2 equations.*\n.*cannot all be satisfied.*\n  conflicting  Near \\(object:[0-9]+\\)\n  conflicting  Far \\(object:[0-9]+\\)$")
    bettercad_add_process_test(cli.export-step.reference.assembly_machine
        COMMAND "$<TARGET_FILE:bettercad_cli>"
        ARGS "export-step|${_reference}/assembly_machine.bcad|${_out}/assembly_machine.step"
        EXIT_CODE 0
        STDOUT_REGEX "^Wrote .*assembly_machine\\.step: STEP AP214 \\(mm\\), assembly of 8 components \\(HousingBody, CoverPlate, ShaftFront, ShaftRear, Foot1, Foot2, Foot3, Foot4\\) from 4 bodies \\(Housing, Cover, Shaft, Foot\\), [0-9]+ bytes$"
        FILE "${_out}/assembly_machine.step" FILE_REGEX "^ISO-10303-21;\nHEADER;")
    # An assembly that cannot be solved has no file to write, and must leave
    # none behind.
    bettercad_add_process_test(cli.export-step.reference.assembly_fault_cases
        COMMAND "$<TARGET_FILE:bettercad_cli>"
        ARGS "export-step|${_reference}/assembly_fault_cases.bcad|${_out}/assembly_fault_cases.step"
        EXIT_CODE 1 STDERR_REGEX "solve")
    bettercad_add_process_test(cli.info.reference.shaft
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "info|${_reference}/shaft.bcad"
        EXIT_CODE 0
        STDOUT_REGEX "^Document: Shaft\n.*Parameters \\(10\\):\n  r1 +15 mm\n.*revolve  Turn .*mirror +TailCentreDrill.*chamfer  EndChamfers")
    bettercad_add_process_test(cli.export-step.reference.bracket
        COMMAND "$<TARGET_FILE:bettercad_cli>"
        ARGS "export-step|${_reference}/mounting_bracket.bcad|${_out}/mounting_bracket.step"
        EXIT_CODE 0 STDOUT_REGEX "^Wrote .*mounting_bracket\\.step: STEP AP214 \\(mm\\), 1 body \\(Gusset\\), [0-9]+ bytes$"
        FILE "${_out}/mounting_bracket.step" FILE_REGEX "^ISO-10303-21;\nHEADER;")
    bettercad_add_process_test(cli.export-step.reference.transition_duct
        COMMAND "$<TARGET_FILE:bettercad_cli>"
        ARGS "export-step|${_reference}/transition_duct.bcad|${_out}/transition_duct.step"
        EXIT_CODE 0 STDOUT_REGEX "^Wrote .*transition_duct\\.step: STEP AP214 \\(mm\\), 1 body \\(Flange\\), [0-9]+ bytes$"
        FILE "${_out}/transition_duct.step" FILE_REGEX "^ISO-10303-21;\nHEADER;")
    # P12-REF-001: the configuration-driven family through the real CLI. The
    # volumes are W^3-scaled and checked against closed forms in
    # MotorMountTests.cpp; here what matters is that the interface selects
    # them and that an unknown name is a structured failure.
    bettercad_add_process_test(cli.info.reference.motor_mount_configurations
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "info|${_reference}/motor_mount.bcad"
        EXIT_CODE 0
        STDOUT_REGEX "Configurations \\(3\\):\n  Small +1 override\n  Medium +1 override\n  Large +1 override")
    bettercad_add_process_test(cli.validate.reference.motor_mount_large
        COMMAND "$<TARGET_FILE:bettercad_cli>"
        ARGS "validate|${_reference}/motor_mount.bcad|--configuration|Large"
        EXIT_CODE 0
        STDOUT_REGEX "Pilot \\(object:[0-9]+\\): 1 solid, volume 199728\\.141 mm\\^3.*\nResult: valid$")
    bettercad_add_process_test(cli.validate.reference.motor_mount_small
        COMMAND "$<TARGET_FILE:bettercad_cli>"
        ARGS "validate|${_reference}/motor_mount.bcad|--configuration|Small"
        EXIT_CODE 0
        STDOUT_REGEX "Pilot \\(object:[0-9]+\\): 1 solid, volume 32003\\.185 mm\\^3.*\nResult: valid$")
    bettercad_add_process_test(cli.validate.reference.motor_mount_unknown_configuration
        COMMAND "$<TARGET_FILE:bettercad_cli>"
        ARGS "validate|${_reference}/motor_mount.bcad|--configuration|Enormous"
        EXIT_CODE 1 STDERR_REGEX "no configuration named 'Enormous' in this document")

    # P13-CLI-001: headless assembly workflows, through the real executable.
    #
    # These exist because the in-process cases check cli::run()'s return
    # value, and a milestone whose whole subject is exit codes and files on
    # disk should also be measured on a real process and a real file. Each
    # one asserts the PROCESS exit status; the atomicity pair asserts it on
    # the file as well, by asking the CLI afterwards what the document holds.
    set(_scripts "${PROJECT_SOURCE_DIR}/examples/scripts")
    set(_asm "${_out}/assembly")
    file(MAKE_DIRECTORY "${_asm}")

    # The committed example script builds a two-part assembly in one
    # transaction. It copies a pristine plate.bcad first, so that nothing in
    # the repository is edited AND so the test is repeatable: editing a
    # document in place is not idempotent, and `ctest --repeat until-fail:5`
    # would otherwise fail the second pass on a name the first pass took.
    bettercad_add_process_test(cli.assembly.batch
        COPY_FROM "${_example}" COPY_TO "${_asm}/built.bcad"
        COMMAND "$<TARGET_FILE:bettercad_cli>"
        ARGS "batch|${_asm}/built.bcad|${_scripts}/build_assembly.txt"
        EXIT_CODE 0
        STDOUT_REGEX "^Applied 6 edits from .*build_assembly\\.txt:\n +[0-9]+  Created Base \\(object:7\\) placing Extrude001 \\(object:6\\), at the origin\n +[0-9]+  Created Arm \\(object:8\\) placing Extrude001 \\(object:6\\), moved \\(0 mm, 0 mm, 50 mm\\), turned \\(0 deg, 0 deg, 0 deg\\)\n +[0-9]+  Created Ground \\(object:9\\): fixed, holding Base \\(object:7\\)\n +[0-9]+  Created Flush \\(object:10\\): coincident, relating Base:origin:xy to Arm:origin:xy\n +[0-9]+  Created configuration 'Loose'\n +[0-9]+  Suppressed Flush \\(object:10\\) in configuration 'Loose'\nWrote .*built\\.bcad$")

    # The assembly it built solves, and to the answer hand-derived in
    # AssemblyCliTests.cpp: a plane-to-plane coincidence removes one
    # translation and two rotations, leaving three.
    bettercad_add_process_test(cli.assembly.solve
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "solve|${_asm}/built.bcad"
        EXIT_CODE 0
        STDOUT_REGEX "Solve: UNDER_CONSTRAINED, 3 degrees of freedom, 3 equations, .*Transforms \\(2\\):\n  Base \\(object:7\\) +origin \\(0, 0, 0\\) mm, z \\(0, 0, 1\\)\n  Arm \\(object:8\\) +origin \\(0, 0, 0\\) mm, z \\(0, 0, 1\\)$")

    # And in the configuration that suppresses the mate, the constraint is
    # gone and Arm sits at the placement intent the solve never overwrote.
    bettercad_add_process_test(cli.assembly.solve.configuration
        COMMAND "$<TARGET_FILE:bettercad_cli>"
        ARGS "solve|${_asm}/built.bcad|--configuration|Loose"
        EXIT_CODE 0
        STDOUT_REGEX "Solve: UNDER_CONSTRAINED, 6 degrees of freedom, 0 equations, .*Arm \\(object:8\\) +origin \\(0, 0, 50\\) mm")

    bettercad_add_process_test(cli.assembly.status
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "status|${_asm}/built.bcad"
        EXIT_CODE 0
        STDOUT_REGEX "^Assembly of Plate\nConfiguration: none \\(base values\\)\nComponents \\(2\\):\n  Base \\(object:7\\) +places Extrude001 \\(object:6\\) +in force\n  Arm \\(object:8\\) +places Extrude001 \\(object:6\\) +in force\nMates \\(2\\):\n  Ground \\(object:9\\) +fixed Base \\(object:7\\) +in force\n  Flush \\(object:10\\) +coincident Base:origin:xy to Arm:origin:xy +in force\n")

    # The script that fails partway: exit 1, the line named, and nothing
    # written.
    bettercad_add_process_test(cli.assembly.batch.fails
        COPY_FROM "${_example}" COPY_TO "${_asm}/atomic.bcad"
        COMMAND "$<TARGET_FILE:bettercad_cli>"
        ARGS "batch|${_asm}/atomic.bcad|${_scripts}/broken_assembly.txt"
        EXIT_CODE 1
        STDERR_REGEX "broken_assembly\\.txt:10: this document has nothing named 'Nonexistent'\nNothing was written; .*atomic\\.bcad is unchanged\\.$")
    # Proved on the file rather than on the message: three of that script's
    # edits succeeded before the failure, and none of them reached the disk.
    bettercad_add_process_test(cli.assembly.batch.fails.wrote-nothing
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "status|${_asm}/atomic.bcad"
        EXIT_CODE 0
        STDOUT_REGEX "Components \\(0\\):\nMates \\(0\\):")

    # The two exit codes, on the rule they follow: 2 when the CLI's own
    # parser could not read the line, 1 when the document or the model
    # rejected what it asked for.
    bettercad_add_process_test(cli.assembly.exit.bad-unit
        COMMAND "$<TARGET_FILE:bettercad_cli>"
        ARGS "component-place|${_asm}/built.bcad|Arm|--z|1kg"
        EXIT_CODE 2 STDERR_REGEX "--z: '1kg' has dimension mass; expected length")
    bettercad_add_process_test(cli.assembly.exit.unknown-mate-type
        COMMAND "$<TARGET_FILE:bettercad_cli>"
        ARGS "mate-add|${_asm}/built.bcad|--type|wobble"
        EXIT_CODE 2 STDERR_REGEX "'wobble' is not a mate type")
    bettercad_add_process_test(cli.assembly.exit.bad-selector
        COMMAND "$<TARGET_FILE:bettercad_cli>"
        ARGS "component-place|${_asm}/built.bcad|7abc|--z|1mm"
        EXIT_CODE 2 STDERR_REGEX "neither an ID \\(digits\\) nor a name")
    bettercad_add_process_test(cli.assembly.exit.no-such-component
        COMMAND "$<TARGET_FILE:bettercad_cli>"
        ARGS "component-place|${_asm}/built.bcad|Ghost|--z|1mm"
        EXIT_CODE 1 STDERR_REGEX "this document has nothing named 'Ghost'")
    bettercad_add_process_test(cli.assembly.exit.still-mated
        COMMAND "$<TARGET_FILE:bettercad_cli>"
        ARGS "component-remove|${_asm}/built.bcad|Base"
        EXIT_CODE 1 STDERR_REGEX "is still mated by Ground \\(object:9\\), Flush \\(object:10\\)")
    bettercad_add_process_test(cli.assembly.exit.missing-file
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "solve|${_asm}/not-here.bcad"
        EXIT_CODE 1)

    # Ordering.
    #
    # The two commands that PRODUCE a document other tests read -- the batch
    # that builds one and the batch that fails against one -- are the fixture,
    # not merely ordered before their readers. ctest pulls a required
    # fixture's setup in even when the run is filtered (ctest -R), whereas a
    # DEPENDS on an excluded test is simply dropped. With DEPENDS alone,
    # `ctest -R cli.assembly.solve` would solve a document nothing ever built.
    #
    # Each of them copies its own pristine input (COPY_FROM), so they are
    # idempotent and the whole set survives `ctest --repeat`. Everything that
    # reads what they made is either read-only or an edit that is refused, so
    # none of the readers changes anything either.
    #
    # They are still ordinary tests: their own exit codes and output are
    # asserted and reported exactly as the others are.
    set_tests_properties(
        cli.assembly.batch cli.assembly.batch.fails
        PROPERTIES FIXTURES_SETUP assembly_cli)
    set_tests_properties(
        cli.assembly.solve cli.assembly.solve.configuration cli.assembly.status
        cli.assembly.batch.fails.wrote-nothing
        cli.assembly.exit.bad-unit cli.assembly.exit.unknown-mate-type
        cli.assembly.exit.bad-selector cli.assembly.exit.no-such-component
        cli.assembly.exit.still-mated
        PROPERTIES FIXTURES_REQUIRED assembly_cli)
    # Among the readers: the ones that assert built.bcad's contents run before
    # anything else touches it. None of the exit-code cases can change it --
    # each is refused -- so they need no order among themselves.
    # --- P14-CLI-001: a drawing built and reported by the real executable ---
    #
    # In-process tests (DrawingCliTests.cpp) drive cli::run directly. These
    # drive the PROCESS, which is what proves the workflow survives leaving
    # one: the batch writes a file and a separate invocation reads it, with
    # nothing shared but the bytes on disk.
    set(_drw "${_out}/drawing")
    file(MAKE_DIRECTORY "${_drw}")

    # The committed workflow script, applied as one transaction to a pristine
    # copy (so the test is repeatable under --repeat until-fail).
    bettercad_add_process_test(cli.drawing.batch
        COPY_FROM "${_example}" COPY_TO "${_drw}/drawn.bcad"
        COMMAND "$<TARGET_FILE:bettercad_cli>"
        ARGS "batch|${_drw}/drawn.bcad|${_scripts}/build_drawing.txt"
        EXIT_CODE 0
        STDOUT_REGEX "^Applied 5 edits from .*build_drawing\\.txt:\n +[0-9]+  Created Sheet1 \\(object:7\\) \\(A3 landscape, scale 1:1\\)\n +[0-9]+  Created Front \\(object:8\\) on Sheet1 \\(object:7\\)\n +[0-9]+  Created Top \\(object:9\\) on Sheet1 \\(object:7\\)\n +[0-9]+  Created Thickness \\(object:10\\) on Front \\(object:8\\)\n +[0-9]+  Created GeneralNote \\(object:11\\) \\(note\\) on Front \\(object:8\\)\nWrote .*drawn\\.bcad$")

    # A SEPARATE process reads what that one wrote. The plate is 20 mm thick
    # (its `thickness` parameter), so the dimension between the extrude's two
    # caps reads 20.00 -- a number this command derives from the model on
    # demand and never reads out of the file.
    bettercad_add_process_test(cli.drawing.report
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "drawing|${_drw}/drawn.bcad"
        EXIT_CODE 0
        STDOUT_REGEX "^Drawing of Plate\nConfiguration: none \\(base values\\)\nSheets \\(1\\):\n  Sheet1 \\(object:7\\) +A3 landscape, scale 1:1\nViews \\(2\\):\n  Front \\(object:8\\) +base +on Sheet1 \\(object:7\\) +scale 1:1 +resolved\n  Top \\(object:9\\) +projected on Sheet1 \\(object:7\\) +scale 1:1 +resolved\nDimensions \\(1\\):\n  Thickness \\(object:10\\) +linear +on Front \\(object:8\\) +20\\.00\nAnnotations \\(1\\):\n  GeneralNote \\(object:11\\) +note +on Front \\(object:8\\) +resolved\nRegeneration: ok, 7 objects regenerated")

    # The script that fails partway: exit 1, the line named, nothing written.
    bettercad_add_process_test(cli.drawing.batch.fails
        COPY_FROM "${_example}" COPY_TO "${_drw}/atomic.bcad"
        COMMAND "$<TARGET_FILE:bettercad_cli>"
        ARGS "batch|${_drw}/atomic.bcad|${_scripts}/broken_drawing.txt"
        EXIT_CODE 1
        STDERR_REGEX "broken_drawing\\.txt:16: this document has nothing named 'Elevation'\nNothing was written; .*atomic\\.bcad is unchanged\\.$")
    # Proved on the FILE: three of that script's edits succeeded before the
    # failure, and none of them reached the disk.
    bettercad_add_process_test(cli.drawing.batch.fails.wrote-nothing
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "drawing|${_drw}/atomic.bcad"
        EXIT_CODE 0
        STDOUT_REGEX "Sheets \\(0\\):\nViews \\(0\\):\nDimensions \\(0\\):\nAnnotations \\(0\\):")

    # The two exit codes on the drawing verbs, following the same rule as the
    # rest of the CLI: 2 when the parser could not read the line, 1 when the
    # model rejected what it asked for.
    bettercad_add_process_test(cli.drawing.exit.bad-target
        COMMAND "$<TARGET_FILE:bettercad_cli>"
        ARGS "dimension-add|${_drw}/drawn.bcad|--view|Front|--type|linear|--from|somewhere"
        EXIT_CODE 2 STDERR_REGEX "not a drawing geometry kind")
    bettercad_add_process_test(cli.drawing.exit.bad-scale
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "sheet-add|${_drw}/drawn.bcad|--scale|1:0"
        EXIT_CODE 2 STDERR_REGEX "--scale")
    bettercad_add_process_test(cli.drawing.exit.no-such-view
        COMMAND "$<TARGET_FILE:bettercad_cli>"
        ARGS "view-move|${_drw}/drawn.bcad|Elevation|--x|1mm|--y|1mm"
        EXIT_CODE 1 STDERR_REGEX "nothing named 'Elevation'")
    bettercad_add_process_test(cli.drawing.exit.wrong-kind
        COMMAND "$<TARGET_FILE:bettercad_cli>"
        ARGS "view-move|${_drw}/drawn.bcad|Sheet1|--x|1mm|--y|1mm"
        EXIT_CODE 1 STDERR_REGEX "expected a view")
    # A projected view derives its place from its parent and stores none, so
    # moving one is refused by the qualified validator rather than by a rule
    # the CLI invented.
    bettercad_add_process_test(cli.drawing.exit.projected-view-has-no-placement
        COMMAND "$<TARGET_FILE:bettercad_cli>"
        ARGS "view-move|${_drw}/drawn.bcad|Top|--x|1mm|--y|1mm"
        EXIT_CODE 1 STDERR_REGEX "derived from its parent")
    # --- P14-EXPORT-001: the three writers, on the real executable ---
    #
    # The drawing built above, written out and checked at its first bytes and
    # its physical size. A file that opens is not a pass; these assert what is
    # IN it.
    bettercad_add_process_test(cli.drawing.export.svg
        COMMAND "$<TARGET_FILE:bettercad_cli>"
        ARGS "export-svg|${_drw}/drawn.bcad|${_drw}/drawn.svg"
        EXIT_CODE 0
        STDOUT_REGEX "^Wrote .*drawn\\.svg: SVG of Sheet1 \\(object:7\\), 420 x 297 mm, [0-9]+ items$"
        FILE "${_drw}/drawn.svg"
        FILE_REGEX "width=\"420\\.0000mm\" height=\"297\\.0000mm\" viewBox=\"0 0 420\\.0000 297\\.0000\"")
    bettercad_add_process_test(cli.drawing.export.dxf
        COMMAND "$<TARGET_FILE:bettercad_cli>"
        ARGS "export-dxf|${_drw}/drawn.bcad|${_drw}/drawn.dxf"
        EXIT_CODE 0
        STDOUT_REGEX "^Wrote .*drawn\\.dxf: DXF \\(R12\\) of Sheet1 \\(object:7\\), 420 x 297 mm, [0-9]+ items$"
        FILE "${_drw}/drawn.dxf" FILE_REGEX "\\$INSUNITS\n70\n4")
    bettercad_add_process_test(cli.drawing.export.pdf
        COMMAND "$<TARGET_FILE:bettercad_cli>"
        ARGS "export-pdf|${_drw}/drawn.bcad|${_drw}/drawn.pdf"
        EXIT_CODE 0
        STDOUT_REGEX "^Wrote .*drawn\\.pdf: PDF of Sheet1 \\(object:7\\), 420 x 297 mm, [0-9]+ items$"
        FILE "${_drw}/drawn.pdf" FILE_REGEX "^%PDF-1\\.4")

    # No false success: a path that cannot be written, and a sheet that is not
    # there, each exit non-zero.
    bettercad_add_process_test(cli.drawing.export.bad-path
        COMMAND "$<TARGET_FILE:bettercad_cli>"
        ARGS "export-svg|${_drw}/drawn.bcad|${_drw}/no/such/place/out.svg"
        EXIT_CODE 1 STDERR_REGEX "export-svg")
    bettercad_add_process_test(cli.drawing.export.no-such-sheet
        COMMAND "$<TARGET_FILE:bettercad_cli>"
        ARGS "export-svg|${_drw}/drawn.bcad|${_drw}/out.svg|--sheet|Elevation"
        EXIT_CODE 1 STDERR_REGEX "nothing named 'Elevation'")

    set_tests_properties(cli.drawing.batch cli.drawing.batch.fails
        PROPERTIES FIXTURES_SETUP drawing_cli)
    set_tests_properties(
        cli.drawing.report cli.drawing.batch.fails.wrote-nothing
        cli.drawing.exit.bad-target cli.drawing.exit.bad-scale
        cli.drawing.exit.no-such-view cli.drawing.exit.wrong-kind
        cli.drawing.exit.projected-view-has-no-placement
        cli.drawing.export.svg cli.drawing.export.dxf cli.drawing.export.pdf
        cli.drawing.export.bad-path cli.drawing.export.no-such-sheet
        PROPERTIES FIXTURES_REQUIRED drawing_cli)
    # The reader that asserts drawn.bcad's contents runs before anything else
    # touches it. Every exit-code case is REFUSED and so changes nothing, which
    # is why they need no order among themselves.
    set_tests_properties(
        cli.drawing.exit.bad-target cli.drawing.exit.bad-scale
        cli.drawing.exit.no-such-view cli.drawing.exit.wrong-kind
        cli.drawing.exit.projected-view-has-no-placement
        cli.drawing.export.svg cli.drawing.export.dxf cli.drawing.export.pdf
        cli.drawing.export.bad-path cli.drawing.export.no-such-sheet
        PROPERTIES DEPENDS cli.drawing.report)

    # --- P14-REFMOD-001: the production drawing suite, through the real
    # executables ---
    #
    # TWO PROCESSES, and nothing shared between them but the bytes on disk.
    # The example program builds every drawing reference model, saves it and
    # exports it; separate bettercad-cli invocations then LOAD what it wrote,
    # regenerate from the intent in the file and report what they find. An
    # in-process test cannot prove that, because it never leaves the process
    # that built the model.
    #
    # `--drawings` runs the drawing suite alone. The full example program also
    # builds twelve parts and eight assemblies with their STEP and STL
    # exports, which is a minute of work this fixture has no use for.
    if(TARGET bettercad_example_reference_models)
        set(_ref "${_out}/refmod")
        file(MAKE_DIRECTORY "${_ref}")

        # PROCESS A: create, regenerate, save, export. Its own exit code is
        # the first assertion -- the program fails if any model does not
        # build, does not regenerate, draws off the page or cannot be written.
        bettercad_add_process_test(cli.refmod.build
            COMMAND "$<TARGET_FILE:bettercad_example_reference_models>"
            ARGS "--drawings|--out|${_ref}"
            EXIT_CODE 0
            STDOUT_REGEX "== DrawnStepPlate \\(RM-DWG-01\\).*== DrawnGuardedFrame \\(RM-DWG-08\\)"
            FILE "${_ref}/drawing_step_plate.bcad"
            FILE_REGEX "\"name\": \"DrawnStepPlate\"")

        # PROCESS B, from here down. Every one of these opens a file it did
        # not write.
        bettercad_add_process_test(cli.refmod.validate
            COMMAND "$<TARGET_FILE:bettercad_cli>"
            ARGS "validate|${_ref}/drawing_tolerance_block.bcad"
            EXIT_CODE 0
            STDOUT_REGEX "missing references +ok.*dependency cycles +ok.*feature regeneration +ok")

        # The hole pattern, reported by name, with every kind of dimension it
        # carries: the plate's 120 and 80 along the view's own axes, an
        # ORDINATE that is negative because its datum is the far edge, a
        # diameter on the source bore and on a patterned copy of it, the same
        # cylinder as a radius reading exactly half, and four centre marks on
        # four identical bores.
        #
        # The negative ordinate is the one worth having here: a horizontal or
        # a vertical dimension reports a magnitude and could not produce it.
        bettercad_add_process_test(cli.refmod.drawing.pattern
            COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "drawing|${_ref}/drawing_hole_plate.bcad"
            EXIT_CODE 0
            STDOUT_REGEX "^Drawing of DrawnHolePlate.*Length \\(object:[0-9]+\\) +horizontal +on Top \\(object:[0-9]+\\) +120\\.00.*Width \\(object:[0-9]+\\) +vertical +on Top \\(object:[0-9]+\\) +80\\.00.*AcrossFromLeft \\(object:[0-9]+\\) +ordinate +on Top \\(object:[0-9]+\\) +120\\.00.*DownFromTop \\(object:[0-9]+\\) +ordinate +on Top \\(object:[0-9]+\\) +-80\\.00.*BoreDiameter \\(object:[0-9]+\\) +diameter +on Top \\(object:[0-9]+\\) +10\\.00.*FarBoreDiameter \\(object:[0-9]+\\) +diameter +on Top \\(object:[0-9]+\\) +10\\.00.*BoreRadius \\(object:[0-9]+\\) +radius +on Top \\(object:[0-9]+\\) +5\\.00.*BoreMark0 .*BoreMark3 .*Regeneration: ok")

        # Two sheets, two formats, two orientations -- and the auxiliary view
        # that no standard direction gives, on the second of them.
        bettercad_add_process_test(cli.refmod.drawing.sheets
            COMMAND "$<TARGET_FILE:bettercad_cli>"
            ARGS "drawing|${_ref}/drawing_angle_bracket.bcad"
            EXIT_CODE 0
            STDOUT_REGEX "Sheets \\(2\\):\n +Sheet1 \\(object:[0-9]+\\) +A3 landscape, scale 1:1\n +Sheet2 \\(object:[0-9]+\\) +A4 portrait, scale 1:2\nViews \\(5\\):.*SlantAuxiliary \\(object:[0-9]+\\) +auxiliary on Sheet2 \\(object:[0-9]+\\).*AcrossSlant \\(object:[0-9]+\\) +aligned +on SlantAuxiliary \\(object:[0-9]+\\) +50\\.00.*Regeneration: ok")
        # Each sheet is written out as a page of its own size, by name.
        bettercad_add_process_test(cli.refmod.export.second-sheet
            COMMAND "$<TARGET_FILE:bettercad_cli>"
            ARGS "export-svg|${_ref}/drawing_angle_bracket.bcad|${_ref}/bracket_sheet2.svg|--sheet|Sheet2"
            EXIT_CODE 0
            STDOUT_REGEX "SVG of Sheet2 \\(object:[0-9]+\\), 210 x 297 mm, [0-9]+ items$"
            FILE "${_ref}/bracket_sheet2.svg"
            FILE_REGEX "width=\"210\\.0000mm\" height=\"297\\.0000mm\"")

        # Tolerances and GD&T as the drawing writes them: a deviation pair, a
        # limit, and a fit designation that is resolved rather than stored.
        #
        # The plus/minus sign is NOT matched literally. It is UTF-8 in the
        # CLI's output and what a CMake regex sees depends on the console code
        # page; cli.new.unicode-path tests that on purpose and this case has
        # no business also depending on it. The nominal and its deviation are
        # still both asserted, on one line, which is the thing under test.
        bettercad_add_process_test(cli.refmod.drawing.tolerances
            COMMAND "$<TARGET_FILE:bettercad_cli>"
            ARGS "drawing|${_ref}/drawing_tolerance_block.bcad"
            EXIT_CODE 0
            STDOUT_REGEX "Length \\(object:[0-9]+\\) +linear +on Front \\(object:[0-9]+\\) +80\\.00 [^\n]*0\\.10.*Width \\(object:[0-9]+\\) +linear +on Top \\(object:[0-9]+\\) +50\\.15.*BoreSize \\(object:[0-9]+\\) +diameter +on Top \\(object:[0-9]+\\) +20\\.000 H7.*feature_control_frame.*surface_finish.*Regeneration: ok")

        # The bill of materials, computed from the assembly by the process
        # that read the file: three rows, quantities 1, 4 and 2, seven
        # occurrences. None of those numbers is in the file.
        bettercad_add_process_test(cli.refmod.drawing.bom
            COMMAND "$<TARGET_FILE:bettercad_cli>"
            ARGS "drawing|${_ref}/drawing_bolted_stack.bcad"
            EXIT_CODE 0
            STDOUT_REGEX "Bill of materials for Front \\(object:[0-9]+\\) \\(7 occurrences\\):\n +1 +Bracket +x1\n +2 +Bolt +x4\n +3 +Spacer +x2\nRegeneration: ok")

        # The same drawing in its other configuration. Two components fewer,
        # the bolt renumbered from item 3 to item 2, and the guard's balloon
        # UNRESOLVED -- reported by name, with a non-zero exit, rather than
        # quietly labelling something else.
        bettercad_add_process_test(cli.refmod.drawing.configuration
            COMMAND "$<TARGET_FILE:bettercad_cli>"
            ARGS "drawing|${_ref}/drawing_guarded_frame.bcad|--configuration|Open"
            EXIT_CODE 1
            STDOUT_REGEX "Configuration: Open.*GuardBalloon \\(object:[0-9]+\\) +balloon +on Front \\(object:[0-9]+\\) +unresolved.*Bill of materials for Front \\(object:[0-9]+\\) \\(3 occurrences\\):\n +1 +Frame +x1\n +2 +Bolt +x2\nRegeneration: FAILED")
        # And in its committed state it regenerates, with all three rows.
        bettercad_add_process_test(cli.refmod.drawing.configuration.base
            COMMAND "$<TARGET_FILE:bettercad_cli>"
            ARGS "drawing|${_ref}/drawing_guarded_frame.bcad"
            EXIT_CODE 0
            STDOUT_REGEX "Configuration: none \\(base values\\).*GuardBalloon \\(object:[0-9]+\\) +balloon +on Front \\(object:[0-9]+\\) +resolved.*\\(6 occurrences\\):\n +1 +Frame +x1\n +2 +Guard +x1\n +3 +Bolt +x4\nRegeneration: ok")

        # An assembly drawing costs a solve, and this one is fully
        # constrained: five occurrences, no freedom left.
        bettercad_add_process_test(cli.refmod.solve
            COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "solve|${_ref}/drawing_clamp_set.bcad"
            EXIT_CODE 0
            STDOUT_REGEX "Solve: FULLY_CONSTRAINED, 0 degrees of freedom.*Transforms \\(5\\):")

        # A model change applied by one process and read by the next: the
        # regenerate command rebuilds from the file alone.
        bettercad_add_process_test(cli.refmod.regenerate
            COMMAND "$<TARGET_FILE:bettercad_cli>"
            ARGS "regenerate|${_ref}/drawing_pocket_block.bcad"
            EXIT_CODE 0 STDOUT_REGEX "regenerated")

        # --- The three writers, driven over a reference model by a process
        # that loaded it from disk. Each file is checked at its own first
        # bytes and at something only that format has.
        bettercad_add_process_test(cli.refmod.export.svg
            COMMAND "$<TARGET_FILE:bettercad_cli>"
            ARGS "export-svg|${_ref}/drawing_hole_plate.bcad|${_ref}/hole_plate.svg"
            EXIT_CODE 0
            STDOUT_REGEX "SVG of Sheet1 \\(object:[0-9]+\\), 420 x 297 mm, [0-9]+ items$"
            FILE "${_ref}/hole_plate.svg"
            FILE_REGEX "width=\"420\\.0000mm\" height=\"297\\.0000mm\"")
        bettercad_add_process_test(cli.refmod.export.dxf
            COMMAND "$<TARGET_FILE:bettercad_cli>"
            ARGS "export-dxf|${_ref}/drawing_hole_plate.bcad|${_ref}/hole_plate.dxf"
            EXIT_CODE 0
            STDOUT_REGEX "DXF \\(R12\\) of Sheet1 \\(object:[0-9]+\\), 420 x 297 mm, [0-9]+ items$"
            FILE "${_ref}/hole_plate.dxf" FILE_REGEX "\\$INSUNITS\n70\n4")
        bettercad_add_process_test(cli.refmod.export.pdf
            COMMAND "$<TARGET_FILE:bettercad_cli>"
            ARGS "export-pdf|${_ref}/drawing_tolerance_block.bcad|${_ref}/tolerance_block.pdf"
            EXIT_CODE 0
            STDOUT_REGEX "PDF of Sheet1 \\(object:[0-9]+\\), 420 x 297 mm, [0-9]+ items$"
            FILE "${_ref}/tolerance_block.pdf" FILE_REGEX "^%PDF-1\\.4")

        # The model that draws a section and a detail, exported whole.
        bettercad_add_process_test(cli.refmod.export.section
            COMMAND "$<TARGET_FILE:bettercad_cli>"
            ARGS "export-svg|${_ref}/drawing_pocket_block.bcad|${_ref}/pocket_block.svg"
            EXIT_CODE 0 STDOUT_REGEX "SVG of Sheet1"
            FILE "${_ref}/pocket_block.svg" FILE_REGEX "<svg xmlns=")

        # The build is a FIXTURE and not merely ordered first: ctest pulls a
        # required fixture's setup in even when the run is filtered, whereas a
        # DEPENDS on an excluded test is dropped. Without it,
        # `ctest -R cli.refmod.drawing` would report drawings nothing built.
        # It rewrites its whole output directory, so it is idempotent under
        # --repeat, and every reader is read-only.
        set_tests_properties(cli.refmod.build PROPERTIES FIXTURES_SETUP refmod_cli)
        set_tests_properties(
            cli.refmod.validate cli.refmod.drawing.pattern cli.refmod.drawing.tolerances
            cli.refmod.drawing.sheets cli.refmod.drawing.bom cli.refmod.drawing.configuration
            cli.refmod.drawing.configuration.base cli.refmod.solve cli.refmod.regenerate
            cli.refmod.export.svg cli.refmod.export.dxf cli.refmod.export.pdf
            cli.refmod.export.section cli.refmod.export.second-sheet
            PROPERTIES FIXTURES_REQUIRED refmod_cli)
    endif()

    set_tests_properties(cli.assembly.solve.configuration PROPERTIES DEPENDS cli.assembly.solve)
    set_tests_properties(cli.assembly.status PROPERTIES DEPENDS cli.assembly.solve.configuration)
    set_tests_properties(
        cli.assembly.exit.bad-unit cli.assembly.exit.unknown-mate-type
        cli.assembly.exit.bad-selector cli.assembly.exit.no-such-component
        cli.assembly.exit.still-mated
        PROPERTIES DEPENDS cli.assembly.status)

    # P12-PARAM-001: a model driven by parameter expressions, through the
    # real executable.
    set(_driven "${PROJECT_SOURCE_DIR}/examples/models/driven_plate.bcad")
    bettercad_add_process_test(cli.info.driven-plate
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "info|${_driven}"
        EXIT_CODE 0
        STDOUT_REGEX "^Document: DrivenPlate\n.*Parameters \\(7\\):\n  hole_y         25 mm  \\(expression: height / 2\\)\n  height         50 mm  \\(expression: width / 2\\)\n.*  width          100 mm\n")
    bettercad_add_process_test(cli.validate.driven-plate
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "validate|${_driven}"
        EXIT_CODE 0
        STDOUT_REGEX "feature regeneration  ok, 2 objects regenerated\n  geometry              ok, 1 result body\nResult bodies \\(1\\):\n  Plate \\(object:9\\): 1 solid, volume 48429\\.204 mm\\^3.*bounds \\(0, 0, 0\\) to \\(100, 50, 10\\) mm\nResult: valid$")
    bettercad_add_process_test(cli.export-step.driven-plate
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "export-step|${_driven}|${_out}/driven_plate.step"
        EXIT_CODE 0 STDOUT_REGEX "^Wrote .*driven_plate\\.step: STEP AP214 \\(mm\\), 1 body \\(Plate\\), [0-9]+ bytes$"
        FILE "${_out}/driven_plate.step" FILE_REGEX "^ISO-10303-21;\nHEADER;")
    # P12-DATUM-001: reference geometry, through the real executable.
    set(_datum "${PROJECT_SOURCE_DIR}/examples/models/datum_block.bcad")
    bettercad_add_process_test(cli.info.datum-block
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "info|${_datum}"
        EXIT_CODE 0
        STDOUT_REGEX "^Document: DatumBlock\n.*  object:7   datum_plane        TopPlane     offset from the model's xy by height\n.*  object:12  datum_axis         Spindle      where Middle and RearPlane meet\n.*  object:14  coordinate_system  Station      from the model's: moved \\(150 mm, 0 mm, 0 mm\\), turned \\(0 deg, 0 deg, 90 deg\\)\n")
    bettercad_add_process_test(cli.validate.datum-block
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "validate|${_datum}"
        EXIT_CODE 0
        STDOUT_REGEX "feature regeneration  ok, 15 objects regenerated\n  geometry              ok, 3 result bodies\nResult bodies \\(3\\):\n  Pattern \\(object:13\\): 1 solid, volume 126031\\.858 mm\\^3.*Fin \\(object:19\\): 1 solid, volume 2000\\.000 mm\\^3.*bounds \\(-51\\.962, 0, 20\\) to \\(-32\\.141, 20, 34\\.33\\) mm\nResult: valid$")
    bettercad_add_process_test(cli.export-step.datum-block
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "export-step|${_datum}|${_out}/datum_block.step"
        EXIT_CODE 0 STDOUT_REGEX "^Wrote .*datum_block\\.step: STEP AP214 \\(mm\\), 3 bodies \\(Pattern, Peg, Fin\\), [0-9]+ bytes$"
        FILE "${_out}/datum_block.step" FILE_REGEX "^ISO-10303-21;\nHEADER;")
    # P12-STREF-001: sketches on faces of features, through the real executable.
    set(_faces "${PROJECT_SOURCE_DIR}/examples/models/face_block.bcad")
    bettercad_add_process_test(cli.info.face-block
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "info|${_faces}"
        EXIT_CODE 0
        STDOUT_REGEX "^Document: FaceBlock\n.*  object:7   sketch   BossSketch  2 entities, 2 constraints, on the end cap of Base\n.*  object:9   sketch   SideSketch  8 entities, 4 constraints, on the side from entity:5 of Base\n")
    bettercad_add_process_test(cli.validate.face-block
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "validate|${_faces}"
        EXIT_CODE 0
        STDOUT_REGEX "feature regeneration  ok, 8 objects regenerated\n  geometry              ok, 1 result body\nResult bodies \\(1\\):\n  Pocket \\(object:10\\): 1 solid, volume 226010\\.619 mm\\^3.*bounds \\(0, 0, 0\\) to \\(150, 60, 35\\) mm\nResult: valid$")
    bettercad_add_process_test(cli.export-step.face-block
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "export-step|${_faces}|${_out}/face_block.step"
        EXIT_CODE 0 STDOUT_REGEX "^Wrote .*face_block\\.step: STEP AP214 \\(mm\\), 1 body \\(Pocket\\), [0-9]+ bytes$"
        FILE "${_out}/face_block.step" FILE_REGEX "^ISO-10303-21;\nHEADER;")
    # P12-SKETCH-003: sketches on the copies a pattern and a mirror make.
    set(_rows "${PROJECT_SOURCE_DIR}/examples/models/post_row.bcad")
    bettercad_add_process_test(cli.info.post-row
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "info|${_rows}"
        EXIT_CODE 0
        STDOUT_REGEX "^Document: PostRow\n.*  object:9   sketch          CopySketch   2 entities, 2 constraints, on the side from entity:6 of Post, copy 2 of Row\n.*  object:11  sketch          ImageSketch  2 entities, 2 constraints, on the side from entity:6 of Post, copy 2 of Row, copy 1 of Flip\n.*  object:13  datum_plane     Gauge        offset from the side from entity:6 of Post, copy 1 of Row by 5 mm\n")
    bettercad_add_process_test(cli.validate.post-row
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "validate|${_rows}"
        EXIT_CODE 0
        STDOUT_REGEX "feature regeneration  ok, 13 objects regenerated\n  geometry              ok, 4 result bodies\nResult bodies \\(4\\):\n  Flip \\(object:8\\): 1 solid, volume 72000\\.000 mm\\^3.*bounds \\(0, 0, 0\\) to \\(200, 30, 30\\) mm\n  CopyBoss \\(object:10\\): 1 solid, volume 113\\.097 mm\\^3.*bounds \\(80, 12, 17\\) to \\(84, 18, 23\\) mm\n  ImageBoss \\(object:12\\): 1 solid, volume 113\\.097 mm\\^3.*bounds \\(116, 12, 17\\) to \\(120, 18, 23\\) mm\n  GaugeBoss \\(object:15\\): 1 solid, volume 37\\.699 mm\\^3.*bounds \\(55, 13, 18\\) to \\(58, 17, 22\\) mm\nResult: valid$")
    bettercad_add_process_test(cli.export-step.post-row
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "export-step|${_rows}|${_out}/post_row.step"
        EXIT_CODE 0 STDOUT_REGEX "^Wrote .*post_row\\.step: STEP AP214 \\(mm\\), 4 bodies \\(Flip, CopyBoss, ImageBoss, GaugeBoss\\), [0-9]+ bytes$"
        FILE "${_out}/post_row.step" FILE_REGEX "^ISO-10303-21;\nHEADER;")
    # P12-FEAT-001: cuts through all of their target.
    set(_slab "${PROJECT_SOURCE_DIR}/examples/models/through_slab.bcad")
    bettercad_add_process_test(cli.info.through-slab
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "info|${_slab}"
        EXIT_CODE 0
        STDOUT_REGEX "^Document: ThroughSlab\n.*  object:5  extrude  Cut          profile CutSketch, through all, reversed, cut Slab\n.*  object:7  extrude  Slant        profile SlantSketch, through all, symmetric, cut Cut\n")
    bettercad_add_process_test(cli.validate.through-slab
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "validate|${_slab}"
        EXIT_CODE 0
        STDOUT_REGEX "feature regeneration  ok, 8 objects regenerated\n  geometry              ok, 2 result bodies\nResult bodies \\(2\\):\n  Slant \\(object:7\\): 1 solid, volume 102164\\.962 mm\\^3.*bounds \\(0, 0, 0\\) to \\(100, 60, 20\\) mm\n  Peg \\(object:9\\): 1 solid, volume 141\\.372 mm\\^3.*bounds \\(60, 27, 7\\) to \\(65, 33, 13\\) mm\nResult: valid$")
    bettercad_add_process_test(cli.export-step.through-slab
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "export-step|${_slab}|${_out}/through_slab.step"
        EXIT_CODE 0 STDOUT_REGEX "^Wrote .*through_slab\\.step: STEP AP214 \\(mm\\), 2 bodies \\(Slant, Peg\\), [0-9]+ bytes$"
        FILE "${_out}/through_slab.step" FILE_REGEX "^ISO-10303-21;\nHEADER;")
    # P12-FEAT-002: bodies combined, then split.
    set(_ops "${PROJECT_SOURCE_DIR}/examples/models/body_ops.bcad")
    bettercad_add_process_test(cli.info.body-ops
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "info|${_ops}"
        EXIT_CODE 0
        STDOUT_REGEX "^Document: BodyOps\n.*  object:9   combine      Joined   join A with B, C\n.*  object:11  split        Halves   target Joined, plane Middle, keep both$")
    bettercad_add_process_test(cli.validate.body-ops
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "validate|${_ops}"
        EXIT_CODE 0
        STDOUT_REGEX "feature regeneration  ok, 9 objects regenerated\n  geometry              ok, 1 result body\nResult bodies \\(1\\):\n  Halves \\(object:11\\): 2 solids, volume 171141\\.593 mm\\^3.*bounds \\(0, 0, 0\\) to \\(150, 60, 40\\) mm\nResult: valid$")
    bettercad_add_process_test(cli.export-step.body-ops
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "export-step|${_ops}|${_out}/body_ops.step"
        EXIT_CODE 0 STDOUT_REGEX "^Wrote .*body_ops\\.step: STEP AP214 \\(mm\\), 1 body \\(Halves\\), [0-9]+ bytes$"
        FILE "${_out}/body_ops.step" FILE_REGEX "^ISO-10303-21;\nHEADER;")
    # P12-FEAT-003: a block shelled inward (open at the top, and at both
    # ends) and outward.
    set(_shells "${PROJECT_SOURCE_DIR}/examples/models/shelled_block.bcad")
    bettercad_add_process_test(cli.info.shelled-block
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "info|${_shells}"
        EXIT_CODE 0
        STDOUT_REGEX "^Document: ShelledBlock\n.*  object:5  shell    Cup          target Block, open the end cap of Block, thickness wall, inward\n  object:6  shell    Tube         target Block, open the end cap of Block and the start cap of Block, thickness wall, inward\n  object:7  shell    Casing       target Block, open the end cap of Block, thickness wall, outward$")
    bettercad_add_process_test(cli.validate.shelled-block
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "validate|${_shells}"
        EXIT_CODE 0
        STDOUT_REGEX "feature regeneration  ok, 5 objects regenerated\n  geometry              ok, 3 result bodies\nResult bodies \\(3\\):\n  Cup \\(object:5\\): 1 solid, volume 82500\\.000 mm\\^3, area 34600\\.000 mm\\^2, bounds \\(0, 0, 0\\) to \\(100, 60, 40\\) mm\n  Tube \\(object:6\\): 1 solid, volume 60000\\.000 mm\\^3, area 27000\\.000 mm\\^2, bounds \\(0, 0, 0\\) to \\(100, 60, 40\\) mm\n  Casing \\(object:7\\): 1 solid, volume 106500\\.000 mm\\^3, area 44400\\.000 mm\\^2, bounds \\(-5, -5, -5\\) to \\(105, 65, 40\\) mm\nResult: valid$")
    bettercad_add_process_test(cli.export-step.shelled-block
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "export-step|${_shells}|${_out}/shelled_block.step"
        EXIT_CODE 0 STDOUT_REGEX "^Wrote .*shelled_block\\.step: STEP AP214 \\(mm\\), 3 bodies \\(Cup, Tube, Casing\\), [0-9]+ bytes$"
        FILE "${_out}/shelled_block.step" FILE_REGEX "^ISO-10303-21;\nHEADER;")
    # P12-FEAT-004: a block drafted about planes and about its own faces.
    set(_drafts "${PROJECT_SOURCE_DIR}/examples/models/drafted_block.bcad")
    bettercad_add_process_test(cli.info.drafted-block
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "info|${_drafts}"
        EXIT_CODE 0
        STDOUT_REGEX "^Document: DraftedBlock\n.*  object:7   draft        Tapered      target Block, faces the side from entity:5 of Block and the side from entity:6 of Block and the side from entity:7 of Block and the side from entity:8 of Block, neutral plane the model's xy, angle taper\n.*  object:10  draft        OnTop        target Block, faces [^\n]*, neutral plane the end cap of Block, angle taper\n  object:11  draft        OnBottom     target Block, faces [^\n]*, neutral plane the start cap of Block, angle -5 deg$")
    bettercad_add_process_test(cli.validate.drafted-block
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "validate|${_drafts}"
        EXIT_CODE 0
        STDOUT_REGEX "feature regeneration  ok, 8 objects regenerated\n  geometry              ok, 5 result bodies\nResult bodies \\(5\\):\n  Tapered \\(object:7\\): 1 solid, volume 218256\\.066 mm\\^3, area 23215\\.960 mm\\^2, bounds \\(0, 0, 0\\) to \\(100, 60, 40\\) mm\n  TaperedMid \\(object:8\\): 1 solid, volume 240163\\.291 mm\\^3, area 24873\\.388 mm\\^2, bounds \\(-1\\.75, -1\\.75, 0\\) to \\(101\\.75, 61\\.75, 40\\) mm\n  Flared \\(object:9\\): 1 solid, volume 263050\\.262 mm\\^3, area 26579\\.802 mm\\^2, bounds \\(-3\\.5, -3\\.5, 0\\) to \\(103\\.5, 63\\.5, 40\\) mm\n  OnTop \\(object:10\\): 1 solid, volume 263050\\.262 mm\\^3, area 26579\\.802 mm\\^2, bounds \\(-3\\.5, -3\\.5, 0\\) to \\(103\\.5, 63\\.5, 40\\) mm\n  OnBottom \\(object:11\\): 1 solid, volume 218256\\.066 mm\\^3, area 23215\\.960 mm\\^2, bounds \\(0, 0, 0\\) to \\(100, 60, 40\\) mm\nResult: valid$")
    bettercad_add_process_test(cli.export-step.drafted-block
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "export-step|${_drafts}|${_out}/drafted_block.step"
        EXIT_CODE 0 STDOUT_REGEX "^Wrote .*drafted_block\\.step: STEP AP214 \\(mm\\), 5 bodies \\(Tapered, TaperedMid, Flared, OnTop, OnBottom\\), [0-9]+ bytes$"
        FILE "${_out}/drafted_block.step" FILE_REGEX "^ISO-10303-21;\nHEADER;")
    # P12-FEAT-005: a rib across the inside corner of an L bracket.
    set(_ribs "${PROJECT_SOURCE_DIR}/examples/models/ribbed_bracket.bcad")
    bettercad_add_process_test(cli.info.ribbed-bracket
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "info|${_ribs}"
        EXIT_CODE 0
        STDOUT_REGEX "^Document: RibbedBracket\n.*  object:10  sketch       RibSketch    3 entities, 2 constraints, on Mid\n  object:11  rib          Rib          target Bracket, profile RibSketch \\(1 edge\\), thickness thickness, symmetric, left side$")
    bettercad_add_process_test(cli.validate.ribbed-bracket
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "validate|${_ribs}"
        EXIT_CODE 0
        STDOUT_REGEX "feature regeneration  ok, 7 objects regenerated\n  geometry              ok, 1 result body\nResult bodies \\(1\\):\n  Rib \\(object:11\\): 1 solid, volume 53800\\.000 mm\\^3, area 14629\\.706 mm\\^2, bounds \\(0, 0, 0\\) to \\(80, 60, 40\\) mm\nResult: valid$")
    bettercad_add_process_test(cli.export-step.ribbed-bracket
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "export-step|${_ribs}|${_out}/ribbed_bracket.step"
        EXIT_CODE 0 STDOUT_REGEX "^Wrote .*ribbed_bracket\\.step: STEP AP214 \\(mm\\), 1 body \\(Rib\\), [0-9]+ bytes$"
        FILE "${_out}/ribbed_bracket.step" FILE_REGEX "^ISO-10303-21;\nHEADER;")
    # P12-FEAT-006: a block with two top edges rounded with variable radii.
    set(_tapers "${PROJECT_SOURCE_DIR}/examples/models/tapered_block.bcad")
    bettercad_add_process_test(cli.info.tapered-block
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "info|${_tapers}"
        EXIT_CODE 0
        STDOUT_REGEX "^Document: TaperedBlock\n.*  object:6  extrude          Block        profile BlockSketch, depth 20 mm, normal, new body\n  object:7  variable_fillet  Taper        target Block, 2 edges: low at 0, high at 1; 2 mm at 0, 4 mm at 0\\.3, 7 mm at 1$")
    bettercad_add_process_test(cli.validate.tapered-block
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "validate|${_tapers}"
        EXIT_CODE 0
        STDOUT_REGEX "feature regeneration  ok, 3 objects regenerated\n  geometry              ok, 1 result body\nResult bodies \\(1\\):\n  Taper \\(object:7\\): 1 solid, volume 98709\\.906 mm\\^3, area 15523\\.393 mm\\^2, bounds \\(0, 0, 0\\) to \\(100, 50, 20\\) mm\nResult: valid$")
    bettercad_add_process_test(cli.export-step.tapered-block
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "export-step|${_tapers}|${_out}/tapered_block.step"
        EXIT_CODE 0 STDOUT_REGEX "^Wrote .*tapered_block\\.step: STEP AP214 \\(mm\\), 1 body \\(Taper\\), [0-9]+ bytes$"
        FILE "${_out}/tapered_block.step" FILE_REGEX "^ISO-10303-21;\nHEADER;")
    # P12-HOLE-001: a plate of holes of standard sizes.
    set(_tapped "${PROJECT_SOURCE_DIR}/examples/models/tapped_plate.bcad")
    bettercad_add_process_test(cli.info.tapped-plate
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "info|${_tapped}"
        EXIT_CODE 0
        STDOUT_REGEX "^Document: Plate\n.*  object:5  hole     Tapped       target Plate, simple through hole, thread M8-6H full length, centre \\(15 mm, 15 mm\\) on plane through \\(0, 0, 12\\) mm facing \\(0, 0, 1\\)\n.*  object:7  hole     Seat         target BlindTapped, counterbore through hole, clearance for M8 \\(medium\\) H13, counterbore 15 mm x 5 mm deep, centre \\(40 mm, 25 mm\\) on plane through \\(0, 0, 12\\) mm facing \\(0, 0, 1\\)\n  object:8  hole     Boss         target Seat, spotface through hole, thread M10-6H full length, spotface 20 mm x 1 mm deep, centre \\(65 mm, 32 mm\\) on plane through \\(0, 0, 12\\) mm facing \\(0, 0, 1\\)\n  object:9  hole     Reamed       target Boss, simple blind hole 8 mm deep, diameter bore H7, centre \\(65 mm, 8 mm\\) on plane through \\(0, 0, 12\\) mm facing \\(0, 0, 1\\)$")
    bettercad_add_process_test(cli.validate.tapped-plate
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "validate|${_tapped}"
        EXIT_CODE 0
        STDOUT_REGEX "feature regeneration  ok, 7 objects regenerated\n  geometry              ok, 1 result body\nResult bodies \\(1\\):\n  Reamed \\(object:9\\): 1 solid, volume 44516\\.171 mm\\^3, area 12255\\.385 mm\\^2, bounds \\(0, 0, 0\\) to \\(80, 50, 12\\) mm\nResult: valid$")
    bettercad_add_process_test(cli.export-step.tapped-plate
        COMMAND "$<TARGET_FILE:bettercad_cli>" ARGS "export-step|${_tapped}|${_out}/tapped_plate.step"
        EXIT_CODE 0 STDOUT_REGEX "^Wrote .*tapped_plate\\.step: STEP AP214 \\(mm\\), 1 body \\(Reamed\\), [0-9]+ bytes$"
        FILE "${_out}/tapped_plate.step" FILE_REGEX "^ISO-10303-21;\nHEADER;")
    bettercad_add_process_test(cli.export-stl.reference.pulley
        COMMAND "$<TARGET_FILE:bettercad_cli>"
        ARGS "export-stl|${_reference}/pulley.bcad|${_out}/pulley.stl"
        EXIT_CODE 0 STDOUT_REGEX "binary STL \\(mm\\), 1 body \\(HubChamfers: [0-9]+ triangles\\)"
        FILE "${_out}/pulley.stl" FILE_REGEX "^BetterCAD [^ ]+ binary STL, units mm: Pulley")
endif()

if(TARGET bettercad)
    # Headless: the offscreen platform plugin needs no display.
    add_test(NAME gui.launch.smoke
        COMMAND "$<TARGET_FILE:bettercad>" --smoke-test -platform offscreen)
    set_tests_properties(gui.launch.smoke PROPERTIES TIMEOUT 60)
endif()

# ---------------------------------------------------------------------------
# Build infrastructure (build location, Qt deployment)
# ---------------------------------------------------------------------------
add_subdirectory(infra)

# ---------------------------------------------------------------------------
# Architecture rules (layering, third-party containment)
# ---------------------------------------------------------------------------
add_subdirectory(architecture)

# ---------------------------------------------------------------------------
# Build-failure tests (misuse must not compile)
# ---------------------------------------------------------------------------
add_subdirectory(compile_fail)

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
        TKernel TKMath TKG3d TKGeomBase TKBRep TKTopAlgo TKXSBase TKDESTEP
)

# ---------------------------------------------------------------------------
# Unit tests (Catch2), one CTest test per TEST_CASE
# ---------------------------------------------------------------------------
bettercad_add_executable(bettercad_tests
    SOURCES
        TestHelpers.hpp
        assembly/ComponentFileTests.cpp
        assembly/ComponentTests.cpp
        assembly/ConfigurationTests.cpp
        assembly/MateFileTests.cpp
        assembly/MateTests.cpp
        assembly/MechanicalMateTests.cpp
        assembly/MechanicalSolveTests.cpp
        assembly/SolverDerivativeTests.cpp
        assembly/SolverTests.cpp
        assembly/ReferenceFileTests.cpp
        assembly/ReferenceTests.cpp
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
        core/units/ConversionTests.cpp
        core/units/FormatAndCatalogTests.cpp
        core/units/QuantityTests.cpp
        features/ChamferFeatureTests.cpp
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
    )
    target_link_libraries(bettercad_tests PRIVATE BetterCAD::cli_lib)
endif()
# The mechanical reference models (P11-REF-001) are built by the examples'
# library, with the same public APIs as any client.
if(TARGET bettercad_reference_models)
    target_sources(bettercad_tests PRIVATE
        reference/AllModelsTests.cpp
        reference/Analytic.hpp
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
#     [FILE <path> [FILE_REGEX <re>]])
function(bettercad_add_process_test name)
    cmake_parse_arguments(PARSE_ARGV 1 arg ""
        "COMMAND;ARGS;EXIT_CODE;STDOUT_REGEX;STDERR_REGEX;FILE;FILE_REGEX" "")
    add_test(NAME ${name}
        COMMAND "${CMAKE_COMMAND}"
            "-DCOMMAND=${arg_COMMAND}"
            "-DARGS=${arg_ARGS}"
            "-DEXPECTED_EXIT_CODE=${arg_EXIT_CODE}"
            "-DEXPECTED_STDOUT_REGEX=${arg_STDOUT_REGEX}"
            "-DEXPECTED_STDERR_REGEX=${arg_STDERR_REGEX}"
            "-DEXPECTED_FILE=${arg_FILE}"
            "-DEXPECTED_FILE_REGEX=${arg_FILE_REGEX}"
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
# Architecture rules (layering, third-party containment)
# ---------------------------------------------------------------------------
add_subdirectory(architecture)

# ---------------------------------------------------------------------------
# Build-failure tests (misuse must not compile)
# ---------------------------------------------------------------------------
add_subdirectory(compile_fail)

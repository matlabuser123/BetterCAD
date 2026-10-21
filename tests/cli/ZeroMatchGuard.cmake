# The zero-match test-filter guard (P16-CLI-001).
#
# THE DEFECT THIS EXISTS TO PREVENT. `ctest -R <pattern>` exits 0 when the
# pattern matches NOTHING. It prints "No tests were found!!!" and returns
# success, so a qualification script that runs a targeted filter and trusts the
# exit code reports PASS having executed no test at all. A renamed test, a
# typo, or a filter written against a different preset's names all produce
# exactly that.
#
# So this asserts the COUNT, not the exit code: the named filters must each
# discover at least a stated minimum, and a filter known to match nothing must
# be seen to match nothing. The second half is what proves the guard itself
# works -- without it, a broken counter would report PASS for every filter.
#
# Inputs:
#   CTEST       the ctest executable
#   BUILD_DIR   the build directory to enumerate in
cmake_minimum_required(VERSION 3.28)

foreach(required CTEST BUILD_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "ZeroMatchGuard.cmake: ${required} is required")
    endif()
endforeach()

# How many tests a filter discovers. `ctest -N` lists without running.
function(discovered pattern out_var)
    execute_process(
        COMMAND "${CTEST}" --test-dir "${BUILD_DIR}" -R "${pattern}" -N
        OUTPUT_VARIABLE _out
        ERROR_VARIABLE _err
        RESULT_VARIABLE _code)
    if(NOT _code EQUAL 0)
        message(FATAL_ERROR "ctest -N failed for '${pattern}': ${_err}")
    endif()
    # "Total Tests: N" is ctest's own count, so this does not re-implement
    # counting by parsing the listing.
    if(NOT _out MATCHES "Total Tests: ([0-9]+)")
        message(FATAL_ERROR "ctest -N printed no total for '${pattern}':\n${_out}")
    endif()
    set(${out_var} "${CMAKE_MATCH_1}" PARENT_SCOPE)
endfunction()

# Each filter this milestone relies on, and the minimum it must find. The
# minimums are deliberately below the real counts: the point is to catch a
# filter that matches NOTHING, not to make every added test a maintenance
# task.
set(_filters
    "unit\\.MeshingCli" 10
    "cli\\.mesh\\." 5
    "unit\\.MeshingPersistence" 10
    "unit\\.MeshingCommand" 10
    # P16-REFMOD-001. The reference suite's own two filters: the in-process
    # cases and the fresh-process ones. The qualification runs both, so both
    # have to be seen to match something.
    "unit\\.Mesh(Block|Cylinder|PlateWithHole|Tube|ThinPlate|TransformedBlock|LocalRefinement|OpenProfile|Reference|Curved)" 20
    "refmod\\.mesh\\." 15
    # P16-QUAL-001. The final cross-milestone gates, which no per-milestone
    # filter reaches: they live under their own test-name prefix precisely so
    # that the final qualification can select them and be seen to have done so.
    "unit\\.P16Qualification" 4
    # P17-CLI-001. The in-process command tests, and the fresh-process ones --
    # which include the multi-process workflow. Both halves are listed because
    # the qualification runs both, so both have to be seen to match something:
    # the in-process cases cannot prove the executable works, and the process
    # cases cannot reach the core for an equivalence comparison.
    "unit\\.StructuralCli" 15
    "cli\\.structural\\." 8
    # P17-REFMOD-001. The structural reference suite's own two filters: the
    # in-process cases, named per model as the repository's convention gives
    # them, and the fresh-process ones. Both run in the qualification, so
    # both have to be seen to match something.
    "unit\\.Structural(Patch|AxialBar|Cantilever|PressureBlock|GravityBlock|Transformed|LocalRefinement|UnderConstrained|StaleMesh|Unresolved|Reference)" 12
    "refmod\\.structural\\." 30
    # P17-QUAL-001. The final cross-milestone gates, under their own prefix for
    # the reason P16-QUAL-001 gave: so the final qualification can select them
    # and be seen to have done so.
    "unit\\.P17Qualification" 3
    # P18-ARCH-001. The thermal input boundary's cases, and the checker
    # fixtures that prove the module's directions.
    "unit\\.ThermalInput" 8
    "architecture\\.checker\\.[a-z-]*thermal" 4
    # P18-DATA-001. The data-model cases, and the compile-fail group that
    # proves the thermal identities do not convert.
    "unit\\.ThermalData" 20
    "compile_fail\\.thermalids\\." 12
    # P18-MAT-001. The thermal material resolution cases.
    "unit\\.ThermalMaterial" 15
    # P18-DOF-001. The temperature DOF model cases, and the compile-fail group
    # that proves the solver-local indices do not convert.
    "unit\\.ThermalDof" 20
    "compile_fail\\.thermaldof\\." 8
    # P18-ELEM-001. The Tet4 thermal element cases, by the brief's seven
    # groups -- geometry, orientation and degeneracy, conduction, capacity,
    # transforms, scaling, determinism -- each under its own name prefix so a
    # targeted run of any one group can be seen to have matched something;
    # and the compile-fail group that proves the quantities and the local
    # matrices do not convert.
    "unit\\.ThermalElement" 40
    "unit\\.ThermalElement_(UnitTet|GeneralTet|ShapeGradient|Affine|Constant)" 5
    "unit\\.ThermalElement_(Inverted|Coplanar|Coincident|NearDegenerate|NonFinite)" 4
    "unit\\.ThermalElement_Conduct" 8
    "unit\\.ThermalElement_(Capacity|OnePoint|Density|SpecificHeat)" 8
    "unit\\.ThermalElement_(Translation|Rotation|Gradient|Reflection|Even|Odd)" 5
    "unit\\.ThermalElement_(GeometryScaling|ConductivityScaling)" 2
    "unit\\.ThermalElement_(Repeat|NoHidden|BitFingerprint)" 3
    "compile_fail\\.thermalelem\\." 8
    # P18-LOAD-001. The thermal load cases, by the brief's groups -- surface,
    # volume, mapping, remesh and currentness, sparse identities, superposition,
    # invalid input, determinism -- each under its own name prefix so a targeted
    # run of any one group can be seen to have matched something; and the
    # compile-fail group that proves the load values, identities and the
    # prepared type do not convert.
    "unit\\.ThermalLoad" 66
    "unit\\.ThermalLoad_(Surface|SingleTriangle|GeneralTriangle|Triangulation|FacetWinding)" 10
    "unit\\.ThermalLoad_(Volume|UnitTet|GeneralTet|Generation|SharedVolume|MultiTet)" 10
    "unit\\.ThermalLoad_(GeometryReference|Unresolved|Malformed|NoNearest|Empty|Target|OneInvalid)" 7
    "unit\\.ThermalLoad_(Prepared|Remesh|SameCounts|SameIds|Edit|Material|Solver|Geometry|Document)" 12
    "unit\\.ThermalLoad_(Sparse|LargeSparse)" 3
    "unit\\.ThermalLoad_(.*Superposition|SignedCancellation|LoadOrder)" 5
    "unit\\.ThermalLoad_(Rejects|Zero|Duplicate|Repeated|ANumbering|Every|NonFinite)" 12
    "unit\\.ThermalLoad_(RepeatedPreparation|Diagnostics|BitFingerprint)" 3
    "compile_fail\\.thermalload\\." 19
    # P18-BC-001. The thermal boundary-condition cases, by the brief's groups --
    # Dirichlet, conflict, Robin analytical, mapping, remesh and currentness,
    # invalid input, sparse identities and determinism, plus the Robin traps, the
    # scaling cases, superposition and purity -- each under its own name prefix so
    # a targeted run of any one group can be seen to have matched something; and
    # the compile-fail group that proves the values, identities and the prepared
    # type do not convert and that nothing mesh-local, penalty or solve is
    # reachable.
    "unit\\.ThermalBC" 92
    "unit\\.ThermalBC_(PrescribedTemperatureDef|ConvectionDef|CanonicalGeometry|UnitsContract|SignConvention)" 5
    "unit\\.ThermalBC_(PrescribedTemperature(Maps|Uses|Stores|Dedup|Deter)|MappedFaceDirichlet)" 6
    "unit\\.ThermalBC_(Duplicate|Conflict|PartialSurface|IdenticalOverlap)" 8
    "unit\\.ThermalBC_(SingleTriangle|GeneralTriangle|ConvectionMatrix|ConvectionAmbientEq|ConvectionAbove|ConvectionBelow|NonUniform|ConvectionFacet|SharedFacet|TriangleWinding|ConstantTemperature|MappedFaceConvection)" 14
    "unit\\.ThermalBC_(ConvectionRejects|ConvectionIsNotLumped|WrongSigns)" 3
    "unit\\.ThermalBC_(ConvectionArea|ConvectionCoefficientScaling|ConvectionAmbientScaling|ConvectionDoesNot|ConvectionIsIndependent)" 5
    "unit\\.ThermalBC_(GeometryReference|PreparedBoundaryKeeps|DirichletAndRobin|NoDirichlet|Unresolved|Unsupported|NoNearest|OneInvalid|Empty)" 9
    "unit\\.ThermalBC_(PreparedBCBinds|Remesh|SameCounts|SameIds|ADifferentBody|Edit|Material|LoadSolver|GeometryEdit|DocumentBoundary)" 12
    "unit\\.ThermalBC_(ZeroConvection|Negative|NaN|Infinite|BelowZero|InvalidArea|EveryBoundary|SeamRefusals|ConvectionSeamRefusals|ZeroArea|ConvectionZeroCoefficientSeam|Identities|ValuesAreChecked|ANumbering|Overflowing)" 20
    "unit\\.ThermalBC_(Sparse|LargeSparse)" 2
    "unit\\.ThermalBC_ConvectionSuperposition" 2
    "unit\\.ThermalBC_(AFailedPreparation|PreparationDoesNot)" 2
    "unit\\.ThermalBC_(RepeatedPreparation|Diagnostics|BitFingerprint)" 3
    "unit\\.ThermalBC_PreparedBoundaryHasNo" 1
    "compile_fail\\.thermalbc\\." 31
    # P18-ASSEMBLY-001. The global thermal assembly cases, by the brief's groups --
    # the single-tetrahedron and two-tetrahedra analytical systems, the convection
    # scatter, the dense-oracle and energy identities, the resultants and the null
    # mode, source compatibility and currentness, the steady-versus-transient
    # material contract, the sparse containers and sparse identities, the seam
    # refusals, and determinism -- each under its own name prefix so a targeted run
    # of any one group can be seen to have matched something; and the compile-fail
    # group that proves K and C are different types, a row is an unknown, and
    # nothing that solves or constrains is reachable.
    "unit\\.ThermalAssembly" 62
    "unit\\.ThermalAssembly_SingleTet" 4
    "unit\\.ThermalAssembly_TwoTet" 4
    "unit\\.ThermalAssembly_(Convection|RepeatedConvection)" 6
    "unit\\.ThermalAssembly_(SmallSystemMatchesIndependentDenseOracle|BlockMatchesIndependentDenseOracle|EnergyIdentities|BlockEnergyIdentities)" 4
    "unit\\.ThermalAssembly_(HeatResultants|BlockHeatResultants)" 2
    "unit\\.ThermalAssembly_(PureConduction|BlockNullMode|ConvectionLifts|TwoTetConstant)" 4
    "unit\\.ThermalAssembly_(Rejects|Remesh|SameIds|Edits|TransientCapacityEdit|FailureOrder)" 9
    "unit\\.ThermalAssembly_(StaleReasons|SameBuilder|BlockSourceRecords)" 3
    "unit\\.ThermalAssembly_(SteadyKOnly|TransientRequires|TransientCapacityEdit)" 3
    "unit\\.ThermalAssembly_(Sparse|LargeSparse|LargeMesh|LargeReal|UsesSparse|Disconnected)" 8
    "unit\\.ThermalAssembly_(SeamRefusals|HeatVectorRefusesNonFinite|EmptyLoadVector)" 3
    "unit\\.ThermalAssembly_(Repeated|BitFingerprint|BlockBitFingerprint)" 5
    "unit\\.ThermalAssembly_(AssemblyDoesNotMutate|PrescribedTemperaturesNever|LocalOrder|DefinitionOverload)" 4
    "compile_fail\\.thermalsys\\." 36
    # P18-SOLVE-001. The steady thermal solve, by the brief's groups -- the Dirichlet
    # partition and its nonzero right-hand side, the analytical and independent
    # oracles, singularity and the missing datum, disconnected regions, the
    # independent residual and the wrong answers it must refuse, source
    # compatibility and the result lifecycle, determinism, and the sparse solves --
    # plus the generic kernel it calls, the ADR-050 / ADR-052 architecture gates and
    # the compile-fail group. Each under its own name so a targeted run of any one
    # can be seen to have matched something.
    "unit\\.ThermalSolve" 51
    "unit\\.Linalg_" 10
    "unit\\.ThermalSolve_(DirichletPartition|InterleavedConstraints|NonzeroDirichlet|OneFreeNode|SingleTetThrough)" 5
    "unit\\.ThermalSolve_(ThreeNodeChain|RobinOnly|RobinAnchored|FullyConstrained|OneFreeNode|MatchesIndependentDenseOracle|LinearProfile|AffineField|BlockLinearProfile|BlockDirichletInSeries|BlockConvectionOnly|ConvectionAnchors)" 13
    "unit\\.ThermalSolve_(PureNeumann|FactorisationSingularity|AnchorTolerance|BlockWithoutAnyDatum)" 5
    "unit\\.ThermalSolve_(Disconnected|ExplicitZeros)" 3
    "unit\\.ThermalSolve_(IndependentResidual|BadSolution|RejectsNonFinite|ConstraintViolation|NonFiniteReduced|FreeResidualMatches|ResidualTolerance|SystemScaling)" 8
    "unit\\.ThermalSolve_(StaleReasons|Edits|Remesh|SolutionRecordsExactly|FailedResolve|RejectsStaleBoundary|SeamRefusals|RejectsMismatched)" 8
    "unit\\.ThermalSolve_(Deterministic|RepeatedBlockSolve|BitFingerprint|SolveDoesNotMutate)" 5
    "unit\\.ThermalSolve_(LargeSparse|FineCylinder|SparseNodeIds)" 3
    "compile_fail\\.thermalsolve\\." 26
    "architecture\\.targets\\." 3
    "architecture\\.checker\\.linalg" 2
    # P18-TRANSIENT-001. The backward-Euler transient solve, by the brief's groups --
    # the time grid, the effective matrix C/dt + K, the hard oracles (the non-uniform
    # consistent-capacity pair and the one-unknown body), the singularity rules (a
    # pure-Neumann or disconnected model SUCCEEDS where the steady one is refused),
    # nonzero Dirichlet and the initial state, stepping and the output policy, the
    # refusals, the two independent residuals and the wrong steps they must refuse, the
    # scaling, steady-limit, refinement, lumped and energy checks, source compatibility
    # and the result lifecycle, determinism, the sparse runs, the assembled-system seam
    # and the diagnostics' vocabulary -- plus the compile-fail group that proves K and C
    # are different types and that nothing time-dependent, adaptive or regularised is
    # reachable. Each under its own name so a targeted run of any one can be seen to
    # have matched something.
    "unit\\.ThermalTransient" 72
    "unit\\.ThermalTransient_TimeGrid" 10
    "unit\\.ThermalTransient_EffectiveMatrix" 3
    "unit\\.ThermalTransient_(OneDofHandOracle|NonUniformTwoUnknown)" 2
    "unit\\.ThermalTransient_(PureNeumann|DisconnectedPositive|RegionWithNoCapacity|ZeroCapacityConnected|InsulatedBlock)" 5
    "unit\\.ThermalTransient_(NonzeroDirichlet|PrescribedDominates|FullyPrescribed)" 3
    "unit\\.ThermalTransient_(LastPartialStep|OutputPolicies|EveryStepEvidence)" 3
    "unit\\.ThermalTransient_(AlgebraRefusals|TheUsersSolverSettings|AStepThatCannotBeTaken|AFailureAtALaterStep|AnOverflowingHistory|SettingsAreRefused|SeamRefusals|AStepBelowTheResolution|AFailedRunNames|EffectiveMatrixRefusals)" 11
    "unit\\.ThermalTransient_(Acceptance|PhysicalResidual|AnOverflowingResidual)" 8
    "unit\\.ThermalTransient_(AnswerIsInvariant|SteadyLimit|TimeStepRefinement|LumpedBody|BlockCooling|BlockRelaxes|BlockWithGeneration|EnergyIsConserved|ConstantTemperature|ABlockAtItsAmbient)" 10
    "unit\\.ThermalTransient_(SolutionRecordsExactly|EditsStale|RejectsStaleBoundary|RemeshStales|SteadyAndTransientSystems|FailedRunDoesNot|StaleReasonsName|SolvesADefinition|RunDoesNotMutate|SolutionAccessors)" 10
    "unit\\.ThermalTransient_(RepeatedRuns|RepeatedBlockRun|TheInputsAreNever|TimeGridIsDeterministic)" 4
    "unit\\.ThermalTransient_(LargeSparse|FineCylinder|SparseNodeIds)" 3
    "unit\\.ThermalTransient_(SeamMatches|SeamRefusals|SparseNodeIds)" 3
    "unit\\.ThermalTransient_DiagnosticNames" 1
    "compile_fail\\.thermaltransient\\." 35
)
list(LENGTH _filters _count)
math(EXPR _last "${_count} - 1")
foreach(i RANGE 0 ${_last} 2)
    list(GET _filters ${i} _pattern)
    math(EXPR _j "${i} + 1")
    list(GET _filters ${_j} _minimum)
    discovered("${_pattern}" _found)
    if(_found LESS _minimum)
        message(FATAL_ERROR
            "ZERO-MATCH GUARD FAILED: '${_pattern}' discovered ${_found} tests, "
            "expected at least ${_minimum}. A filter that matches too few tests "
            "makes a targeted run meaningless, and ctest -R exits 0 when it "
            "matches none at all.")
    endif()
    message(STATUS "${_pattern}: ${_found} tests (minimum ${_minimum})")
endforeach()

# AND THE GUARD ITSELF MUST WORK. A pattern that cannot match anything has to
# come back as zero; if this reported a non-zero count, every check above would
# be vacuous.
discovered("unit\\.ThisTestDoesNotExist_P16CLI001" _none)
if(NOT _none EQUAL 0)
    message(FATAL_ERROR
        "the guard is broken: a deliberately impossible filter discovered ${_none} tests")
endif()
message(STATUS "an impossible filter discovers 0 tests, so the counter is real")

# And ctest really does exit 0 on a zero match, which is the whole premise.
execute_process(
    COMMAND "${CTEST}" --test-dir "${BUILD_DIR}" -R "unit\\.ThisTestDoesNotExist_P16CLI001"
    OUTPUT_VARIABLE _out ERROR_VARIABLE _err RESULT_VARIABLE _code)
message(STATUS "ctest on an impossible filter exits ${_code} -- which is why counting is required")

message(STATUS "ZeroMatchGuard: every filter discovers tests, and the counter is proven")

# Build-failure tests: prove that misuse is rejected by the compiler.
#
# Each group has one source file. The file compiled without any
# BETTERCAD_CF_* macro is the group's control target: it is part of the normal
# build and must compile cleanly. Each case compiles the same file with one
# BETTERCAD_CF_<CASE> macro and expects the build to fail with a case-specific
# diagnostic. Because the control compiles, every failure is attributable to
# the selected line.

# bettercad_compile_fail_group(<group> <source> [<extra libraries>...])
#
# The targets are OBJECT libraries, so nothing links: the extra libraries are
# there for their INTERFACE include directories, which is what a case above
# layer 0 needs (a generated Export.hpp lives with its own target).
function(bettercad_compile_fail_group group source)
    add_library(bettercad_cf_${group}_control OBJECT "${source}")
    target_link_libraries(bettercad_cf_${group}_control PRIVATE
        BetterCAD::core BetterCAD::compiler_options ${ARGN})
endfunction()

# bettercad_compile_fail_case(<group> <source> <CASE> <gnu_regex> [<extra libraries>...])
#
# Diagnostics are matched for GCC (the verified toolchain); other compilers
# only need to report an error. Regexes avoid quote characters because GCC
# prints Unicode quotes in UTF-8 locales.
function(bettercad_compile_fail_case group source case gnu_regex)
    string(TOLOWER "${case}" name)
    string(REPLACE "_" "-" name "${name}")
    set(target "bettercad_cf_${group}_${name}")

    add_library(${target} OBJECT EXCLUDE_FROM_ALL "${source}")
    target_compile_definitions(${target} PRIVATE "BETTERCAD_CF_${case}")
    target_link_libraries(${target} PRIVATE BetterCAD::core BetterCAD::compiler_options ${ARGN})
    target_compile_options(${target} PRIVATE
        "$<$<CXX_COMPILER_ID:GNU>:-fdiagnostics-plain-output>")

    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set(regex "${gnu_regex}")
    else()
        set(regex "error")
    endif()
    bettercad_add_process_test(compile_fail.${group}.${name}
        COMMAND "${CMAKE_COMMAND}"
        ARGS "--build|${PROJECT_BINARY_DIR}|--config|$<CONFIG>|--target|${target}"
        EXIT_CODE 1
        STDOUT_REGEX "${regex}"
    )
    # Tests that drive the build tree must not run concurrently.
    set_tests_properties(compile_fail.${group}.${name} PROPERTIES RESOURCE_LOCK bettercad_build_tree)
endfunction()

# --- Units (P1-001) ---------------------------------------------------------
set(_units "${CMAKE_CURRENT_SOURCE_DIR}/UnitsMisuse.cpp")
bettercad_compile_fail_group(units "${_units}")
bettercad_compile_fail_case(units "${_units}" ADD_LENGTH_ANGLE "no match for [^ ]*operator\\+")
bettercad_compile_fail_case(units "${_units}" ASSIGN_MASS_TO_LENGTH "no match for [^ ]*operator=")
bettercad_compile_fail_case(units "${_units}" ASSIGN_DOUBLE_TO_LENGTH "no match for [^ ]*operator=.*double")
bettercad_compile_fail_case(units "${_units}" LENGTH_TO_DOUBLE "cannot convert .* to [^ ]*double")
bettercad_compile_fail_case(units "${_units}" FORCE_PER_LENGTH_AS_PRESSURE "conversion from .* to non-scalar type")
bettercad_compile_fail_case(units "${_units}" COMPARE_LENGTH_TIME "no match for [^ ]*operator<")
bettercad_compile_fail_case(units "${_units}" SQRT_OF_VOLUME "no matching function for call to [^ ]*sqrt")

# P15-UNITS-001: the engineering-data quantities.
bettercad_compile_fail_case(units "${_units}" ADD_DENSITY_PRESSURE "no match for [^ ]*operator\+")
bettercad_compile_fail_case(units "${_units}" MODULUS_AS_CONDUCTIVITY "could not convert")
bettercad_compile_fail_case(units "${_units}" LENGTH_AS_DENSITY "could not convert")
bettercad_compile_fail_case(units "${_units}" SPECIFIC_HEAT_AS_EXPANSION "conversion from .* to non-scalar type")
bettercad_compile_fail_case(units "${_units}" MASS_PER_AREA_AS_DENSITY "conversion from .* to non-scalar type")
bettercad_compile_fail_case(units "${_units}" TEMPERATURE_AS_EXPANSION "could not convert")
bettercad_compile_fail_case(units "${_units}" POISSON_FROM_DOUBLE "conversion from [^ ]*double[^ ]* to non-scalar type")
bettercad_compile_fail_case(units "${_units}" POISSON_TO_DOUBLE "cannot convert")
bettercad_compile_fail_case(units "${_units}" POISSON_AS_KINEMATIC_VISCOSITY "could not convert")
bettercad_compile_fail_case(units "${_units}" DYNAMIC_AS_KINEMATIC_VISCOSITY "could not convert")
bettercad_compile_fail_case(units "${_units}" CONDUCTIVITY_AS_SPECIFIC_HEAT "conversion from .* to non-scalar type")

# --- Mechanical material properties (P15-MECH-001) --------------------------
set(_mech "${CMAKE_CURRENT_SOURCE_DIR}/MechanicalMisuse.cpp")
bettercad_compile_fail_group(mech "${_mech}")
bettercad_compile_fail_case(mech "${_mech}" DENSITY_AS_YOUNGS_MODULUS "no matching function for call to .*::known.bettercad::Quantity<")
bettercad_compile_fail_case(mech "${_mech}" CONDUCTIVITY_AS_YIELD_STRENGTH "no matching function for call to .*::known.bettercad::Quantity<")
bettercad_compile_fail_case(mech "${_mech}" SPECIFIC_HEAT_AS_SHEAR_MODULUS "conversion from|could not convert")
bettercad_compile_fail_case(mech "${_mech}" POISSON_FROM_DOUBLE "no matching function for call to .*MaterialProperty<bettercad::PoissonRatio>::known.double.")
bettercad_compile_fail_case(mech "${_mech}" ELONGATION_FROM_DOUBLE "no matching function for call to .*Elongation>::known.double.")
bettercad_compile_fail_case(mech "${_mech}" HARDNESS_WITHOUT_SCALE "too few arguments|no matching function")
bettercad_compile_fail_case(mech "${_mech}" HARDNESS_AS_STRESS "conversion from|could not convert")
bettercad_compile_fail_case(mech "${_mech}" PROPERTY_TO_VALUE "conversion from|could not convert")
bettercad_compile_fail_case(mech "${_mech}" MUTATE_LIBRARY_ENTRY "const bettercad::materials::LibraryMaterial.*discards qualifiers")

# --- Material assignment (P15-ASSIGN-001) -----------------------------------
set(_assign "${CMAKE_CURRENT_SOURCE_DIR}/AssignmentMisuse.cpp")
bettercad_compile_fail_group(assign "${_assign}" BetterCAD::features BetterCAD::assembly)
bettercad_compile_fail_case(assign "${_assign}" ASSIGN_OBJECT_ID
    "(could not|cannot) convert .*ObjectIdTag.* to .*MaterialIdTag"
    BetterCAD::features BetterCAD::assembly)
bettercad_compile_fail_case(assign "${_assign}" ASSIGN_INTEGER
    "(could not|cannot) convert .* from .int. to .*MaterialId"
    BetterCAD::features BetterCAD::assembly)
bettercad_compile_fail_case(assign "${_assign}" ASSIGN_COMPONENT_ID
    "(could not|cannot) convert .*ComponentIdTag.* to .*MaterialIdTag"
    BetterCAD::features BetterCAD::assembly)
bettercad_compile_fail_case(assign "${_assign}" DOCUMENT_ASSIGN_OBJECT_ID
    "(could not|cannot) convert .*ObjectIdTag|no matching function for call to .*setMaterialAssignment"
    BetterCAD::features BetterCAD::assembly)
bettercad_compile_fail_case(assign "${_assign}" OCCURRENCE_MATERIAL_FIELD
    "no member named .material"
    BetterCAD::features BetterCAD::assembly)
bettercad_compile_fail_case(assign "${_assign}" OCCURRENCE_MATERIAL_READ
    "no member named .material"
    BetterCAD::features BetterCAD::assembly)

# --- Thermal material properties (P15-THERM-001) ----------------------------
set(_therm "${CMAKE_CURRENT_SOURCE_DIR}/ThermalMisuse.cpp")
bettercad_compile_fail_group(therm "${_therm}")
bettercad_compile_fail_case(therm "${_therm}" PRESSURE_AS_CONDUCTIVITY "no matching function for call to .*::known.bettercad::Pressure.")
bettercad_compile_fail_case(therm "${_therm}" ENERGY_AS_SPECIFIC_HEAT "no matching function for call to .*::known.bettercad::Energy.")
bettercad_compile_fail_case(therm "${_therm}" HEAT_CAPACITY_AS_SPECIFIC_HEAT "no matching function for call to .*::known.bettercad::Quantity<")
bettercad_compile_fail_case(therm "${_therm}" POISSON_AS_THERMAL_EXPANSION "no matching function for call to .*::known.bettercad::PoissonRatio.")
bettercad_compile_fail_case(therm "${_therm}" DIMENSIONLESS_AS_THERMAL_EXPANSION "no matching function for call to .*::known.double.")
bettercad_compile_fail_case(therm "${_therm}" DENSITY_AS_RESISTIVITY "MaterialProperty<bettercad::materials::ElectricalResistivity>::known.bettercad::Quantity<")
bettercad_compile_fail_case(therm "${_therm}" RESISTIVITY_FROM_DOUBLE "MaterialProperty<bettercad::materials::ElectricalResistivity>::known.double.")
bettercad_compile_fail_case(therm "${_therm}" RESISTIVITY_TO_DOUBLE "cannot convert .*ElectricalResistivity")
bettercad_compile_fail_case(therm "${_therm}" TEMPERATURE_FROM_DOUBLE "no matching function for call to .*::known.double.")
bettercad_compile_fail_case(therm "${_therm}" CONDUCTIVITY_AS_TEMPERATURE "no matching function for call to .*::known.bettercad::ThermalConductivity.")

# --- Strong IDs (P1-002) ----------------------------------------------------
set(_ids "${CMAKE_CURRENT_SOURCE_DIR}/IdsMisuse.cpp")
bettercad_compile_fail_group(ids "${_ids}")
bettercad_compile_fail_case(ids "${_ids}" SKETCH_ID_AS_FEATURE_ID "could not convert .*SketchId.* to .*FeatureId")
bettercad_compile_fail_case(ids "${_ids}" INTEGER_TO_ID "conversion from [^ ]*int[^ ]* to non-scalar type")
bettercad_compile_fail_case(ids "${_ids}" ID_TO_INTEGER "cannot convert .*SketchId.* in initialization")
bettercad_compile_fail_case(ids "${_ids}" COMPARE_DIFFERENT_KINDS "no match for [^ ]*operator==")
bettercad_compile_fail_case(ids "${_ids}" OBJECT_TO_SKETCH_ID "conversion from .*ObjectIdTag.* to non-scalar type")
bettercad_compile_fail_case(ids "${_ids}" OBJECT_TO_COMPONENT_ID "conversion from .*ObjectIdTag.* to non-scalar type")
bettercad_compile_fail_case(ids "${_ids}" COMPONENT_ID_AS_FEATURE_ID "conversion from .*ComponentIdTag.* to non-scalar type .*FeatureIdTag")
bettercad_compile_fail_case(ids "${_ids}" OBJECT_TO_MATE_ID "conversion from .*ObjectIdTag.* to non-scalar type")
bettercad_compile_fail_case(ids "${_ids}" MATE_ID_AS_COMPONENT_ID "conversion from .*MateIdTag.* to non-scalar type .*ComponentIdTag")
bettercad_compile_fail_case(ids "${_ids}" OBJECT_TO_SHEET_ID "conversion from .*ObjectIdTag.* to non-scalar type")
bettercad_compile_fail_case(ids "${_ids}" SHEET_ID_AS_COMPONENT_ID "conversion from .*SheetIdTag.* to non-scalar type .*ComponentIdTag")
bettercad_compile_fail_case(ids "${_ids}" OBJECT_TO_MATERIAL_ID "conversion from .*ObjectIdTag.* to non-scalar type")
bettercad_compile_fail_case(ids "${_ids}" MATERIAL_ID_AS_FEATURE_ID "conversion from .*MaterialIdTag.* to non-scalar type .*FeatureIdTag")
bettercad_compile_fail_case(ids "${_ids}" SHEET_ID_AS_MATERIAL_ID "conversion from .*SheetIdTag.* to non-scalar type .*MaterialIdTag")
bettercad_compile_fail_case(ids "${_ids}" INTEGER_TO_MATERIAL_ID "conversion from [^ ]*int[^ ]* to non-scalar type")
bettercad_compile_fail_case(ids "${_ids}" MATERIAL_ID_TO_INTEGER "cannot convert .*MaterialId.* in initialization")
bettercad_compile_fail_case(ids "${_ids}" ENTITY_ID_AS_OBJECT_ID "could not convert .*EntityId")

# --- Mass properties (P15-MASS-001) -----------------------------------------
#
# Half of these separate a second moment of VOLUME from a second moment of MASS,
# which differ by a density and by nine orders of magnitude in a millimetre model.
# The other half prove ABSENCES: nothing can store a mass, because mass is derived
# and never persisted (ADR-026).
set(_massp "${CMAKE_CURRENT_SOURCE_DIR}/MassMisuse.cpp")
bettercad_compile_fail_group(massp "${_massp}" BetterCAD::features)
bettercad_compile_fail_case(massp "${_massp}" VOLUME_MOMENT_AS_MASS_MOMENT
    "no match for [^ ]*operator=" BetterCAD::features)
bettercad_compile_fail_case(massp "${_massp}" MASS_MOMENT_AS_VOLUME_MOMENT
    "no match for [^ ]*operator=" BetterCAD::features)
bettercad_compile_fail_case(massp "${_massp}" ADD_VOLUME_AND_MASS_MOMENT
    "no match for [^ ]*operator\+" BetterCAD::features)
bettercad_compile_fail_case(massp "${_massp}" MASS_MOMENT_IN_MM5
    "cannot convert .*bettercad::Unit<" BetterCAD::features)
bettercad_compile_fail_case(massp "${_massp}" VOLUME_MOMENT_IN_KG_MM2
    "cannot convert .*bettercad::Unit<" BetterCAD::features)
bettercad_compile_fail_case(massp "${_massp}" MASS_MOMENT_FROM_DOUBLE
    "no match for [^ ]*operator=.*double" BetterCAD::features)
bettercad_compile_fail_case(massp "${_massp}" MASS_MOMENT_TO_DOUBLE
    "cannot convert" BetterCAD::features)
bettercad_compile_fail_case(massp "${_massp}" VOLUME_TIMES_DENSITY_IS_NOT_A_MOMENT
    "no match for [^ ]*operator=" BetterCAD::features)
bettercad_compile_fail_case(massp "${_massp}" SHIFT_WITH_A_VOLUME
    "could not convert|cannot convert|invalid conversion" BetterCAD::features)
bettercad_compile_fail_case(massp "${_massp}" SHIFT_WITH_A_DENSITY
    "could not convert|cannot convert|invalid conversion" BetterCAD::features)
bettercad_compile_fail_case(massp "${_massp}" TENSOR_REFERENCE_FROM_COMPONENTS
    "no match for [^ ]*operator=" BetterCAD::features)
bettercad_compile_fail_case(massp "${_massp}" DOCUMENT_STORES_MASS
    "has no member named .setMass" BetterCAD::features)
bettercad_compile_fail_case(massp "${_massp}" DOCUMENT_READS_MASS
    "has no member named .mass" BetterCAD::features)
bettercad_compile_fail_case(massp "${_massp}" MASS_PROPERTIES_HAVE_NO_SETTER
    "has no member named .setMass" BetterCAD::features)
bettercad_compile_fail_case(massp "${_massp}" BODY_RETURNS_MASS
    "has no member named .mass" BetterCAD::features)

# --- Custom materials (P15-CUSTOM-001) --------------------------------------
#
# Two groups of absences. The first proves the built-in library cannot be mutated
# through any path a caller might reach for -- a runtime test can only show that it
# did not change. The second proves there is no override model: ADR-025 chose
# snapshot semantics, so a base reference, an override map and an `Inherited`
# property state are all unrepresentable, which is what keeps "sometimes copied,
# sometimes inherited" from ever arising.
set(_custom "${CMAKE_CURRENT_SOURCE_DIR}/CustomMaterialMisuse.cpp")
bettercad_compile_fail_group(custom "${_custom}" BetterCAD::features)
bettercad_compile_fail_case(custom "${_custom}" LIBRARY_ENTRY_HAS_NO_SETTER
    "has no member named .setDesignation" BetterCAD::features)
bettercad_compile_fail_case(custom "${_custom}" LIBRARY_ENTRY_FIELDS_ARE_PRIVATE
    "is private within this context" BetterCAD::features)
bettercad_compile_fail_case(custom "${_custom}" LIBRARY_TABLE_IS_CONST
    "binding reference of type .* to .* discards qualifiers" BetterCAD::features)
bettercad_compile_fail_case(custom "${_custom}" LIBRARY_ENTRY_IS_NOT_A_DEFINITION
    "invalid initialization of reference|cannot convert|no match for" BetterCAD::features)
bettercad_compile_fail_case(custom "${_custom}" LIBRARY_ENTRY_IS_NOT_A_DOCUMENT_OBJECT
    "cannot convert .*LibraryMaterial.* to .*unique_ptr<bettercad::DocumentObject>" BetterCAD::features)
bettercad_compile_fail_case(custom "${_custom}" LIBRARY_ENTRY_HAS_NO_PROPERTIES
    "has no member named .mechanical" BetterCAD::features)
bettercad_compile_fail_case(custom "${_custom}" FOUND_MATERIAL_IS_CONST
    "discards qualifiers" BetterCAD::features)
bettercad_compile_fail_case(custom "${_custom}" DEFINITION_HAS_NO_ID
    "has no member named .id" BetterCAD::features)
bettercad_compile_fail_case(custom "${_custom}" DEFINITION_HAS_NO_MATERIAL_ID
    "has no member named .materialId" BetterCAD::features)
bettercad_compile_fail_case(custom "${_custom}" NO_BASE_MATERIAL_REFERENCE
    "has no member named .baseMaterial" BetterCAD::features)
bettercad_compile_fail_case(custom "${_custom}" NO_OVERRIDE_MAP
    "has no member named .overrides" BetterCAD::features)
bettercad_compile_fail_case(custom "${_custom}" PROPERTY_IS_NEVER_INHERITED
    "has no member named .isInherited" BetterCAD::features)
bettercad_compile_fail_case(custom "${_custom}" NO_INHERITED_PROPERTY_STATE
    "Inherited. is not a member of" BetterCAD::features)
bettercad_compile_fail_case(custom "${_custom}" DEFINITION_HAS_NO_MASS
    "has no member named .mass" BetterCAD::features)
bettercad_compile_fail_case(custom "${_custom}" DEFINITION_HAS_NO_VOLUME
    "has no member named .volume" BetterCAD::features)
bettercad_compile_fail_case(custom "${_custom}" MECHANICAL_HAS_NO_SHEAR_MODULUS_SLOT
    "has no member named .shearModulus" BetterCAD::features)
bettercad_compile_fail_case(custom "${_custom}" MECHANICAL_HAS_NO_BULK_MODULUS_SLOT
    "has no member named .bulkModulus" BetterCAD::features)
bettercad_compile_fail_case(custom "${_custom}" REMOVE_TAKES_A_TYPED_KIND
    "no matching function for call to .*removeMaterialProperty" BetterCAD::features)
bettercad_compile_fail_case(custom "${_custom}" CLONE_TAKES_A_MATERIAL_ID
    "no matching function for call to .*cloneMaterial|could not convert" BetterCAD::features)
bettercad_compile_fail_case(custom "${_custom}" CLONE_DOES_NOT_TAKE_A_LIBRARY_ENTRY
    "no matching function for call to .*cloneMaterial|could not convert" BetterCAD::features)

# --- Provenance and completeness (P15-PROV-001) ------------------------------
#
# Mostly absences, and the absences are the milestone. There is no single
# completeness flag, because "complete" has no meaning without a consumer; no
# unvalidated Date; no CFD consumer, because ADR-028 defines no requirements for one;
# and no fabricated value anywhere to reach for.
set(_prov "${CMAKE_CURRENT_SOURCE_DIR}/ProvenanceMisuse.cpp")
bettercad_compile_fail_group(prov "${_prov}" BetterCAD::features)
bettercad_compile_fail_case(prov "${_prov}" REPORT_IS_NOT_A_BOOLEAN
    "could not convert|no match for|cannot convert" BetterCAD::features)
bettercad_compile_fail_case(prov "${_prov}" REPORT_HAS_NO_IS_COMPLETE
    "has no member named .isComplete" BetterCAD::features)
bettercad_compile_fail_case(prov "${_prov}" MATERIAL_HAS_NO_IS_COMPLETE
    "has no member named .isComplete" BetterCAD::features)
bettercad_compile_fail_case(prov "${_prov}" COMPLETENESS_NEEDS_A_CONSUMER
    "too few arguments to function .*materialCompleteness" BetterCAD::features)
bettercad_compile_fail_case(prov "${_prov}" CONSUMER_IS_TYPED
    "cannot convert .*int. to .*ConsumerKind" BetterCAD::features)
bettercad_compile_fail_case(prov "${_prov}" NO_CFD_CONSUMER
    "Cfd. is not a member of" BetterCAD::features)
bettercad_compile_fail_case(prov "${_prov}" DATE_HAS_NO_PUBLIC_CONSTRUCTOR
    "is private within this context|no matching function for call to" BetterCAD::features)
bettercad_compile_fail_case(prov "${_prov}" DATE_FACTORY_RETURNS_OPTIONAL
    "conversion from .*optional.* to non-scalar type|could not convert" BetterCAD::features)
bettercad_compile_fail_case(prov "${_prov}" PROVENANCE_IS_NOT_A_VALUE
    "no match for [^ ]*operator=" BetterCAD::features)
bettercad_compile_fail_case(prov "${_prov}" VALUE_IS_NOT_PROVENANCE
    "no match for [^ ]*operator=" BetterCAD::features)
bettercad_compile_fail_case(prov "${_prov}" PROVENANCE_KIND_IS_NOT_A_PROPERTY_STATE
    "cannot convert|no match for [^ ]*operator=" BetterCAD::features)
bettercad_compile_fail_case(prov "${_prov}" PROPERTY_STATE_IS_NOT_A_SOURCE_KIND
    "cannot convert|invalid conversion" BetterCAD::features)
bettercad_compile_fail_case(prov "${_prov}" ISSUE_IS_NOT_A_STRING
    "no match for [^ ]*operator==" BetterCAD::features)
bettercad_compile_fail_case(prov "${_prov}" NO_DEFAULT_DENSITY
    "defaultDensity. is not a member of" BetterCAD::features)
bettercad_compile_fail_case(prov "${_prov}" PROPERTY_HAS_NO_VALUE_OR
    "has no member named .valueOr" BetterCAD::features)

# --- Material commands (P15-CMD-001) ----------------------------------------
#
# The absences are the claims. No command has anywhere to hold a mass, an inertia or
# a completeness report, so a derived value cannot enter the undo history even by
# mistake; commands are keyed on MaterialId and never on a name, because two
# materials may share a designation; a library entry has no ObjectId so no command
# can name one; and commands are not copyable.
set(_matcmd "${CMAKE_CURRENT_SOURCE_DIR}/MaterialCommandMisuse.cpp")
bettercad_compile_fail_group(matcmd "${_matcmd}" BetterCAD::features)
bettercad_compile_fail_case(matcmd "${_matcmd}" CREATE_HAS_NO_MASS
    "has no member named .setMass" BetterCAD::features)
bettercad_compile_fail_case(matcmd "${_matcmd}" EDIT_HAS_NO_MASS
    "has no member named .setMass" BetterCAD::features)
bettercad_compile_fail_case(matcmd "${_matcmd}" EDIT_HAS_NO_INERTIA
    "has no member named .inertia" BetterCAD::features)
bettercad_compile_fail_case(matcmd "${_matcmd}" EDIT_HAS_NO_COMPLETENESS
    "has no member named .completeness" BetterCAD::features)
bettercad_compile_fail_case(matcmd "${_matcmd}" DEFINITION_HAS_NO_VOLUME
    "has no member named .volume" BetterCAD::features)
bettercad_compile_fail_case(matcmd "${_matcmd}" DELETE_TAKES_A_MATERIAL_ID
    "could not convert|cannot convert|no matching function" BetterCAD::features)
bettercad_compile_fail_case(matcmd "${_matcmd}" ASSIGN_TAKES_A_MATERIAL_ID
    "could not convert|cannot convert|no matching function" BetterCAD::features)
bettercad_compile_fail_case(matcmd "${_matcmd}" ASSIGN_TAKES_NO_NAME
    "could not convert|cannot convert|no matching function" BetterCAD::features)
bettercad_compile_fail_case(matcmd "${_matcmd}" DELETE_TAKES_NO_NAME
    "could not convert|cannot convert|no matching function" BetterCAD::features)
bettercad_compile_fail_case(matcmd "${_matcmd}" EDIT_TAKES_NO_NAME
    "could not convert|cannot convert|no matching function" BetterCAD::features)
bettercad_compile_fail_case(matcmd "${_matcmd}" DELETE_TAKES_NO_LIBRARY_ENTRY
    "could not convert|cannot convert|no matching function" BetterCAD::features)
bettercad_compile_fail_case(matcmd "${_matcmd}" EDIT_TAKES_NO_LIBRARY_ENTRY
    "could not convert|cannot convert|no matching function" BetterCAD::features)
bettercad_compile_fail_case(matcmd "${_matcmd}" COMMANDS_ARE_NOT_COPYABLE
    "use of deleted function" BetterCAD::features)
bettercad_compile_fail_case(matcmd "${_matcmd}" MATERIAL_ID_IS_NOT_AN_INT
    "could not convert|cannot convert|no matching function" BetterCAD::features)

# Mesh-local identity (P16-DATA-001, ADR-031). The central cases are the two
# directions between a mesh handle and a document identity: a mesh handle is
# derived state a remesh destroys, and ObjectId means the opposite.
set(_meshids "${CMAKE_CURRENT_SOURCE_DIR}/MeshIdsMisuse.cpp")
bettercad_compile_fail_group(meshids "${_meshids}" BetterCAD::meshing)
bettercad_compile_fail_case(meshids "${_meshids}" NODE_ID_AS_ELEMENT_ID
    "could not convert|cannot convert|no matching function" BetterCAD::meshing)
bettercad_compile_fail_case(meshids "${_meshids}" ELEMENT_ID_AS_NODE_ID
    "could not convert|cannot convert|no matching function" BetterCAD::meshing)
bettercad_compile_fail_case(meshids "${_meshids}" REGION_ID_AS_NODE_ID
    "could not convert|cannot convert|no matching function" BetterCAD::meshing)
bettercad_compile_fail_case(meshids "${_meshids}" NODE_ID_AS_REGION_ID
    "could not convert|cannot convert|no matching function" BetterCAD::meshing)
bettercad_compile_fail_case(meshids "${_meshids}" NODE_ID_AS_OBJECT_ID
    "could not convert|cannot convert|no matching function" BetterCAD::meshing)
bettercad_compile_fail_case(meshids "${_meshids}" ELEMENT_ID_AS_OBJECT_ID
    "could not convert|cannot convert|no matching function" BetterCAD::meshing)
bettercad_compile_fail_case(meshids "${_meshids}" REGION_ID_AS_OBJECT_ID
    "could not convert|cannot convert|no matching function" BetterCAD::meshing)
bettercad_compile_fail_case(meshids "${_meshids}" OBJECT_ID_AS_NODE_ID
    "could not convert|cannot convert|no matching function" BetterCAD::meshing)
bettercad_compile_fail_case(meshids "${_meshids}" SKETCH_ID_AS_NODE_ID
    "could not convert|cannot convert|no matching function" BetterCAD::meshing)
bettercad_compile_fail_case(meshids "${_meshids}" INTEGER_TO_NODE_ID
    "conversion from [^ ]*int[^ ]* to non-scalar type" BetterCAD::meshing)
bettercad_compile_fail_case(meshids "${_meshids}" NODE_ID_TO_INTEGER
    "cannot convert .*NodeId.* in initialization" BetterCAD::meshing)
bettercad_compile_fail_case(meshids "${_meshids}" COMPARE_NODE_AND_ELEMENT
    "no match for [^ ]*operator==" BetterCAD::meshing)
bettercad_compile_fail_case(meshids "${_meshids}" MUTATE_NODE_THROUGH_MESH
    "assignment of member|read-only|discards qualifiers" BetterCAD::meshing)
bettercad_compile_fail_case(meshids "${_meshids}" ADD_ELEMENT_THROUGH_MESH
    "no member named .*push_back|has no member" BetterCAD::meshing)
bettercad_compile_fail_case(meshids "${_meshids}" TRIANGLE_WITH_TET_CONNECTIVITY
    "cannot convert .*brace-enclosed initializer list.* to .*array<.*NodeId, 3>" BetterCAD::meshing)

# P16-VOL-001: a validated volume mesh cannot be fabricated.
#
# ADR-030 requires that "a mesh reaches a solver only as a ValidatedMesh, which
# only the validating path can construct. The rule is enforced by the type
# system, not by documentation." VolumeMesh is that token for volume meshes,
# and these are the proof: if any of them started compiling, possessing a
# VolumeMesh would stop being evidence that the mesh passed validation,
# conformity and volume recovery.
set(_volumemesh "${CMAKE_CURRENT_SOURCE_DIR}/VolumeMeshMisuse.cpp")
bettercad_compile_fail_group(volumemesh "${_volumemesh}" BetterCAD::meshing)
bettercad_compile_fail_case(volumemesh "${_volumemesh}" DEFAULT_CONSTRUCT_VOLUME_MESH
    "private|is private|not accessible|no matching function|deleted" BetterCAD::meshing)
bettercad_compile_fail_case(volumemesh "${_volumemesh}" VOLUME_MESH_FROM_PLAIN_MESH
    "private|is private|not accessible|no matching function|no match for|deleted" BetterCAD::meshing)
bettercad_compile_fail_case(volumemesh "${_volumemesh}" MUTATE_VOLUME_MESH
    "read-only|discards qualifiers|lvalue|not assignable" BetterCAD::meshing)

# P16-QUALITY-001: the quality layer observes, and cannot alter a mesh.
#
# The milestone's hard rule is "bad Tet -> report bad Tet", never "bad Tet ->
# move nodes -> optimise -> silently replace mesh". A before/after comparison
# shows only that today's implementation does not mutate; these show that no
# implementation behind this API could, because there is no mutable overload to
# write through and the report carries no route back to the mesh.
set(_meshquality "${CMAKE_CURRENT_SOURCE_DIR}/MeshQualityMisuse.cpp")
bettercad_compile_fail_group(meshquality "${_meshquality}" BetterCAD::meshing)
bettercad_compile_fail_case(meshquality "${_meshquality}"
    EVALUATE_MESH_THROUGH_A_MUTABLE_REFERENCE
    "cannot convert|could not convert|invalid conversion|no matches converting"
    BetterCAD::meshing)
bettercad_compile_fail_case(meshquality "${_meshquality}"
    EVALUATE_TET_THROUGH_A_MUTABLE_REFERENCE
    "cannot convert|could not convert|invalid conversion|no matches converting"
    BetterCAD::meshing)
bettercad_compile_fail_case(meshquality "${_meshquality}"
    REACH_THE_MESH_THROUGH_THE_REPORT
    "has no member named|no member named" BetterCAD::meshing)
bettercad_compile_fail_case(meshquality "${_meshquality}"
    REPAIRED_MESH_FROM_A_REPORT
    "conversion from .* to non-scalar type|cannot convert|could not convert|no matching function"
    BetterCAD::meshing)

# P16-MAP-001: correspondence observes, and cannot be fabricated.
#
# A mapping answers questions about a geometry and a mesh. It must not be able
# to heal CAD, regenerate a feature or move a node -- and possessing a
# GeometryMeshMap must stay evidence that a geometry and a mesh were checked to
# be the same pair (ADR-030), which a default-constructed one would not be.
set(_geometrymeshmap "${CMAKE_CURRENT_SOURCE_DIR}/GeometryMeshMapMisuse.cpp")
bettercad_compile_fail_group(geometrymeshmap "${_geometrymeshmap}" BetterCAD::meshing)
bettercad_compile_fail_case(geometrymeshmap "${_geometrymeshmap}"
    BUILD_MAP_FROM_A_MUTABLE_GEOMETRY
    "cannot convert|could not convert|invalid conversion|no matches converting"
    BetterCAD::meshing)
bettercad_compile_fail_case(geometrymeshmap "${_geometrymeshmap}"
    QUERY_THROUGH_A_MUTABLE_MESH
    "cannot convert|could not convert|invalid conversion|no matches converting"
    BetterCAD::meshing)
bettercad_compile_fail_case(geometrymeshmap "${_geometrymeshmap}"
    FABRICATE_A_MAP
    "private|is private|not accessible|no matching function|deleted" BetterCAD::meshing)
bettercad_compile_fail_case(geometrymeshmap "${_geometrymeshmap}"
    WRITE_THROUGH_A_MAPPED_FACE
    "read-only|discards qualifiers|no member named clear|assignment of member" BetterCAD::meshing)
bettercad_compile_fail_case(geometrymeshmap "${_geometrymeshmap}"
    REACH_THE_MESH_THROUGH_THE_MAP
    "has no member named|no member named" BetterCAD::meshing)

# INFRA-VIEWER-001: the viewer owns no engineering state.
#
# A GUI is the most likely place for an engineering invariant to leak, so these
# pin it at compile time: the viewer cannot take a mutable body, its own
# presentation handles cannot be confused with document identities or invented
# from integers, a viewer cannot be copied, and there is no Document behind it
# to reach through.
set(_viewer "${CMAKE_CURRENT_SOURCE_DIR}/ViewerMisuse.cpp")
bettercad_compile_fail_group(viewer "${_viewer}" BetterCAD::renderer BetterCAD::geometry)
bettercad_compile_fail_case(viewer "${_viewer}" DISPLAY_THROUGH_A_MUTABLE_BODY
    "cannot convert|could not convert|invalid conversion|no matches converting"
    BetterCAD::renderer BetterCAD::geometry)
bettercad_compile_fail_case(viewer "${_viewer}" PRESENTATION_ID_AS_OBJECT_ID
    "conversion from .* to non-scalar type|could not convert|cannot convert"
    BetterCAD::renderer BetterCAD::geometry)
bettercad_compile_fail_case(viewer "${_viewer}" OBJECT_ID_AS_PRESENTATION_ID
    "conversion from .* to non-scalar type|could not convert|cannot convert"
    BetterCAD::renderer BetterCAD::geometry)
bettercad_compile_fail_case(viewer "${_viewer}" PRESENTATION_ID_FROM_INTEGER
    "conversion from [^ ]*int[^ ]* to non-scalar type|could not convert|cannot convert"
    BetterCAD::renderer BetterCAD::geometry)
bettercad_compile_fail_case(viewer "${_viewer}" COPY_A_VIEWER
    "use of deleted function|is private|deleted" BetterCAD::renderer BetterCAD::geometry)
bettercad_compile_fail_case(viewer "${_viewer}" REACH_A_DOCUMENT_THROUGH_THE_VIEWER
    "has no member named|no member named" BetterCAD::renderer BetterCAD::geometry)

# The mesh visualisation adapter (P16-VIZ-001). A render index is a position in
# a GPU buffer; a NodeId and an ElementId are engineering identities. The
# confusions below are not caught at runtime -- they cannot be written.
set(_meshview "${CMAKE_CURRENT_SOURCE_DIR}/MeshViewMisuse.cpp")
bettercad_compile_fail_group(meshview "${_meshview}" BetterCAD::renderer BetterCAD::meshing)
bettercad_compile_fail_case(meshview "${_meshview}" NODE_ID_FROM_A_RENDER_INDEX
    "conversion from .* to non-scalar type|could not convert|cannot convert"
    BetterCAD::renderer BetterCAD::meshing)
bettercad_compile_fail_case(meshview "${_meshview}" ELEMENT_ID_FROM_A_RENDER_INDEX
    "conversion from .* to non-scalar type|could not convert|cannot convert"
    BetterCAD::renderer BetterCAD::meshing)
bettercad_compile_fail_case(meshview "${_meshview}" RENDER_INDEX_FROM_A_NODE_ID
    "cannot convert|could not convert|invalid conversion|no matches converting"
    BetterCAD::renderer BetterCAD::meshing)
bettercad_compile_fail_case(meshview "${_meshview}" MUTATE_A_MESH
    "has no member named|no member named" BetterCAD::renderer BetterCAD::meshing)
bettercad_compile_fail_case(meshview "${_meshview}" REACH_A_MESH_THROUGH_A_VIEW
    "has no member named|no member named" BetterCAD::renderer BetterCAD::meshing)

# P16-CMD-001: a meshing command carries canonical intent and nothing derived.
# A generated mesh cannot even be default-constructed, so it cannot become a
# member of what a command copies; canonical intent has no node array, element
# array, quality report or facet set; local sizing is keyed on a FaceName and
# never on a mesh-local identity or a bare ObjectId; commands are typed on
# MeshControlId; commands are not copyable; and the mesher does not hand out a
# mutable mesh.
set(_meshcmd "${CMAKE_CURRENT_SOURCE_DIR}/MeshingCommandMisuse.cpp")
bettercad_compile_fail_group(meshcmd "${_meshcmd}" BetterCAD::meshing BetterCAD::features)
bettercad_compile_fail_case(meshcmd "${_meshcmd}" MESH_IS_NOT_DEFAULT_CONSTRUCTIBLE
    "VolumeMesh.* is private" BetterCAD::meshing BetterCAD::features)
bettercad_compile_fail_case(meshcmd "${_meshcmd}" DEFINITION_HAS_NO_NODES
    "no member named .nodes" BetterCAD::meshing BetterCAD::features)
bettercad_compile_fail_case(meshcmd "${_meshcmd}" DEFINITION_HAS_NO_ELEMENTS
    "no member named .elements" BetterCAD::meshing BetterCAD::features)
bettercad_compile_fail_case(meshcmd "${_meshcmd}" DEFINITION_HAS_NO_QUALITY_REPORT
    "no member named .elements" BetterCAD::meshing BetterCAD::features)
bettercad_compile_fail_case(meshcmd "${_meshcmd}" DEFINITION_HAS_NO_GENERATED_MESH
    "no member named .tetrahedra" BetterCAD::meshing BetterCAD::features)
bettercad_compile_fail_case(meshcmd "${_meshcmd}" LOCAL_SIZING_IS_NOT_KEYED_ON_A_NODE
    "could not convert" BetterCAD::meshing BetterCAD::features)
bettercad_compile_fail_case(meshcmd "${_meshcmd}" LOCAL_SIZING_IS_NOT_KEYED_ON_AN_ELEMENT
    "could not convert" BetterCAD::meshing BetterCAD::features)
bettercad_compile_fail_case(meshcmd "${_meshcmd}" A_COMMAND_WILL_NOT_TAKE_A_FEATURE_FOR_A_FACE
    "no matching function" BetterCAD::meshing BetterCAD::features)
bettercad_compile_fail_case(meshcmd "${_meshcmd}" BOUNDARY_SET_HAS_NO_FACETS
    "no member named .facets" BetterCAD::meshing BetterCAD::features)
bettercad_compile_fail_case(meshcmd "${_meshcmd}" COMMAND_IS_KEYED_ON_A_MESH_CONTROL_ID
    "no matching function" BetterCAD::meshing BetterCAD::features)
bettercad_compile_fail_case(meshcmd "${_meshcmd}" COMMAND_HAS_NO_MESH_SETTER
    "no member named .setMesh" BetterCAD::meshing BetterCAD::features)
bettercad_compile_fail_case(meshcmd "${_meshcmd}" COMMAND_IS_NOT_COPYABLE
    "use of deleted function" BetterCAD::meshing BetterCAD::features)
bettercad_compile_fail_case(meshcmd "${_meshcmd}" MESHER_DOES_NOT_HAND_OUT_A_MUTABLE_MESH
    "invalid conversion" BetterCAD::meshing BetterCAD::features)

# Structural identity (P17-DATA-001). Three domains that must not converge:
# document identities that are persisted and survive a remesh; mesh-local
# handles that do not; and a solver-local DOF index that is meaningful only
# within one numbering of one mesh. The central cases are the ones a real
# mistake would produce -- a load keyed on a node, a restraint keyed on an
# equation -- and the one widening the model permits, AnalysisId to ObjectId,
# is exercised by the control rather than by a case.
set(_structids "${CMAKE_CURRENT_SOURCE_DIR}/StructuralIdMisuse.cpp")
bettercad_compile_fail_group(structids "${_structids}" BetterCAD::structural)
bettercad_compile_fail_case(structids "${_structids}" ANALYSIS_ID_AS_LOAD_ID
    "could not convert|cannot convert|no matching function" BetterCAD::structural)
bettercad_compile_fail_case(structids "${_structids}" LOAD_ID_AS_RESTRAINT_ID
    "could not convert|cannot convert|no matching function" BetterCAD::structural)
bettercad_compile_fail_case(structids "${_structids}" RESTRAINT_ID_AS_LOAD_ID
    "could not convert|cannot convert|no matching function" BetterCAD::structural)
bettercad_compile_fail_case(structids "${_structids}" LOAD_ID_AS_OBJECT_ID
    "could not convert|cannot convert|no matching function" BetterCAD::structural)
bettercad_compile_fail_case(structids "${_structids}" RESTRAINT_ID_AS_OBJECT_ID
    "could not convert|cannot convert|no matching function" BetterCAD::structural)
bettercad_compile_fail_case(structids "${_structids}" DOF_INDEX_AS_OBJECT_ID
    "could not convert|cannot convert|no matching function" BetterCAD::structural)
bettercad_compile_fail_case(structids "${_structids}" DOF_INDEX_AS_NODE_ID
    "could not convert|cannot convert|no matching function" BetterCAD::structural)
bettercad_compile_fail_case(structids "${_structids}" NODE_ID_AS_DOF_INDEX
    "could not convert|cannot convert|no matching function" BetterCAD::structural)
bettercad_compile_fail_case(structids "${_structids}" DOF_INDEX_AS_INTEGER
    "cannot convert .*DofIndex.*" BetterCAD::structural)
bettercad_compile_fail_case(structids "${_structids}" INTEGER_AS_LOAD_ID
    "conversion from [^ ]*int[^ ]* to non-scalar type|could not convert" BetterCAD::structural)
bettercad_compile_fail_case(structids "${_structids}" NODE_ID_AS_LOAD_ID
    "could not convert|cannot convert|no matching function" BetterCAD::structural)
bettercad_compile_fail_case(structids "${_structids}" NODE_ID_AS_RESTRAINT_ID
    "could not convert|cannot convert|no matching function" BetterCAD::structural)

# P17-DOF-001: the solver-local index space. A DofIndex names a degree of
# freedom of the model and a FreeEquationIndex a row of the REDUCED system, and
# the two agree only for a model with no restraints -- so a test on a
# free-floating body would not catch the confusion and the compiler has to. The
# last three cases enforce the other half of the milestone: a numbering, a
# constraint set and a free-equation map have one friend each, so possessing one
# is evidence it came from a real mesh (ADR-036).
set(_structdof "${CMAKE_CURRENT_SOURCE_DIR}/StructuralDofMisuse.cpp")
bettercad_compile_fail_group(structdof "${_structdof}" BetterCAD::structural)
bettercad_compile_fail_case(structdof "${_structdof}" FREE_EQUATION_AS_DOF_INDEX
    "could not convert|cannot convert|no matching function" BetterCAD::structural)
bettercad_compile_fail_case(structdof "${_structdof}" DOF_INDEX_AS_FREE_EQUATION
    "could not convert|cannot convert|no matching function" BetterCAD::structural)
bettercad_compile_fail_case(structdof "${_structdof}" FREE_EQUATION_FROM_INTEGER
    "could not convert|cannot convert|no matching function" BetterCAD::structural)
bettercad_compile_fail_case(structdof "${_structdof}" FREE_EQUATION_AS_INTEGER
    "cannot convert .*FreeEquationIndex.*" BetterCAD::structural)
bettercad_compile_fail_case(structdof "${_structdof}" FREE_EQUATION_AS_NODE_ID
    "could not convert|cannot convert|no matching function" BetterCAD::structural)
bettercad_compile_fail_case(structdof "${_structdof}" FREE_EQUATION_DEFAULT_CONSTRUCTED
    "use of deleted function|deleted" BetterCAD::structural)
bettercad_compile_fail_case(structdof "${_structdof}" MESH_DOF_MAP_CONSTRUCTED_DIRECTLY
    "is private within this context|private" BetterCAD::structural)
bettercad_compile_fail_case(structdof "${_structdof}" CONSTRAINT_SET_CONSTRUCTED_DIRECTLY
    "is private within this context|private" BetterCAD::structural)
bettercad_compile_fail_case(structdof "${_structdof}" FREE_EQUATION_MAP_CONSTRUCTED_DIRECTLY
    "is private within this context|private" BetterCAD::structural)

# P17-BC-001: the restraint model. A canonical restraint is an identity, a CAD
# target and a component mask, and the facets, nodes and indices it resolves to
# are rebuilt for whatever mesh is current. A restraint carrying a mesh handle
# would survive a remesh naming material that had moved, which a size assertion
# in the suite cannot catch for an attempted CONVERSION -- so the compiler
# refuses. The mask is a strong type for the same reason a DofIndex is: a
# 1|2|4 bitfield would be interchangeable with a count, an offset and a DOF
# index. And `PreparedRestraints` has one friend, so possessing one is evidence
# that every target resolved and the numbering describes this mesh (ADR-036).
set(_structbc "${CMAKE_CURRENT_SOURCE_DIR}/StructuralRestraintMisuse.cpp")
bettercad_compile_fail_group(structbc "${_structbc}" BetterCAD::structural)
bettercad_compile_fail_case(structbc "${_structbc}" RESTRAINT_FROM_NODE_ID
    "could not convert|cannot convert|no matching function|no match for" BetterCAD::structural)
bettercad_compile_fail_case(structbc "${_structbc}" RESTRAINT_FROM_DOF_INDEX
    "could not convert|cannot convert|no matching function|no match for" BetterCAD::structural)
bettercad_compile_fail_case(structbc "${_structbc}" COMPONENTS_FROM_INTEGER
    "could not convert|cannot convert|no matching function|invalid conversion" BetterCAD::structural)
bettercad_compile_fail_case(structbc "${_structbc}" COMPONENTS_AS_INTEGER
    "cannot convert .*RestraintComponents.*|could not convert" BetterCAD::structural)
bettercad_compile_fail_case(structbc "${_structbc}" COMPONENTS_FROM_DISPLACEMENT
    "could not convert|cannot convert|no matching function|invalid conversion" BetterCAD::structural)
bettercad_compile_fail_case(structbc "${_structbc}" PREPARED_RESTRAINTS_CONSTRUCTED_DIRECTLY
    "is private within this context|private|no matching function" BetterCAD::structural)

# P17-ASSEMBLY-001: the global system. A hand-built K would defeat every check
# in the module -- the dimensions against a real numbering, every element's
# acceptance by P17-ELEM, every load's mesh, and the finiteness scan -- so the
# matrix, the vector and the system each have one friend (ADR-036). And `K u =
# F` is dimensional: a stiffness is N/m and a force is N, neither converts to
# the other, and neither decays to a double. `values()` is the one deliberate
# SI escape hatch, for a solver that maps the buffer.
set(_structasm "${CMAKE_CURRENT_SOURCE_DIR}/StructuralSystemMisuse.cpp")
bettercad_compile_fail_group(structasm "${_structasm}" BetterCAD::structural)
bettercad_compile_fail_case(structasm "${_structasm}" STIFFNESS_MATRIX_CONSTRUCTED_DIRECTLY
    "is private within this context|private|no matching function" BetterCAD::structural)
bettercad_compile_fail_case(structasm "${_structasm}" FORCE_VECTOR_CONSTRUCTED_DIRECTLY
    "is private within this context|private|no matching function" BetterCAD::structural)
bettercad_compile_fail_case(structasm "${_structasm}" GLOBAL_SYSTEM_CONSTRUCTED_DIRECTLY
    "is private within this context|private|no matching function" BetterCAD::structural)
bettercad_compile_fail_case(structasm "${_structasm}" STIFFNESS_AS_FORCE
    "could not convert|cannot convert|no matching function|invalid conversion" BetterCAD::structural)
bettercad_compile_fail_case(structasm "${_structasm}" FORCE_AS_STIFFNESS
    "could not convert|cannot convert|no matching function|invalid conversion" BetterCAD::structural)
bettercad_compile_fail_case(structasm "${_structasm}" STIFFNESS_AS_DOUBLE
    "cannot convert .*Quantity.*|could not convert|invalid conversion" BetterCAD::structural)

# P17-SOLVE-001: the linear static solve. A hand-built SolvedSystem would be a
# displacement field nobody validated -- not the mesh pairing, not the pivot
# ratio, not finiteness, not the independent residual -- so the reduced system
# and the solution each have one friend (ADR-036, ADR-039). The settings of a
# DIRECT solver have no iteration count and no preconditioner, and referring to
# one must not compile: a field that existed but did nothing would be a false
# capability. And a residual norm is a Force, not a Length and not a double,
# because `K u = F` is dimensional.
set(_structsolve "${CMAKE_CURRENT_SOURCE_DIR}/StructuralSolveMisuse.cpp")
bettercad_compile_fail_group(structsolve "${_structsolve}" BetterCAD::structural)
bettercad_compile_fail_case(structsolve "${_structsolve}" FREE_SYSTEM_CONSTRUCTED_DIRECTLY
    "is private within this context|private|no matching function" BetterCAD::structural)
bettercad_compile_fail_case(structsolve "${_structsolve}" SOLVED_SYSTEM_CONSTRUCTED_DIRECTLY
    "is private within this context|private|no matching function" BetterCAD::structural)
bettercad_compile_fail_case(structsolve "${_structsolve}" SETTINGS_HAVE_MAXIMUM_ITERATIONS
    "has no member named|no member" BetterCAD::structural)
bettercad_compile_fail_case(structsolve "${_structsolve}" SETTINGS_HAVE_PRECONDITIONER
    "has no member named|no member" BetterCAD::structural)
bettercad_compile_fail_case(structsolve "${_structsolve}" RESIDUAL_NORM_AS_LENGTH
    "could not convert|cannot convert|no matching function|invalid conversion" BetterCAD::structural)
bettercad_compile_fail_case(structsolve "${_structsolve}" RESIDUAL_NORM_AS_DOUBLE
    "cannot convert .*Quantity.*|could not convert|invalid conversion" BetterCAD::structural)

# P17-POST-001. `RecoveredFields` cannot be conjured: it has a private
# constructor and one friend, so possession proves the four sources agreed and
# every value is finite -- and that its source stamp was checked rather than
# assigned, which is how a stale result would otherwise be made to look
# current. Stress is a Pressure and strain is a ratio, so neither converts to
# the other nor to a bare double; a displacement magnitude is a Length, so an
# MPa/mm leak cannot enter the core unnoticed. And a `Strain6` (ENGINEERING
# shear) is not a `StrainTensor3` (TENSOR shear, gamma/2), which is the
# convention error the whole milestone is built against.
set(_structpost "${CMAKE_CURRENT_SOURCE_DIR}/StructuralPostMisuse.cpp")
bettercad_compile_fail_group(structpost "${_structpost}" BetterCAD::structural)
bettercad_compile_fail_case(structpost "${_structpost}" RECOVERED_FIELDS_CONSTRUCTED_DIRECTLY
    "is private within this context|private|no matching function" BetterCAD::structural)
bettercad_compile_fail_case(structpost "${_structpost}" VON_MISES_AS_DOUBLE
    "cannot convert .*Quantity.*|could not convert|invalid conversion" BetterCAD::structural)
bettercad_compile_fail_case(structpost "${_structpost}" PRINCIPAL_STRAIN_AS_STRESS
    "could not convert|cannot convert|no matching function|invalid conversion" BetterCAD::structural)
bettercad_compile_fail_case(structpost "${_structpost}" DISPLACEMENT_MAGNITUDE_AS_DOUBLE
    "cannot convert .*Quantity.*|could not convert|invalid conversion" BetterCAD::structural)
bettercad_compile_fail_case(structpost "${_structpost}" STRAIN6_AS_STRAIN_TENSOR
    "invalid initialization of reference|could not convert|cannot convert|no matching function|invalid conversion" BetterCAD::structural)
bettercad_compile_fail_case(structpost "${_structpost}" STRESS_TENSOR_AS_STRAIN_TENSOR
    "invalid initialization of reference|could not convert|cannot convert|no matching function|invalid conversion" BetterCAD::structural)

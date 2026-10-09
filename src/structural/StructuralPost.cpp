// Displacement, strain, stress and the derived invariants (P17-POST-001).
//
// THERE IS NO `B` AND NO `D` IN THIS FILE. Both are P17-ELEM-001's and both
// are reached through the two functions that header made public for this
// milestone by name:
//
//     Tet4Kinematics::strainFrom(u_e)      eps   = B u_e
//     ElasticityMatrix::stressFrom(eps)    sigma = D eps
//
// So there is no shape-function gradient here, no `1/(6V)`, no transpose, no
// `lambda = E nu / ((1+nu)(1-2nu))` and no `mu = E / (2(1+nu))`. A second
// implementation of either would be free to drift from the qualified one, and
// the drift that matters -- a tensor-shear D against an engineering-shear B --
// leaves the normal terms right and halves or doubles every shear.
//
// EIGEN IS USED FOR THE 3x3 SYMMETRIC EIGENVALUES AND NOTHING ELSE. ADR-039
// admitted it PRIVATE to this module and ADR-040 chose
// SelfAdjointEigenSolver's default path over computeDirect() -- faster and
// less accurate, by Eigen's own description, on no hot path. The ORDER is
// BetterCAD's: the three values are sorted descending afterwards, so no
// behaviour depends on the library's output order.
//
// NO SOLVE, NO REACTIONS, NO SMOOTHING, NO MUTATION. Every input is a const
// reference; nothing is written through one.

#include <bettercad/structural/StructuralPost.hpp>

#include <Eigen/Eigenvalues>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <utility>

namespace bettercad::structural {

std::string_view toString(RecoveryProblem problem) noexcept {
    switch (problem) {
    case RecoveryProblem::SolutionSourceMismatch:
        return "solution_source_mismatch";
    case RecoveryProblem::MeshMismatch:
        return "mesh_mismatch";
    case RecoveryProblem::MaterialSourceMismatch:
        return "material_source_mismatch";
    case RecoveryProblem::MeshHasNoElements:
        return "mesh_has_no_elements";
    case RecoveryProblem::ElementNodeMissing:
        return "element_node_missing";
    case RecoveryProblem::DegreeOfFreedomMissing:
        return "degree_of_freedom_missing";
    case RecoveryProblem::ElementRejected:
        return "element_rejected";
    case RecoveryProblem::InvalidMaterial:
        return "invalid_material";
    case RecoveryProblem::NonFiniteDisplacement:
        return "non_finite_displacement";
    case RecoveryProblem::NonFiniteStrain:
        return "non_finite_strain";
    case RecoveryProblem::NonFiniteStress:
        return "non_finite_stress";
    case RecoveryProblem::PrincipalValueFailure:
        return "principal_value_failure";
    case RecoveryProblem::NonFiniteDerivedResult:
        return "non_finite_derived_result";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
// Displacement magnitude
// ---------------------------------------------------------------------------

Length displacementMagnitude(const Translation3D& displacement) noexcept {
    // std::hypot, THE THREE-ARGUMENT OVERLOAD, which is what Point3D::distance
    // already uses. Correctly rounded, and no intermediate square to overflow.
    return Length::fromSi(
        std::hypot(displacement.x.si(), displacement.y.si(), displacement.z.si()));
}

// ---------------------------------------------------------------------------
// von Mises
// ---------------------------------------------------------------------------

Stress vonMisesStress(const Stress6& stress) noexcept {
    const double sxx = stress.xx.si();
    const double syy = stress.yy.si();
    const double szz = stress.zz.si();
    const double txy = stress.xy.si();
    const double tyz = stress.yz.si();
    const double tzx = stress.zx.si();

    // ALL SIX COMPONENTS. szz appears in two of the three differences and tyz
    // and tzx in the shear sum; a plane-stress formula drops all three and
    // still agrees on every uniaxial and pure-XY-shear state.
    const double dxy = sxx - syy;
    const double dyz = syy - szz;
    const double dzx = szz - sxx;

    const double radicand =
        0.5 * (dxy * dxy + dyz * dyz + dzx * dzx) + 3.0 * (txy * txy + tyz * tyz + tzx * tzx);

    // A SUM OF SQUARES WITH POSITIVE COEFFICIENTS, so it cannot be negative
    // for finite input. There is no std::abs and no clamp: a repaired radicand
    // would hide a formula error rather than a rounding one.
    return Stress::fromSi(std::sqrt(radicand));
}

// ---------------------------------------------------------------------------
// Principal values
// ---------------------------------------------------------------------------

namespace {

/// The three eigenvalues of a symmetric 3x3, descending, or a failure.
///
/// ONE EIGENSOLVE IN THIS FILE, shared by stress and strain, so the two cannot
/// use different paths or different orderings. @p what names the quantity in
/// the diagnostic, which is the only thing that differs.
[[nodiscard]] Result<std::array<double, 3>> descendingEigenvalues(const Eigen::Matrix3d& tensor,
                                                                  std::string_view what) {
    // THE DEFAULT PATH, NOT computeDirect(). ADR-040.
    const Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> solver(tensor);
    if (solver.info() != Eigen::Success) {
        return makeError(ErrorCode::Internal,
                         std::format("the symmetric eigensolve of the {} tensor did not converge, "
                                     "so there are no principal values to report",
                                     what));
    }

    const Eigen::Vector3d values = solver.eigenvalues();
    std::array<double, 3> out{values(0), values(1), values(2)};
    for (const double value : out) {
        if (!std::isfinite(value)) {
            // A FINITE SYMMETRIC TENSOR CANNOT PRODUCE ONE, so this is a
            // result check rather than an input tolerance -- the same
            // distinction P17-ELEM draws for NonFiniteResult.
            return makeError(ErrorCode::Internal,
                             std::format("the symmetric eigensolve of the {} tensor produced a "
                                         "non-finite principal value",
                                         what));
        }
    }

    // BETTERCAD'S ORDER. Eigen documents ascending eigenvalues and that is not
    // relied on: sorting descending here means no behaviour depends on the
    // library's output order, and a probe that reverses the comparator is
    // killed rather than invisible.
    std::ranges::sort(out, std::ranges::greater{});
    return out;
}

} // namespace

Result<PrincipalStresses> principalStresses(const Stress6& stress) {
    // THROUGH tensorOf, which is the one place the Voigt-to-tensor mapping is
    // written (ADR-040). Nothing here indexes a component by hand.
    const StressTensor3 tensor = tensorOf(stress);

    Eigen::Matrix3d matrix;
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            matrix(static_cast<Eigen::Index>(row), static_cast<Eigen::Index>(column)) =
                tensor.at(row, column).si();
        }
    }

    Result<std::array<double, 3>> values = descendingEigenvalues(matrix, "stress");
    if (!values.has_value()) {
        return std::unexpected(values.error());
    }
    return PrincipalStresses{.sigma1 = Stress::fromSi((*values)[0]),
                             .sigma2 = Stress::fromSi((*values)[1]),
                             .sigma3 = Stress::fromSi((*values)[2])};
}

Result<PrincipalStrains> principalStrains(const Strain6& strain) {
    // THE gamma/2 IS APPLIED BY tensorOf AND NOWHERE ELSE. Diagonalising a
    // matrix carrying full engineering shear in its off-diagonals is the
    // convention error this whole type exists to prevent.
    const StrainTensor3 tensor = tensorOf(strain);

    Eigen::Matrix3d matrix;
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            matrix(static_cast<Eigen::Index>(row), static_cast<Eigen::Index>(column)) =
                tensor.at(row, column);
        }
    }

    Result<std::array<double, 3>> values = descendingEigenvalues(matrix, "strain");
    if (!values.has_value()) {
        return std::unexpected(values.error());
    }
    return PrincipalStrains{.e1 = (*values)[0], .e2 = (*values)[1], .e3 = (*values)[2]};
}

// ---------------------------------------------------------------------------
// Element displacement gather
// ---------------------------------------------------------------------------

Result<std::array<Translation3D, kTet4Nodes>>
elementDisplacements(const MeshDofMap& numbering, const SolvedSystem& solution,
                     const std::array<meshing::NodeId, kTet4Nodes>& nodes) {
    // THE TWELVE GLOBAL INDICES COME FROM P17-ASSEMBLY'S SHARED MAPPER, which
    // itself goes through P17-DOF's indicesOf. No `3 * node`, no `dof % 3`,
    // and no second interleaving convention.
    Result<std::array<DofIndex, kTet4Dofs>> dofs = elementDegreesOfFreedom(numbering, nodes);
    if (!dofs.has_value()) {
        return std::unexpected(dofs.error());
    }

    std::array<Translation3D, kTet4Nodes> out{};
    for (std::size_t node = 0; node < kTet4Nodes; ++node) {
        // localDofIndex IS P17-ELEM'S OWN LOOKUP, so the order of u_e is the
        // order B's columns are in by construction rather than by agreement.
        const DofIndex x = (*dofs)[localDofIndex(node, DofComponent::Ux)];
        const DofIndex y = (*dofs)[localDofIndex(node, DofComponent::Uy)];
        const DofIndex z = (*dofs)[localDofIndex(node, DofComponent::Uz)];
        if (!numbering.contains(x) || !numbering.contains(y) || !numbering.contains(z)) {
            return makeError(ErrorCode::Internal,
                             std::format("{} has a degree of freedom outside the numbering, so its "
                                         "displacement cannot be gathered",
                                         nodes[node]));
        }
        out[node] = Translation3D{solution.displacementOf(x), solution.displacementOf(y),
                                  solution.displacementOf(z)};
    }
    return out;
}

// ---------------------------------------------------------------------------
// The per-element kernel
// ---------------------------------------------------------------------------

namespace {

/// One element's recovery, shared by `recoverElementFields`,
/// `elementRecoveryProblem` and the traversal in `run`, so no two of them can
/// drift apart. @p element names the tetrahedron in the diagnostics and is
/// otherwise unused -- a kernel caller with no mesh passes an invalid handle.
struct ElementOutcome {
    std::optional<RecoveryProblem> problem{};
    Error error{};
    ElementFields fields{};
};

[[nodiscard]] ElementOutcome elementFail(RecoveryProblem problem, Error error) {
    return ElementOutcome{.problem = problem, .error = std::move(error)};
}

/// Names @p element in a message when it is a real handle, and says "the
/// element" when the caller had none. So the kernel's diagnostics read
/// correctly from both entry points without a second message table.
[[nodiscard]] std::string named(meshing::ElementId element) {
    return element.isValid() ? std::format("element {}", element) : std::string{"the element"};
}

[[nodiscard]] ElementOutcome
recoverOneElement(meshing::ElementId element, const std::array<Point3D, kTet4Nodes>& corners,
                  const std::array<Translation3D, kTet4Nodes>& displacements,
                  const ElasticityMatrix& elasticity) {
    // FINITENESS FIRST, AND ON THE INPUT. A NaN here is reported as a
    // non-finite displacement rather than surviving to become a non-finite
    // strain or an eigensolver failure three steps later, which would lose the
    // root cause (brief section 58).
    for (std::size_t corner = 0; corner < kTet4Nodes; ++corner) {
        if (!isFinite(displacements[corner])) {
            return elementFail(
                RecoveryProblem::NonFiniteDisplacement,
                makeError(ErrorCode::Internal,
                          std::format("{}: the displacement of its corner {} is not finite, so no "
                                      "field can be recovered from it",
                                      named(element), corner))
                    .error());
        }
    }

    // B IS P17-ELEM'S, AND A REFUSAL IS PROPAGATED. There is no second
    // determinant or orientation test here: an inverted or degenerate element
    // is refused by the shared kinematics, which is the only place that rule
    // lives.
    Result<Tet4Kinematics> kinematics = computeTet4Kinematics(corners);
    if (!kinematics.has_value()) {
        return elementFail(RecoveryProblem::ElementRejected,
                           makeError(kinematics.error().code,
                                     std::format("{}: {}", named(element),
                                                 kinematics.error().message))
                               .error());
    }

    ElementOutcome out;
    out.fields.element = element;

    // eps = B u_e, THROUGH THE SHARED PATH.
    out.fields.strain = kinematics->strainFrom(displacements);
    if (!isFinite(out.fields.strain)) {
        return elementFail(RecoveryProblem::NonFiniteStrain,
                           makeError(ErrorCode::Internal,
                                     std::format("the recovered strain of {} is not finite",
                                                 named(element)))
                               .error());
    }

    // sigma = D eps, THROUGH THE SHARED PATH.
    out.fields.stress = elasticity.stressFrom(out.fields.strain);
    if (!isFinite(out.fields.stress)) {
        return elementFail(RecoveryProblem::NonFiniteStress,
                           makeError(ErrorCode::Internal,
                                     std::format("the recovered stress of {} is not finite",
                                                 named(element)))
                               .error());
    }

    Result<PrincipalStresses> principalStress = principalStresses(out.fields.stress);
    if (!principalStress.has_value()) {
        return elementFail(RecoveryProblem::PrincipalValueFailure,
                           makeError(principalStress.error().code,
                                     std::format("{}: {}", named(element),
                                                 principalStress.error().message))
                               .error());
    }
    out.fields.principalStress = *principalStress;

    Result<PrincipalStrains> principalStrain = principalStrains(out.fields.strain);
    if (!principalStrain.has_value()) {
        return elementFail(RecoveryProblem::PrincipalValueFailure,
                           makeError(principalStrain.error().code,
                                     std::format("{}: {}", named(element),
                                                 principalStrain.error().message))
                               .error());
    }
    out.fields.principalStrain = *principalStrain;

    out.fields.vonMises = vonMisesStress(out.fields.stress);
    out.fields.hydrostatic = hydrostaticStress(out.fields.stress);
    if (!isFinite(out.fields.vonMises) || !isFinite(out.fields.hydrostatic)) {
        return elementFail(RecoveryProblem::NonFiniteDerivedResult,
                           makeError(ErrorCode::Internal,
                                     std::format("a derived scalar of {} is not finite although "
                                                 "its stress is",
                                                 named(element)))
                               .error());
    }
    return out;
}

} // namespace

Result<ElementFields> recoverElementFields(meshing::ElementId element,
                                           const std::array<Point3D, kTet4Nodes>& corners,
                                           const std::array<Translation3D, kTet4Nodes>& displacements,
                                           const ElasticityMatrix& elasticity) {
    ElementOutcome outcome = recoverOneElement(element, corners, displacements, elasticity);
    if (outcome.problem.has_value()) {
        return std::unexpected(std::move(outcome.error));
    }
    return outcome.fields;
}

std::optional<RecoveryProblem>
elementRecoveryProblem(const std::array<Point3D, kTet4Nodes>& corners,
                       const std::array<Translation3D, kTet4Nodes>& displacements,
                       const ElasticityMatrix& elasticity) {
    return recoverOneElement(meshing::ElementId{}, corners, displacements, elasticity).problem;
}

// ---------------------------------------------------------------------------
// Recovery
// ---------------------------------------------------------------------------

namespace {

/// The working state of one recovery, and the only place a partially recovered
/// set exists. Nothing escapes until every check has passed.
struct Working {
    std::optional<RecoveryProblem> problem{};
    Error error{};

    Length largestDisplacement{};
    Stress largestVonMises{};
    std::vector<NodalDisplacement> displacements{};
    std::vector<ElementFields> elements{};
};

[[nodiscard]] Working fail(RecoveryProblem problem, Error error) {
    return Working{.problem = problem, .error = std::move(error)};
}

[[nodiscard]] Working fail(RecoveryProblem problem, std::unexpected<Error> error) {
    return Working{.problem = problem, .error = std::move(error.error())};
}

/// One recovery, shared by `recoverFields` and `fieldRecoveryProblem` so the
/// two cannot drift apart.
[[nodiscard]] Working run(const StructuralModel& model, const StructuralMaterial& material,
                          const GlobalStructuralSystem& system, const SolvedSystem& solution) {
    const meshing::Mesh& mesh = model.mesh().mesh();
    const MeshDofMap& numbering = system.numbering();

    // -----------------------------------------------------------------------
    // SOURCE COMPATIBILITY, BEFORE ANY ARITHMETIC.
    // -----------------------------------------------------------------------

    // THE SOLUTION MUST BE THIS SYSTEM'S. A field-complete AssemblySource
    // comparison, so a move of any of the six dependencies is caught -- and
    // the comparison is `= default`, so a seventh field could not be forgotten.
    if (!(solution.source() == system.source())) {
        return fail(RecoveryProblem::SolutionSourceMismatch,
                    makeError(ErrorCode::FailedPrecondition,
                              "the solved displacement field was produced from a different "
                              "assembled system than the one handed in, so its values do not "
                              "belong to this stiffness, mesh or material"));
    }

    // THE NUMBERING AND THE FIELD MUST BE THIS MESH'S. Stamp and count both,
    // which is what stops "u from M1 on mesh M2" when the two happen to have
    // the same size and overlapping NodeIds.
    if (!numbering.describes(mesh)) {
        return fail(RecoveryProblem::MeshMismatch,
                    makeError(ErrorCode::FailedPrecondition,
                              "the degree-of-freedom numbering was built for a different mesh "
                              "from the one this analysis holds, so every index would name "
                              "unrelated material"));
    }
    if (!solution.describes(mesh)) {
        return fail(RecoveryProblem::MeshMismatch,
                    makeError(ErrorCode::FailedPrecondition,
                              "the solved displacement field does not describe this mesh, so its "
                              "entries cannot be read as this mesh's nodes"));
    }

    // THE MATERIAL MUST BE THE ONE THAT WAS SOLVED WITH. Identity AND
    // revision: "the same material with a changed modulus" is a different
    // solver input, which is why AssemblySource carries both. Recomputing
    // sigma = D(new) eps(old) would produce a number nothing downstream can
    // detect as wrong.
    if (material.id() != solution.source().material ||
        material.revision() != solution.source().materialRevision) {
        return fail(
            RecoveryProblem::MaterialSourceMismatch,
            makeError(ErrorCode::FailedPrecondition,
                      std::format("this displacement field was solved with material {} at "
                                  "revision {}, and the material handed in is {} at revision {}; "
                                  "stress recovered from the two would be wrong in a way nothing "
                                  "downstream could detect",
                                  solution.source().material, solution.source().materialRevision,
                                  material.id(), material.revision())));
    }

    const std::span<const meshing::Tetrahedron> tets = mesh.tetrahedra();
    if (tets.empty()) {
        return fail(RecoveryProblem::MeshHasNoElements,
                    makeError(ErrorCode::FailedPrecondition,
                              "the mesh carries no tetrahedron, so there is no element field to "
                              "recover"));
    }

    // D ONCE, FOR EVERY ELEMENT. It is a function of the material alone, which
    // is exactly why P17-ELEM split it out from the kinematics.
    Result<ElasticityMatrix> elasticity = isotropicElasticity(material.elastic());
    if (!elasticity.has_value()) {
        return fail(RecoveryProblem::InvalidMaterial, elasticity.error());
    }

    Working out;

    // -----------------------------------------------------------------------
    // NODAL DISPLACEMENT, in the mesh's own ascending-NodeId enumeration.
    // -----------------------------------------------------------------------
    const std::span<const meshing::Node> nodes = mesh.nodes();
    out.displacements.reserve(nodes.size());
    for (const meshing::Node& node : nodes) {
        // THE THREE INDICES COME FROM P17-DOF. No arithmetic on a NodeId.
        Result<std::array<DofIndex, kDofsPerNode>> indices = numbering.indicesOf(node.id);
        if (!indices.has_value()) {
            return fail(RecoveryProblem::DegreeOfFreedomMissing, indices.error());
        }

        static_assert(kDofsPerNode == 3);
        static_assert(offsetOf(DofComponent::Ux) == 0);
        static_assert(offsetOf(DofComponent::Uy) == 1);
        static_assert(offsetOf(DofComponent::Uz) == 2);
        const Translation3D displacement{solution.displacementOf((*indices)[0]),
                                         solution.displacementOf((*indices)[1]),
                                         solution.displacementOf((*indices)[2])};

        // FINITENESS FIRST, AND ON THE BASE QUANTITY. A NaN here is reported
        // as a non-finite displacement rather than surviving to become a
        // non-finite strain or an eigensolver failure three steps later, which
        // would lose the root cause.
        if (!isFinite(displacement)) {
            return fail(RecoveryProblem::NonFiniteDisplacement,
                        makeError(ErrorCode::Internal,
                                  std::format("the solved displacement of {} is not finite, so no "
                                              "field can be recovered from it",
                                              node.id)));
        }

        const Length magnitude = displacementMagnitude(displacement);
        if (!isFinite(magnitude)) {
            return fail(RecoveryProblem::NonFiniteDerivedResult,
                        makeError(ErrorCode::Internal,
                                  std::format("the displacement magnitude of {} is not finite "
                                              "although its components are",
                                              node.id)));
        }
        if (magnitude > out.largestDisplacement) {
            out.largestDisplacement = magnitude;
        }

        out.displacements.push_back(
            NodalDisplacement{.node = node.id, .displacement = displacement, .magnitude = magnitude});
    }

    // -----------------------------------------------------------------------
    // ELEMENT FIELDS, in the mesh's own ascending-ElementId enumeration.
    // -----------------------------------------------------------------------
    out.elements.reserve(tets.size());
    for (const meshing::Tetrahedron& tet : tets) {
        std::array<Point3D, kTet4Nodes> corners{};
        for (std::size_t corner = 0; corner < kTet4Nodes; ++corner) {
            const meshing::Node* node = mesh.findNode(tet.nodes[corner]);
            if (node == nullptr) {
                return fail(RecoveryProblem::ElementNodeMissing,
                            makeError(ErrorCode::NotFound,
                                      std::format("element {} names {}, which is not a node of "
                                                  "this mesh",
                                                  tet.id, tet.nodes[corner])));
            }
            corners[corner] = node->position;
        }

        // u_e IN localDofIndex ORDER, from the mesh's node order. NOT
        // reordered by coordinate.
        Result<std::array<Translation3D, kTet4Nodes>> local =
            elementDisplacements(numbering, solution, tet.nodes);
        if (!local.has_value()) {
            return fail(RecoveryProblem::DegreeOfFreedomMissing,
                        makeError(local.error().code,
                                  std::format("element {}: {}", tet.id, local.error().message)));
        }

        // THE WHOLE PER-ELEMENT RECOVERY IS THE SHARED KERNEL. B, D, the
        // invariants and every finiteness refusal live in `recoverOneElement`,
        // which `recoverElementFields` also calls -- so the analytical tests
        // and the refusal tests exercise this exact code rather than a parallel
        // copy of it.
        ElementOutcome recovered = recoverOneElement(tet.id, corners, *local, *elasticity);
        if (recovered.problem.has_value()) {
            return fail(*recovered.problem, std::move(recovered.error));
        }

        if (recovered.fields.vonMises > out.largestVonMises) {
            out.largestVonMises = recovered.fields.vonMises;
        }
        out.elements.push_back(recovered.fields);
    }

    return out;
}

} // namespace

Result<RecoveredFields> recoverFields(const StructuralModel& model,
                                      const StructuralMaterial& material,
                                      const GlobalStructuralSystem& system,
                                      const SolvedSystem& solution) {
    Working work = run(model, material, system, solution);
    if (work.problem.has_value()) {
        return std::unexpected(std::move(work.error));
    }
    // ATOMIC: the vectors move in only now, after every element passed. An
    // element that failed at index 927 published nothing.
    return RecoveredFields(system.source(), solution.mesh(), work.largestDisplacement,
                           work.largestVonMises, std::move(work.displacements),
                           std::move(work.elements));
}

std::optional<RecoveryProblem> fieldRecoveryProblem(const StructuralModel& model,
                                                    const StructuralMaterial& material,
                                                    const GlobalStructuralSystem& system,
                                                    const SolvedSystem& solution) {
    return run(model, material, system, solution).problem;
}

Result<NodalDisplacement> RecoveredFields::displacementOf(const meshing::Mesh& mesh,
                                                          meshing::NodeId node) const {
    // THE MESH IS CHECKED FIRST. P16 restarts its handles at 1 after every
    // remesh, so a handle from another mesh would resolve here and return a
    // plausible number; identity is what decides, never the handle's value.
    if (!describes(mesh)) {
        return makeError(ErrorCode::FailedPrecondition,
                         "these fields were recovered on a different mesh, so none of its node "
                         "handles names the same material");
    }
    // BINARY SEARCH OVER ASCENDING HANDLES, the access model Mesh, MeshDofMap
    // and StructuralResult all use. Never an index derived from the handle.
    const auto found = std::ranges::lower_bound(displacements_, node, {}, &NodalDisplacement::node);
    if (found == displacements_.end() || found->node != node) {
        return makeError(ErrorCode::NotFound,
                         std::format("{} is not a node of the mesh these fields were recovered on",
                                     node));
    }
    return *found;
}

Result<ElementFields> RecoveredFields::fieldsOf(const meshing::Mesh& mesh,
                                                meshing::ElementId element) const {
    if (!describes(mesh)) {
        return makeError(ErrorCode::FailedPrecondition,
                         "these fields were recovered on a different mesh, so none of its element "
                         "handles names the same material");
    }
    const auto found = std::ranges::lower_bound(elements_, element, {}, &ElementFields::element);
    if (found == elements_.end() || found->element != element) {
        return makeError(
            ErrorCode::NotFound,
            std::format("element {} is not an element of the mesh these fields were recovered on",
                        element));
    }
    return *found;
}

} // namespace bettercad::structural

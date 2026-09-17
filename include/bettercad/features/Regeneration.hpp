#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/document/References.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/core/geometry/Chamfer.hpp>
#include <bettercad/core/geometry/Hole.hpp>
#include <bettercad/core/geometry/Sweeps.hpp>
#include <bettercad/core/geometry/VariableFillet.hpp>
#include <bettercad/core/math/Direction.hpp>
#include <bettercad/core/math/RigidTransform.hpp>
#include <bettercad/features/ChamferFeature.hpp>
#include <bettercad/features/CircularPatternFeature.hpp>
#include <bettercad/features/CombineFeature.hpp>
#include <bettercad/features/DraftFeature.hpp>
#include <bettercad/features/Export.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/FaceReferences.hpp>
#include <bettercad/features/FilletFeature.hpp>
#include <bettercad/features/HoleFeature.hpp>
#include <bettercad/features/LinearPatternFeature.hpp>
#include <bettercad/features/LoftFeature.hpp>
#include <bettercad/features/MirrorFeature.hpp>
#include <bettercad/features/RevolveFeature.hpp>
#include <bettercad/features/RibFeature.hpp>
#include <bettercad/features/ShellFeature.hpp>
#include <bettercad/features/SplitFeature.hpp>
#include <bettercad/features/SweepFeature.hpp>
#include <bettercad/features/VariableFilletFeature.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <vector>

// Evaluation of individual features from the current document state. The
// sketch geometry is used as it is; solving sketches first is the job of the
// regeneration graph (Regenerator).
namespace bettercad::features {

/// Computes the body of a split feature (P12-FEAT-002): @p target (the target
/// feature's body) cut by the resolved plane, keeping the chosen side(s)
/// (geometry::splitBody()). @p bodies resolves a plane on a named face.
/// Fails with FailedPrecondition without a target body, with the plane's
/// resolution errors ("the split plane: ..."), and with splitBody()'s
/// errors, each prefixed with the feature's name.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<geometry::Body>
regenerateSplit(const SplitFeature& feature, const Document& document, const geometry::Body* target,
                const BodyLookup& bodies = {});

/// Computes the body of a combine feature (P12-FEAT-002): @p target joined
/// with, cut by or intersected with each tool's body from @p bodies, in
/// order. Fails with FailedPrecondition without a target body or a tool's
/// body, and when a step leaves nothing ("nothing is left after cutting
/// Pin (object:5)"), each prefixed with the feature's name.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<geometry::Body>
regenerateCombine(const CombineFeature& feature, const Document& document, const geometry::Body* target,
                  const BodyLookup& bodies);

/// Depth of an extrude: the driving parameter's value if it has one,
/// otherwise the literal depth. The result must be a positive length.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<Length> resolveDepth(const ExtrudeDefinition& definition,
                                                                   const Document& document);

/// The tool solid of an extrude: the profile sketch's closed regions,
/// extruded by the resolved depth, before it is combined with a target.
///
/// A through-all extrude (P12-FEAT-001) reaches through @p target, which it
/// needs (FailedPrecondition without it): from the sketch plane to 1 mm
/// beyond the target's bounds in its direction (both ways when symmetric).
/// It fails with FailedPrecondition when the target lies wholly on the side
/// it does not go to. @p placement is where the tool will be moved before it
/// cuts (a pattern's or mirror's instance): the target is measured along the
/// moved sketch normal, and the tool is returned unmoved.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<geometry::Body>
extrudeTool(const ExtrudeFeature& feature, const Document& document, const geometry::Body* target = nullptr,
            const RigidTransform3D& placement = {});

/// Computes the body of an extrude feature: its tool (extrudeTool()),
/// combined with @p target for Join/Cut/Intersect.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<geometry::Body>
regenerateExtrude(const ExtrudeFeature& feature, const Document& document,
                  const geometry::Body* target = nullptr);

/// Sweep angle of a revolve: the driving parameter's value if it has one
/// (NotFound if missing, DimensionMismatch if not an angle), otherwise the
/// literal angle. The result must be in (0, 360°].
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<Angle> resolveAngle(const RevolveDefinition& definition,
                                                                  const Document& document);

/// The revolve axis in model space, taken from the profile sketch. Fails
/// with NotFound if the axis line does not exist in the sketch, and with
/// InvalidArgument if the entity is not a line or has zero length.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<Axis3D> resolveAxis(const RevolveAxis& axis,
                                                                  const sketch::Sketch& profile);

/// The tool solid of a revolve: the profile sketch's closed regions rotated
/// about the resolved axis through the resolved angle in the chosen
/// direction, before it is combined with a target. Profiles that cross the
/// axis fail with InvalidArgument.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<geometry::Body> revolveTool(const RevolveFeature& feature,
                                                                          const Document& document);

/// Computes the body of a revolve feature: its tool (revolveTool()),
/// combined with @p target for Join/Cut/Intersect.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<geometry::Body>
regenerateRevolve(const RevolveFeature& feature, const Document& document,
                  const geometry::Body* target = nullptr);

/// The path of a sweep in its sketch's plane: its edges' current geometry,
/// each oriented to start where the one before ends, in the order of
/// travel (see SweepPath). Fails with NotFound for a missing sketch or
/// edge, and with InvalidArgument for a point, a circle joined with other
/// edges, an edge of zero length, or consecutive edges that do not meet.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<geometry::PlanarPath> resolveSweepPath(const SweepPath& path,
                                                                                     const Document& document);

/// The tool solid of a sweep: the profile sketch's closed regions swept
/// along the resolved path (geometry::makeSweep()), before it is combined
/// with a target.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<geometry::Body> sweepTool(const SweepFeature& feature,
                                                                        const Document& document);

/// Computes the body of a sweep feature: its tool (sweepTool()), combined
/// with @p target for Join/Cut/Intersect.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<geometry::Body>
regenerateSweep(const SweepFeature& feature, const Document& document, const geometry::Body* target = nullptr);

/// The sections of a loft, in order: each section sketch's one closed
/// profile, its plane moved along its normal by the section's offset (the
/// driving parameter's value if it has one). Fails with NotFound for a
/// missing sketch or offset parameter, DimensionMismatch for an offset
/// parameter that is not a length, and FailedPrecondition for a sketch
/// without exactly one closed profile, or whose profile has a hole; messages
/// name the section ("section 2 (sketch 'Top'): ...").
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<std::vector<geometry::PlanarRegion>>
resolveLoftSections(const LoftDefinition& definition, const Document& document);

/// The tool solid of a loft: its resolved sections lofted in order
/// (geometry::makeLoft()), before it is combined with a target.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<geometry::Body> loftTool(const LoftFeature& feature,
                                                                       const Document& document);

/// Computes the body of a loft feature: its tool (loftTool()), combined with
/// @p target for Join/Cut/Intersect.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<geometry::Body>
regenerateLoft(const LoftFeature& feature, const Document& document, const geometry::Body* target = nullptr);

/// Distance of a chamfer: the driving parameter's value if it has one
/// (NotFound if missing, DimensionMismatch if not a length), otherwise the
/// literal distance. Its range is checked by the chamfer itself.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<Length> resolveChamferDistance(const ChamferDefinition& definition,
                                                                             const Document& document);

/// The geometry request of a chamfer, with its distance resolved.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<geometry::ChamferRequest>
resolveChamferRequest(const ChamferDefinition& definition, const Document& document);

/// Names for the faces chamfer @p chamfer cuts (P12-SKETCH-003): the face
/// of edge reference i (from 0) is its `chamfer` face with `edge` i + 1,
/// with @p copies (a pattern's or mirror's instance) appended.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT geometry::ChamferFaceNamer chamferFaceNamer(ObjectId chamfer,
                                                                                   std::vector<FaceCopy> copies);

/// Computes the body of a chamfer feature: @p target (the target feature's
/// body) with the referenced edges chamfered. Fails with FailedPrecondition
/// if there is no target body. Edge references that match no edge, or
/// several, are errors, never guesses; see geometry::chamferEdges().
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<geometry::Body>
regenerateChamfer(const ChamferFeature& feature, const Document& document, const geometry::Body* target);

/// Radius of a fillet: the driving parameter's value if it has one (NotFound
/// if missing, DimensionMismatch if not a length), otherwise the literal
/// radius. Its range is checked by the fillet itself.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<Length> resolveFilletRadius(const FilletDefinition& definition,
                                                                          const Document& document);

/// Computes the body of a fillet feature: @p target (the target feature's
/// body) with the referenced edges rounded. Fails with FailedPrecondition if
/// there is no target body. Edge references that match no edge, or several,
/// are errors, never guesses; see geometry::filletEdges().
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<geometry::Body>
regenerateFillet(const FilletFeature& feature, const Document& document, const geometry::Body* target);

/// The geometry request of a variable-radius fillet: its edges and
/// stations with every driven radius replaced by its parameter's value
/// (NotFound if the parameter is missing, DimensionMismatch if it is not a
/// length). The stations and the law are checked by the fillet itself.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<geometry::VariableFilletRequest>
resolveVariableFilletRequest(const VariableFilletDefinition& definition, const Document& document);

/// Computes the body of a variable-radius fillet feature (P12-FEAT-006):
/// @p target (the target feature's body) with the referenced edges rounded
/// and the result checked against the radius law (see
/// geometry::variableFilletEdges()). Fails with FailedPrecondition if there
/// is no target body; errors are prefixed with the feature's name.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<geometry::Body>
regenerateVariableFillet(const VariableFilletFeature& feature, const Document& document,
                         const geometry::Body* target);

/// Thickness of a rib: the driving parameter's value if it has one
/// (NotFound if missing, DimensionMismatch if not a length), otherwise the
/// literal thickness. Its range is checked by the rib itself.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<Length> resolveRibThickness(const RibDefinition& definition,
                                                                          const Document& document);

/// The profile of a rib (P12-FEAT-005): the definition's edges, in order,
/// head to tail, in the profile sketch's plane. Lines, arcs and open splines
/// are taken; each later edge starts exactly where the one before ends.
/// Fails with NotFound for a missing sketch or edge, and with
/// InvalidArgument for construction geometry, other kinds of entity,
/// degenerate edges and edges that do not meet ("the profile is not
/// connected: ...").
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<geometry::PlanarPath> resolveRibProfile(
    const RibDefinition& definition, const Document& document);

/// Computes the body of a rib feature: @p target (the target feature's body)
/// with the rib joined (geometry::addRib()), its faces named for the
/// feature. Fails with FailedPrecondition without a target body, with the
/// profile's and the thickness's resolution errors, and with addRib()'s
/// errors, each prefixed with the feature's name.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<geometry::Body>
regenerateRib(const RibFeature& feature, const Document& document, const geometry::Body* target);

/// Angle of a draft: the driving parameter's value if it has one (NotFound
/// if missing, DimensionMismatch if not an angle), otherwise the literal
/// angle. Its range is checked by the draft itself.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<Angle> resolveDraftAngle(const DraftDefinition& definition,
                                                                       const Document& document);

/// Computes the body of a draft feature (P12-FEAT-004): @p target (the
/// target feature's body) with the named faces turned about the resolved
/// neutral plane (geometry::draftFaces()). @p bodies resolves a plane on a
/// named face. Fails with FailedPrecondition without a target body; with
/// the plane's resolution errors ("the neutral plane: ..."); with the errors
/// of checkFaceName() for a face ("face 2: ..."); with NotFound when no face
/// of the target's body carries a face's name; and with draftFaces()'s
/// errors, each prefixed with the feature's name.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<geometry::Body>
regenerateDraft(const DraftFeature& feature, const Document& document, const geometry::Body* target,
                const BodyLookup& bodies = {});

/// Thickness of a shell: the driving parameter's value if it has one
/// (NotFound if missing, DimensionMismatch if not a length), otherwise the
/// literal thickness. Its range is checked by the shell itself.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<Length> resolveShellThickness(const ShellDefinition& definition,
                                                                            const Document& document);

/// Computes the body of a shell feature (P12-FEAT-003): @p target (the
/// target feature's body) hollowed as defined (geometry::shellBody()).
/// Fails with FailedPrecondition without a target body; with the errors of
/// checkFaceName() for an open face ("open face 2: ..."); with NotFound when
/// no face of the target's body carries an open face's name; and with
/// shellBody()'s errors, each prefixed with the feature's name.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<geometry::Body>
regenerateShell(const ShellFeature& feature, const Document& document, const geometry::Body* target);

/// The geometry request of a hole, with the driven diameter, depth and
/// centre coordinates taken from their parameters (NotFound if missing,
/// DimensionMismatch if not lengths). The values' ranges are checked by the
/// hole itself.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<geometry::HoleRequest>
resolveHoleRequest(const HoleDefinition& definition, const Document& document);

/// Names for the flat faces hole @p hole makes (P12-SKETCH-003): its
/// `hole_bottom` and `counterbore_floor`, with @p copies appended.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT geometry::HoleFaceNamer holeFaceNamer(ObjectId hole,
                                                                             std::vector<FaceCopy> copies);

/// Computes the body of a hole feature: @p target (the target feature's
/// body) drilled as defined. Fails with FailedPrecondition if there is no
/// target body; placement faces that match nothing, or several faces under
/// the centre, are errors, never guesses; see geometry::cutHole().
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<geometry::Body>
regenerateHole(const HoleFeature& feature, const Document& document, const geometry::Body* target);

/// The instances of a linear pattern, in order, with the driven counts and
/// spacings taken from their parameters: counts must be whole numbers of at
/// least 1 (DimensionMismatch unless the parameter is dimensionless),
/// spacings positive lengths, and there may be at most
/// kMaxPatternInstances instances (InvalidArgument).
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<std::vector<PatternInstance>>
resolvePatternInstances(const LinearPatternDefinition& definition, const Document& document);

/// Computes the body of a linear pattern: @p target (the source feature's
/// body, which is instance 0) with the source's operation applied at every
/// other instance, in order:
/// - an extrude or revolve's tool is moved and united with the body (new
///   body and join) or subtracted from it (cut); intersect is not
///   supported. Instances of a new body that touch or overlap fuse, as the
///   regions of one extrude do;
/// - a hole, chamfer or fillet is applied with its face or edge references
///   moved exactly by the offset, with all of its own checks (a hole must
///   fit on its face, a moved edge must match exactly one edge).
///
/// The first instance that fails fails the pattern, with its index and
/// offset in the message; there are no partial patterns. Patterns of
/// patterns are refused.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<geometry::Body>
regenerateLinearPattern(const LinearPatternFeature& feature, const Document& document,
                        const geometry::Body* target);

/// The instances of a circular pattern, in order, with the driven count and
/// angle taken from their parameters and checked as the definition's literal
/// values are (a whole count from 1 to kMaxPatternInstances; an angle that
/// never reaches 360°).
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<std::vector<CircularPatternInstance>>
resolveCircularPatternInstances(const CircularPatternDefinition& definition, const Document& document);

/// Computes the body of a circular pattern exactly as a linear pattern's
/// (regenerateLinearPattern()), with each instance turned about the axis
/// instead of moved: the source's operation is repeated at every instance
/// with the same checks, the first failure fails the pattern ("instance 4 at
/// 180 deg: …"), and patterns of patterns are refused.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<geometry::Body>
regenerateCircularPattern(const CircularPatternFeature& feature, const Document& document,
                          const geometry::Body* target);

/// The mirror plane in model space, with a driven offset taken from its
/// length parameter (DimensionMismatch otherwise), and the reflection
/// across it.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<MirrorReflection>
resolveMirrorReflection(const MirrorDefinition& definition, const Document& document);

/// Computes the body of a mirror from @p target (the source feature's body,
/// instance 0) and its mirror image (instance 1):
/// - feature scope: the source's operation is applied again, reflected, as
///   one more pattern instance (the same sources and checks as for linear
///   patterns; patterns and mirrors are refused as sources). A hole the plane
///   maps onto itself, and a chamfer or fillet edge it maps onto one of the
///   feature's own edges, are refused: they are already there;
/// - body scope: the whole source body is reflected and united with the
///   original, or on its own when the original is not kept.
///
/// A failure fails the mirror, naming the mirror image and its plane; there
/// is no partial result.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<geometry::Body>
regenerateMirror(const MirrorFeature& feature, const Document& document, const geometry::Body* target);

} // namespace bettercad::features

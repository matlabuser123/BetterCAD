#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/core/geometry/Split.hpp>
#include <bettercad/core/math/Frame.hpp>
#include <bettercad/core/math/Point.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/drawing/Export.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Section views (P14-VIEW-002, on ADR-011, ADR-012 and ADR-018).
//
// A section shows what a solid looks like where a cutting plane passes
// through it. The CUTTING PLANE is intent: an engineer chooses where to cut
// and which way to look. Everything that follows -- the cut solid, the cut
// faces, the hatch on them, the projected curves -- is derived and is
// recomputed on every request (ADR-011).
//
// THE SIGN CONVENTION, STATED ONCE. In a section view the material between
// the viewer and the cutting plane is removed, so that the cut face is what
// you see. That is not a choice between standards the way first- and
// third-angle placement is (ADR-018); it is what "section" means. So the
// removed side is derived from the view's own direction rather than stored,
// and cannot disagree with the view it belongs to.
namespace bettercad::drawing {

/// How much of the model a section cuts.
enum class SectionKind : std::uint8_t {
    /// One plane through the whole model.
    Full,
    /// One plane through half the model, the other half left uncut, split by
    /// a second plane.
    Half,
    /// A path of parallel plane segments, joined by jogs, so that features
    /// no single plane reaches are all crossed (ISO 128 calls this an offset
    /// or stepped section).
    Offset,
};

/// "full", "half", "offset".
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::string_view toString(SectionKind kind) noexcept;
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::optional<SectionKind> sectionKindFromString(
    std::string_view text) noexcept;

/// One leg of a cutting path, as an offset from the section's own plane.
///
/// A full section has none. An offset section has one per jog: each says how
/// far along the plane's normal that leg sits, and where along the plane's X
/// axis the jog to it happens. Parallel legs only -- the ordinary stepped
/// section -- because that is the robust foundation this milestone is scoped
/// to and an arbitrary swept cutting surface is not.
struct SectionLeg {
    /// Where the jog to this leg happens, along the plane's X axis, measured
    /// from the plane's origin.
    Length at{};
    /// How far this leg sits from the section's plane, along its normal.
    Length offset{};

    friend bool operator==(const SectionLeg&, const SectionLeg&) = default;
};

/// Where a section cuts, and how much.
///
/// The plane is given as an origin and a normal; its X axis orders the legs
/// of an offset section and splits a half section. `kind` says how much of
/// the model is cut, and the legs say where an offset section steps.
struct CuttingPlane {
    /// A point the plane passes through.
    Point3D origin{};
    /// The plane's normal. A section view made from this plane stands on
    /// the normal's side and looks back along it, so the material on the
    /// normal's side is what the viewer is standing in and what is removed.
    ///
    /// That is the DEFAULT, not a rule: which side a section removes is
    /// derived from the basis of the view doing the looking (see keptSide),
    /// so a plane can be used by a view that looks at it the other way and
    /// the removal follows the view, not the stored normal.
    Direction3D normal = Direction3D::unitY();
    /// Orders the legs of an offset section and splits a half section.
    /// Projected into the plane, as Frame3D::create does.
    Direction3D reference = Direction3D::unitX();
    SectionKind kind = SectionKind::Full;
    /// Where a half section stops cutting, along the plane's X axis. Half
    /// sections only.
    Length splitAt{};
    /// An offset section's jogs, in the order they are crossed. Must be
    /// strictly increasing in `at`, so the path never doubles back.
    std::vector<SectionLeg> legs{};

    friend bool operator==(const CuttingPlane&, const CuttingPlane&) = default;
};

/// Checks a cutting plane on its own: a finite origin, a usable normal and
/// reference that are not parallel, the legs a kind calls for and no others,
/// and legs that are strictly increasing and finite.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<void> validate(const CuttingPlane& plane);

/// The plane as a Frame3D, with the normal and the reference orthonormalised
/// the way Frame3D::create does.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<Frame3D> frameOf(const CuttingPlane& plane);

/// Every plane the cut faces can lie on, in the order the cutting path
/// crosses them.
///
/// A full or half section cuts on one plane, so there is one. An offset
/// section cuts on one per leg, each parallel to the first and displaced
/// along its normal -- which is why an offset section needs no separate
/// "development" step: parallel legs, seen along the shared normal, already
/// project into a single plane.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<std::vector<Frame3D>> legFrames(
    const CuttingPlane& plane);

/// Where a half section stops cutting: the plane through `splitAt` whose
/// normal is the cutting plane's X axis. Material is removed on the normal
/// side of it -- where the cutting plane's local x exceeds `splitAt` -- and
/// the model is left whole on the other.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<Frame3D> splitFrame(const CuttingPlane& plane);

/// How a section is hatched.
///
/// Settings are intent; the lines they produce are derived. ISO 128 hatches
/// cut material at 45 degrees by default, and gives adjacent components
/// different angles so a joint can be read -- that last part is an assembly
/// question and is not decided here.
struct HatchSettings {
    Angle angle = Angle::fromSi(0.7853981633974483); // 45 degrees
    Length spacing = Length::fromSi(0.0025);         // 2.5 mm on the sheet
    /// Named so a later milestone can add patterns without changing the
    /// meaning of the ones already stored. "iso-45" is plain parallel lines.
    std::string pattern = "iso-45";

    friend bool operator==(const HatchSettings&, const HatchSettings&) = default;
};

/// Both finite, a spacing greater than zero, and a known pattern.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<void> validate(const HatchSettings& settings);

/// A closed loop of a cut face, in sheet coordinates.
///
/// `outer` distinguishes the boundary of the material from the boundary of a
/// void inside it -- a hole through a sectioned wall is an inner loop, and
/// what makes it a hole rather than more material is that hatch must not
/// cross it.
struct SectionLoop {
    std::vector<Point2D> points{};
    bool outer = true;
    /// WHICH OCCURRENCE's material this loop bounds (P14-ASM-001).
    ///
    /// Empty for a section of a feature. Set for every loop of an assembly
    /// section, so that adjacent components' cut faces remain distinguishable
    /// -- which is what a later hatch policy needs to give two touching
    /// components different angles, and what stops one component's hatch
    /// being drawn across another's material.
    std::optional<ComponentId> occurrence{};
};

/// What a section draws, in sheet coordinates.
struct SectionGeometry {
    /// The cut faces' boundaries: the outline of the material the plane
    /// passed through, plus a loop for every void inside it.
    std::vector<SectionLoop> loops{};
    /// Hatch, clipped to the material: inside an outer loop and outside every
    /// inner one.
    std::vector<std::pair<Point2D, Point2D>> hatch{};
    /// Total cut area, in sheet units, from the loops by the shoelace
    /// formula. Reported so a test can check it against a closed form rather
    /// than against a picture.
    Area area{};
};

/// Which side of a cutting plane a view removes.
///
/// Derived, never stored: in a section the material between the viewer and
/// the plane goes, so the answer follows from the view's own direction and
/// cannot disagree with the view it belongs to.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT geometry::SplitKeep keptSide(const Frame3D& plane,
                                                                    const Frame3D& viewBasis) noexcept;

/// The solid that remains after @p plane cuts @p body for a view looking
/// through @p viewBasis.
///
/// A full section removes everything between the viewer and the plane. A
/// half section removes only the part of that which also lies on the normal
/// side of `splitFrame`, so half the model is cut and half is left whole. An
/// offset section removes, for each leg, the part between the viewer and
/// that leg's plane which lies in that leg's span along the cutting plane's
/// X axis.
///
/// Fails with FailedPrecondition when the plane misses the body entirely,
/// which is a section of nothing rather than an empty one.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<geometry::Body> cutBody(const geometry::Body& body,
                                                                      const CuttingPlane& plane,
                                                                      const Frame3D& viewBasis);

/// What @p body's section looks like on the sheet.
///
/// The cut solid's edges that lie ON the cutting plane are its outline; they
/// are chained into closed loops, classified into material and voids by
/// containment, and hatched. @p factor and @p placement put the result in
/// sheet coordinates, exactly as a view's own projection does.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<SectionGeometry> sectionGeometry(
    const geometry::Body& body, const CuttingPlane& plane, const Frame3D& viewBasis, double factor,
    const Point2D& placement, const HatchSettings& hatch);

// --- 2D helpers, needed because no 2D geometry existed before this ---------

/// Twice the signed area of @p loop, by the shoelace formula. Positive when
/// the points wind counter-clockwise.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT double twiceSignedArea(const std::vector<Point2D>& loop) noexcept;

/// Whether @p point is inside @p loop, by the crossing-number rule.
///
/// A point exactly on the boundary is not defined either way, which is why
/// hatch lines are placed off the boundary rather than on it.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT bool contains(const std::vector<Point2D>& loop,
                                                     const Point2D& point) noexcept;

/// Where the infinite line through @p from and @p to crosses @p loop, as
/// parameters along that line, sorted.
///
/// Used to clip a hatch line to a loop: the crossings come in pairs, and the
/// material lies between alternate pairs.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::vector<double> crossings(const std::vector<Point2D>& loop,
                                                                     const Point2D& from,
                                                                     const Point2D& to);

} // namespace bettercad::drawing

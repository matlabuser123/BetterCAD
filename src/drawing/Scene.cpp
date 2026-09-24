#include <bettercad/drawing/Scene.hpp>

#include <bettercad/core/Units.hpp>

#include <cmath>
#include <format>

namespace bettercad::drawing {

std::string_view toString(LineStyle style) noexcept {
    switch (style) {
    case LineStyle::Continuous:
        return "continuous";
    case LineStyle::Dashed:
        return "dashed";
    case LineStyle::Centre:
        return "centre";
    case LineStyle::Thin:
        return "thin";
    }
    return "unknown";
}

std::optional<LineStyle> lineStyleFromString(std::string_view text) noexcept {
    for (const LineStyle style :
         {LineStyle::Continuous, LineStyle::Dashed, LineStyle::Centre, LineStyle::Thin}) {
        if (toString(style) == text) {
            return style;
        }
    }
    return std::nullopt;
}

std::string_view toString(TextAnchor anchor) noexcept {
    switch (anchor) {
    case TextAnchor::BaselineLeft:
        return "baseline_left";
    case TextAnchor::BaselineCentre:
        return "baseline_centre";
    case TextAnchor::BaselineRight:
        return "baseline_right";
    case TextAnchor::MiddleLeft:
        return "middle_left";
    case TextAnchor::MiddleCentre:
        return "middle_centre";
    case TextAnchor::MiddleRight:
        return "middle_right";
    }
    return "unknown";
}

std::optional<TextAnchor> textAnchorFromString(std::string_view text) noexcept {
    for (const TextAnchor anchor :
         {TextAnchor::BaselineLeft, TextAnchor::BaselineCentre, TextAnchor::BaselineRight,
          TextAnchor::MiddleLeft, TextAnchor::MiddleCentre, TextAnchor::MiddleRight}) {
        if (toString(anchor) == text) {
            return anchor;
        }
    }
    return std::nullopt;
}

void SceneItems::append(const SceneItems& other) {
    lines.insert(lines.end(), other.lines.begin(), other.lines.end());
    arcs.insert(arcs.end(), other.arcs.begin(), other.arcs.end());
    texts.insert(texts.end(), other.texts.begin(), other.texts.end());
}

Result<void> validate(const SceneItems& items) {
    const auto finite = [](const Point2D& p) {
        return std::isfinite(p.x.si()) && std::isfinite(p.y.si());
    };
    for (const SceneLine& line : items.lines) {
        if (line.points.size() < 2) {
            return makeError(ErrorCode::InvalidArgument,
                             "a drawn line needs at least two points; one point is not a line");
        }
        if (toString(line.style) == "unknown") {
            return makeError(ErrorCode::InvalidArgument, "a drawn line has an unknown style");
        }
        for (const Point2D& p : line.points) {
            if (!finite(p)) {
                return makeError(ErrorCode::InvalidArgument,
                                 "a drawn line has a point that is not finite");
            }
        }
        if (!std::isfinite(line.width.si()) || line.width.si() <= 0.0) {
            return makeError(ErrorCode::InvalidArgument,
                             "a drawn line's width must be finite and greater than zero");
        }
    }
    for (const SceneArc& arc : items.arcs) {
        if (!finite(arc.centre)) {
            return makeError(ErrorCode::InvalidArgument, "an arc's centre is not finite");
        }
        if (!std::isfinite(arc.radius.si()) || arc.radius.si() <= 0.0) {
            return makeError(ErrorCode::InvalidArgument,
                             "an arc's radius must be finite and greater than zero");
        }
        if (!std::isfinite(arc.start.si()) || !std::isfinite(arc.sweep.si())) {
            return makeError(ErrorCode::InvalidArgument, "an arc's angles must be finite");
        }
        if (arc.sweep.si() == 0.0) {
            return makeError(ErrorCode::InvalidArgument,
                             "an arc that sweeps nothing draws nothing; it is a point, not an arc");
        }
        if (toString(arc.style) == "unknown") {
            return makeError(ErrorCode::InvalidArgument, "an arc has an unknown style");
        }
        if (!std::isfinite(arc.width.si()) || arc.width.si() <= 0.0) {
            return makeError(ErrorCode::InvalidArgument,
                             "an arc's width must be finite and greater than zero");
        }
    }
    for (const SceneText& text : items.texts) {
        if (text.text.empty()) {
            return makeError(ErrorCode::InvalidArgument,
                             "a text run with nothing in it would draw nothing and take up room");
        }
        if (!finite(text.at)) {
            return makeError(ErrorCode::InvalidArgument, "a text run sits at a point that is not finite");
        }
        if (!std::isfinite(text.height.si()) || text.height.si() <= 0.0) {
            return makeError(ErrorCode::InvalidArgument,
                             "a text run's height must be finite and greater than zero");
        }
        if (!std::isfinite(text.rotation.si())) {
            return makeError(ErrorCode::InvalidArgument, "a text run's rotation must be finite");
        }
        if (toString(text.anchor) == "unknown") {
            return makeError(ErrorCode::InvalidArgument, "a text run has an unknown anchor");
        }
    }
    return {};
}

Result<void> validate(const DrawingScene& scene) {
    if (!std::isfinite(scene.width.si()) || scene.width.si() <= 0.0 ||
        !std::isfinite(scene.height.si()) || scene.height.si() <= 0.0) {
        return makeError(ErrorCode::InvalidArgument,
                         "a sheet's width and height must be finite and greater than zero");
    }
    if (auto valid = validate(scene.items); !valid) {
        return valid;
    }
    // Everything drawn is ON the page. A primitive at negative y is a
    // coordinate-system mistake -- almost always a forgotten flip -- and
    // catching it once here is what stops three writers each producing a
    // differently wrong picture from the same scene.
    const double w = scene.width.si();
    const double h = scene.height.si();
    const auto offPage = [&](const Point2D& p) {
        return p.x.si() < 0.0 || p.x.si() > w || p.y.si() < 0.0 || p.y.si() > h;
    };
    const auto refuse = [&](const Point2D& p, std::string_view what) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("{} at ({:.3f}, {:.3f}) mm is off a {:.3f} x {:.3f} mm sheet",
                                     what, p.x.in(units::mm), p.y.in(units::mm),
                                     scene.width.in(units::mm), scene.height.in(units::mm)));
    };
    for (const SceneLine& line : scene.items.lines) {
        for (const Point2D& p : line.points) {
            if (offPage(p)) {
                return refuse(p, "a drawn line has a point");
            }
        }
    }
    for (const SceneArc& arc : scene.items.arcs) {
        // The arc's extent, not just its centre: a circle centred on the page
        // can still run off it.
        const Point2D min{arc.centre.x - arc.radius, arc.centre.y - arc.radius};
        const Point2D max{arc.centre.x + arc.radius, arc.centre.y + arc.radius};
        if (offPage(min) || offPage(max)) {
            return refuse(arc.centre, "an arc centred");
        }
    }
    for (const SceneText& text : scene.items.texts) {
        if (offPage(text.at)) {
            return refuse(text.at, "a text run");
        }
    }
    return {};
}

} // namespace bettercad::drawing

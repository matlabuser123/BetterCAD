#pragma once

#include <bettercad/core/geometry/Mesh.hpp>
#include <bettercad/core/units/Units.hpp>

#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Independent checks of triangle meshes and STL files. Nothing here uses the
// geometry kernel, so export tests compare the kernel's output with
// geometry computed from first principles.
namespace bettercad::test {

using Vertex = std::array<double, 3>; // millimetres
using Triangle = std::array<Vertex, 3>;

inline std::vector<Triangle> trianglesOf(const geometry::Mesh& mesh) {
    std::vector<Triangle> triangles;
    for (const auto& t : mesh.triangles) {
        Triangle triangle{};
        for (std::size_t i = 0; i < 3; ++i) {
            const Point3D& p = mesh.vertices.at(t[i]);
            triangle[i] = {p.x.in(units::mm), p.y.in(units::mm), p.z.in(units::mm)};
        }
        triangles.push_back(triangle);
    }
    return triangles;
}

inline Vertex cross(const Vertex& u, const Vertex& v) {
    return {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
}

inline Vertex minus(const Vertex& a, const Vertex& b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

inline double dot(const Vertex& u, const Vertex& v) {
    return u[0] * v[0] + u[1] * v[1] + u[2] * v[2];
}

/// Volume enclosed by closed, outward-oriented triangles (divergence theorem).
inline double enclosedVolume(const std::vector<Triangle>& triangles) {
    double sum = 0.0;
    for (const Triangle& t : triangles) {
        sum += dot(t[0], cross(t[1], t[2]));
    }
    return sum / 6.0;
}

inline double surfaceArea(const std::vector<Triangle>& triangles) {
    double sum = 0.0;
    for (const Triangle& t : triangles) {
        const Vertex n = cross(minus(t[1], t[0]), minus(t[2], t[0]));
        sum += std::sqrt(dot(n, n)) / 2.0;
    }
    return sum;
}

struct SurfaceCheck {
    std::size_t vertices = 0;   ///< distinct positions
    std::size_t openEdges = 0;  ///< used in one direction only
    std::size_t badEdges = 0;   ///< used twice in the same direction, or degenerate

    /// Closed and consistently oriented: every edge is shared by exactly two
    /// triangles that traverse it in opposite directions.
    [[nodiscard]] bool watertight() const noexcept { return openEdges == 0 && badEdges == 0; }
};

/// Merges vertices with identical coordinates and checks the edge structure.
inline SurfaceCheck checkSurface(const std::vector<Triangle>& triangles) {
    std::map<Vertex, std::uint32_t> ids;
    const auto id = [&](const Vertex& v) {
        return ids.emplace(v, static_cast<std::uint32_t>(ids.size())).first->second;
    };
    std::map<std::pair<std::uint32_t, std::uint32_t>, int> directed;
    SurfaceCheck check;
    for (const Triangle& t : triangles) {
        const std::array<std::uint32_t, 3> v{id(t[0]), id(t[1]), id(t[2])};
        for (std::size_t i = 0; i < 3; ++i) {
            const auto a = v[i];
            const auto b = v[(i + 1) % 3];
            if (a == b) {
                ++check.badEdges;
            } else {
                ++directed[{a, b}];
            }
        }
    }
    for (const auto& [edge, count] : directed) {
        if (count != 1) {
            ++check.badEdges;
            continue;
        }
        const auto reverse = directed.find({edge.second, edge.first});
        if (reverse == directed.end()) {
            ++check.openEdges;
        } else if (reverse->second != 1) {
            ++check.badEdges;
        }
    }
    check.vertices = ids.size();
    return check;
}

struct StlData {
    std::string header{};    ///< binary header, or the ASCII solid name
    std::vector<Triangle> triangles{};
    std::vector<Vertex> normals{};
};

/// Strict binary STL reader: the size must match the triangle count.
inline std::optional<StlData> parseBinaryStl(std::string_view bytes) {
    if (bytes.size() < 84) {
        return std::nullopt;
    }
    const auto u32 = [&](std::size_t offset) {
        std::uint32_t value = 0;
        for (std::size_t i = 0; i < 4; ++i) {
            value |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + i])) << (8 * i);
        }
        return value;
    };
    const auto f32 = [&](std::size_t offset) { return static_cast<double>(std::bit_cast<float>(u32(offset))); };
    const std::size_t count = u32(80);
    if (bytes.size() != 84 + 50 * count) {
        return std::nullopt;
    }
    StlData data;
    data.header = std::string{bytes.substr(0, 80)};
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t base = 84 + 50 * i;
        data.normals.push_back({f32(base), f32(base + 4), f32(base + 8)});
        Triangle t{};
        for (std::size_t v = 0; v < 3; ++v) {
            const std::size_t offset = base + 12 + 12 * v;
            t[v] = {f32(offset), f32(offset + 4), f32(offset + 8)};
        }
        data.triangles.push_back(t);
    }
    return data;
}

/// Strict ASCII STL reader for single-solid files; numbers are read as
/// 32-bit floats, like a binary file.
inline std::optional<StlData> parseAsciiStl(std::string_view text) {
    if (!text.ends_with('\n')) {
        return std::nullopt;
    }
    std::vector<std::string_view> lines; // without the final empty line
    while (!text.empty()) {
        const auto end = text.find('\n');
        std::string_view line = text.substr(0, end);
        while (!line.empty() && line.front() == ' ') {
            line.remove_prefix(1);
        }
        lines.push_back(line);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
    }
    const auto numbers = [](std::string_view line, std::string_view keyword) -> std::optional<Vertex> {
        if (!line.starts_with(keyword)) {
            return std::nullopt;
        }
        line.remove_prefix(keyword.size());
        Vertex v{};
        for (double& component : v) {
            while (!line.empty() && line.front() == ' ') {
                line.remove_prefix(1);
            }
            float value = 0.0F;
            const auto [rest, error] = std::from_chars(line.data(), line.data() + line.size(), value);
            if (error != std::errc{}) {
                return std::nullopt;
            }
            component = static_cast<double>(value);
            line.remove_prefix(static_cast<std::size_t>(rest - line.data()));
        }
        return line.empty() ? std::optional<Vertex>{v} : std::nullopt;
    };
    if (lines.size() < 2 || !lines.front().starts_with("solid ")) {
        return std::nullopt;
    }
    StlData data;
    data.header = std::string{lines.front().substr(6)};
    std::size_t i = 1;
    while (i + 7 <= lines.size() - 1 && lines[i].starts_with("facet normal ")) {
        const auto normal = numbers(lines[i], "facet normal ");
        const auto a = numbers(lines[i + 2], "vertex ");
        const auto b = numbers(lines[i + 3], "vertex ");
        const auto c = numbers(lines[i + 4], "vertex ");
        if (!normal || lines[i + 1] != "outer loop" || !a || !b || !c || lines[i + 5] != "endloop" ||
            lines[i + 6] != "endfacet") {
            return std::nullopt;
        }
        data.normals.push_back(*normal);
        data.triangles.push_back({*a, *b, *c});
        i += 7;
    }
    if (i != lines.size() - 1 || lines[i] != "endsolid " + data.header) {
        return std::nullopt;
    }
    return data;
}

} // namespace bettercad::test

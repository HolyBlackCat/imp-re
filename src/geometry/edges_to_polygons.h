#pragma once

#include "geometry/common.h"
#include "geometry/simplify_straight_edges.h"
#include "utils/sparse_set.h"

#include <CDT/CDT.h> // For the constructor of Topology that fills it from a triangulation.

#include <cassert>
#include <cstdint>
#include <type_traits>
#include <utility>

// Converts multiple edges to triangles (using the CDT library, which you can also use directly),
//   and then combines triangles into polygons using our own code.

/* How to use (an example with Box2D).

Geom::EdgesToPolygons::TriangulationInput<int> tri_input; // Usually either `int` or `float` here, it's the coordiante type.

Geom::TilesToEdges::ConvertTilesToEdges(..., tri_input.InsertionCallback());

TilesToEdges::Convert(convert_params);

b2Hull hull{};
b2::Shape::Params shape_params;
// Here `float` is the internal coordinate type for triangulation, CDT library requires it to be floating-point`.
Geom::EdgesToPolygons::ConvertToPolygons<float>(tri_input, b2_maxPolygonVertices, [&](ivec2 pos, Geom::PointInfo info)
{
    if (info.type == Geom::PointType::last)
    {
        body.CreateShape(b2::DestroyWithParent, shape_params, b2MakePolygon(&hull, 0));
        hull.count = 0;
    }
    else
    {
        assert(hull.count < b2_maxPolygonVertices);
        hull.points[hull.count++] = b2Vec2(fvec2(pos));
    }
});

// NOTE: Both input and output is automatically stripped of redundant vertices, don't do it yourself.

*/

namespace Geom::EdgesToPolygons
{
    enum class VertexId : std::uint32_t {invalid = std::uint32_t(-1)};
    enum class EdgeId   : std::uint32_t {invalid = std::uint32_t(-1)}; // Actually a half-edge.
    enum class FaceId   : std::uint32_t {invalid = std::uint32_t(-1)};

    // T is the coordinate type, e.g. `float` or `int`.
    template <typename T>
    struct TriangulationInput
    {
        // You don't need to fill any of this manually, use `InsertionCallback()`.

        std::vector<vec2<T>> points;

        using VertIndex = std::underlying_type_t<VertexId>;
        using Edge = std::pair<VertIndex, VertIndex>;
        // Pairs of indices into `points`.
        std::vector<Edge> edges;

        // Same size as `point`, true if that point is convex.
        std::vector<char/*bool*/> point_convexity;

        // Maps point coordinates to their indices, to deduplicate them.
        // CDT will assert if `points` contains ANY duplicates, even in different loops.
        // This is optional, see `InsertionCallback` below.
        phmap::flat_hash_map<vec2<T>, VertexId> point_map;

        void Reserve(std::size_t num_points, std::size_t num_edges)
        {
            points.reserve(num_points);
            point_convexity.reserve(num_points);
            point_map.reserve(num_points);

            edges.reserve(num_edges);
        }
        // This passes the same number of points and edges. Most of the time this should be good enough.
        void Reserve(std::size_t n)
        {
            Reserve(n, n);
        }

        // Use this instead of `Reserve()` if you're going to pass `deduplicate_points == false` to `InsertionCallback`.
        void ReserveNoDuplicates(std::size_t num_points)
        {
            points.reserve(num_points);
            point_convexity.reserve(num_points);
            edges.reserve(num_points);
            // Skipping `point_map`.
        }

        void Reset()
        {
            points.clear();
            point_convexity.clear();
            point_map.clear();

            edges.clear();
        }

        // Returns a callback to insert new elements, compatible with `TilesToEdges`.
        // Automatically removes redundant points.
        // The returned callback is `(vec2<T> pos, PointInfo info) -> void`. Loops must be closed by repeating the first vertex with `info.last == true`.
        // NOTE: The callback is stateful, keep it alive for all insertion operations (or if `deduplicate_points == false`, for at least one whole loop).
        // While `deduplicate_points == true` removes zero-length edges, it's not its primary purpose.
        // You must enable it if you have points with same coordinates anywhere, even not immediately after each other.
        // CDT (the triangulation library) will assert on duplicate points if you set this to `false` and pass them.
        [[nodiscard]] auto InsertionCallback(bool deduplicate_points = true)
        {
            return SimplifyStraightEdges<T>([
                this,
                prev_vertex = VertexId::invalid,
                first_vertex = VertexId::invalid,
                deduplicate_points
            ](vec2<T> pos, PointInfo info, bool convex) mutable
            {
                assert(info.closed && "The input contours must be closed.");
                assert((info.type == PointType::normal || info.type == PointType::last) && "Weird point type.");

                bool is_first = first_vertex == VertexId::invalid;
                bool is_last = info.type == PointType::last;
                ASSERT(is_first + is_last <= 1, "A degenerate edge loop?");

                VertexId this_vertex;
                if (is_last)
                {
                    this_vertex = first_vertex;
                }
                else
                {
                    this_vertex = VertexId(points.size());
                    bool point_is_new = true;
                    if (deduplicate_points)
                    {
                        if (auto result = point_map.try_emplace(pos, VertexId(this_vertex)); !result.second)
                        {
                            this_vertex = result.first->second;
                            point_is_new = false;
                        }
                    }
                    if (point_is_new)
                    {
                        points.push_back(pos);
                        point_convexity.push_back(convex);
                    }
                    if (is_first)
                        first_vertex = this_vertex;
                }

                if (!is_first)
                {
                    if (!deduplicate_points || prev_vertex != this_vertex)
                        edges.push_back({VertIndex(prev_vertex), VertIndex(this_vertex)});
                }

                if (is_last)
                    first_vertex = VertexId::invalid;
                else
                    prev_vertex = this_vertex;
            });
        }
    };

    // Must manually specify output type T. CDT requires it to be floating-point.
    template <std::floating_point T, typename U>
    [[nodiscard]] CDT::Triangulation<T> Triangulate(const TriangulationInput<U> &input)
    {
        CDT::Triangulation<T> ret;

        // I insert everything at once, because the first insertion is somehow privileged? Weird.
        // Not sure if this is actually beneficial, but it probably is (because otherwise it probably updates the triangulation on every insertion).
        ret.insertVertices(
            input.points.begin(), input.points.end(),
            [](const vec2<T> &v){return T(v.x);},
            [](const vec2<T> &v){return T(v.y);}
        );
        ret.insertEdges(
            input.edges.begin(), input.edges.end(),
            [](const TriangulationInput<T>::Edge &e) {return e.first;},
            [](const TriangulationInput<T>::Edge &e) {return e.second;}
        );
        ret.eraseOuterTrianglesAndHoles(); // This finalizes the triangulation.

        return ret;
    }


    // Actually a half-edge.
    struct Edge
    {
        // The origin vertex.
        VertexId origin_vert = VertexId::invalid;

        // Whether the origin vertex is flat in this polygon (previous edge has the same direction as this one).
        bool origin_vert_is_flat = false;

        // The edge of the neighbor face, with the opposite direction.
        // If this is invalid, there's no neighbor in this direction.
        EdgeId neighbor = EdgeId::invalid;

        // Next edge in this polygon (in `face`).
        EdgeId prev = EdgeId::invalid;
        // Previous edge in this polygon (in `face`).
        EdgeId next = EdgeId::invalid;

        // Face owning this edge (on the right side of it, if Y points downwards).
        FaceId face = FaceId::invalid;

        [[nodiscard]] friend bool operator==(const Edge &, const Edge &) = default;
    };

    struct Face
    {
        // One of the edges. Its `side_face` matches this face.
        EdgeId any_edge = EdgeId::invalid;

        // The number of vertices in this polygon, ignoring flat vertices.
        int num_nonflat_verts = 0;

        [[nodiscard]] friend bool operator==(const Face &, const Face &) = default;
    };

    class Topology
    {
        // Indexed by `EdgeId`. Those are actually half-edges.
        std::vector<Edge> edges;

        // Indexed by `FaceId`.
        std::vector<Face> faces;

        SparseSet<std::underlying_type_t<EdgeId>> edge_set;
        SparseSet<std::underlying_type_t<FaceId>> face_set;

      public:
        Topology() {}

        // Get triangles from the results of triangulation.
        template <typename T>
        Topology(const CDT::Triangulation<T> &input)
        {
            assert(input.triangles.size() > 0); // Not strictly necessary?
            if (input.triangles.empty())
                return;

            // Reserve memory:

            faces.resize(input.triangles.size());
            face_set.Reserve(faces.size());

            edges.resize(input.triangles.size() * 3);
            edge_set.Reserve(edges.size());

            for (std::size_t i = 0; i < input.triangles.size(); i++)
            {
                face_set.Insert(i);
                faces[i].num_nonflat_verts = 3;

                for (int j = 0; j < 3; j++)
                {
                    EdgeId edge_id = EdgeId(i * 3 + j);
                    if (j == 0)
                        faces[i].any_edge = edge_id;

                    edge_set.Insert(std::to_underlying(edge_id));

                    Edge &edge = edges[std::to_underlying(edge_id)];

                    edge.face = FaceId(i);
                    edge.next = EdgeId(i * 3 + (j + 1) % 3);
                    edge.prev = EdgeId(i * 3 + (j + 2) % 3);
                    edge.origin_vert = VertexId(input.triangles[i].vertices[j]);

                    CDT::TriInd neighbor_triangle_index = input.triangles[i].neighbors[j];
                    if (neighbor_triangle_index != CDT::invalidIndex && neighbor_triangle_index < i)
                    {
                        // If the neighbor exists and was already generated...

                        const CDT::Triangle &input_neighbor_ref = input.triangles[neighbor_triangle_index];

                        int k = 0;
                        while (k < 3)
                        {
                            if (input_neighbor_ref.neighbors[k] == i)
                            {
                                EdgeId neighbor_edge_id = EdgeId(neighbor_triangle_index * 3 + k);

                                edge.neighbor = neighbor_edge_id;
                                edges[std::to_underlying(neighbor_edge_id)].neighbor = edge_id;
                                break;
                            }

                            k++;
                        }
                        assert(k != 3 && "Neighbor roundtrip failed.");
                    }
                }
            }
        }

        // Combine convex polygons to bigger convex polygons.
        // We read vertices and convexity data from the `input`.
        // `max_verts_per_polygon` is the maximum amount of (non-flat) vertices per polygon, or negative if no limit.
        template <typename T>
        void CombineToConvexPolygons(const TriangulationInput<T> &input, int max_verts_per_polygon)
        {
            struct EdgeEntry
            {
                EdgeId edge_id;
                T length_sq = 0;
            };

            std::vector<EdgeEntry> edge_queue;
            // Divide by two because we skip mirrored edges. We also don't need space for the contour edges, hence the subtraction.
            edge_queue.reserve((edge_set.ElemCount() - input.edges.size()) / 2);

            // Whether merging the two faces would produce a face with number of edges (same as verts) <= `max_verts_per_polygon + extra_allowance`.
            auto SumVertsNumberIsOk = [&](FaceId a, FaceId b, int extra_allowance) -> bool
            {
                if (max_verts_per_polygon < 0)
                    return true;
                return GetFace(a).num_nonflat_verts + GetFace(b).num_nonflat_verts - 2 <= max_verts_per_polygon + extra_allowance;
            };

            for (std::underlying_type_t<EdgeId> i = 0; i < edge_set.ElemCount(); i++)
            {
                EdgeId edge_id = EdgeId(edge_set.GetElem(i));
                const Edge &edge = GetEdge(edge_id);
                if (edge.neighbor == EdgeId::invalid)
                    continue; // This the border of the shape, don't want to remove this edge.
                if (edge.neighbor < edge_id)
                    continue; // Skip mirrored edges. Must process smaller edge first to avoid screwing up iteration order.

                const Edge &neighbor_edge = GetEdge(edge.neighbor);

                if (!SumVertsNumberIsOk(edge.face, neighbor_edge.face, 0)) // Initial triangles don't have flat vertices, therefore `0`.
                    continue; // Merging those polygons would create a polygon with too many edges.

                // If both points are convex in the original shape, destroy the edge early.
                if (input.point_convexity[std::to_underlying(edge.origin_vert)] && input.point_convexity[std::to_underlying(neighbor_edge.origin_vert)])
                {
                    CollapseEdge(edge_id);
                    i--;
                    continue;
                }

                edge_queue.push_back({
                    .edge_id = edge_id,
                    .length_sq = (input.points[std::to_underlying(edge.origin_vert)] - input.points[std::to_underlying(neighbor_edge.origin_vert)]).len_sq(),
                });
            }

            // Sort the queue to have longer edges first.
            // One guy on stackoverflow says this is a decent metric.
            std::ranges::sort(edge_queue, std::greater{}, &EdgeEntry::length_sq);

            auto GetEdgeCurvature = [&](VertexId va, VertexId vb, VertexId vc) -> int
            {
                vec2<T> a = input.points[std::to_underlying(va)];
                vec2<T> b = input.points[std::to_underlying(vb)];
                vec2<T> c = input.points[std::to_underlying(vc)];
                return sign((b - a) /cross/ (c - b));
            };

            for (const EdgeEntry &entry : edge_queue)
            {
                Edge &edge = GetMutEdge(entry.edge_id);
                Edge &neighbor_edge = GetMutEdge(edge.neighbor);

                int ca = GetEdgeCurvature(GetEdge(edge.prev).origin_vert, edge.origin_vert, GetEdge(GetEdge(neighbor_edge.next).next).origin_vert);
                int cb = GetEdgeCurvature(GetEdge(neighbor_edge.prev).origin_vert, neighbor_edge.origin_vert, GetEdge(GetEdge(edge.next).next).origin_vert);

                if (ca >= 0 && cb >= 0)
                {
                    int num_flattened_verts = 0;

                    if (ca == 0)
                    {
                        Edge &e = GetMutEdge(neighbor_edge.next);
                        assert(!e.origin_vert_is_flat);
                        if (!e.origin_vert_is_flat)
                        {
                            e.origin_vert_is_flat = true;
                            num_flattened_verts++;
                        }
                    }
                    if (cb == 0)
                    {
                        Edge &e = GetMutEdge(edge.next);
                        assert(!e.origin_vert_is_flat);
                        if (!e.origin_vert_is_flat)
                        {
                            e.origin_vert_is_flat = true;
                            num_flattened_verts++;
                        }
                    }

                    if (SumVertsNumberIsOk(edge.face, neighbor_edge.face, num_flattened_verts))
                    {
                        Face &face = GetMutFace(edge.face);
                        CollapseEdge(entry.edge_id);

                        face.num_nonflat_verts -= num_flattened_verts;
                    }
                }
            }
        }

        // Output each polygin into `func`. Takes vertex coordinates from `input`.
        // `func` is `(vec2<T> pos, PointInfo info)`, and the first point of each loop is repeated at the end of it with `last == true`.
        // NOTE: Since `CombineToConvexPolygons()` can result in redundant edges (several edges in a row in the same direction),
        //   consider piping the output through `SimplifyStraightEdges()` to fix that.
        template <typename T, typename F>
        void DumpPolygons(const TriangulationInput<T> &input, F &&func) const
        {
            for (std::underlying_type_t<FaceId> i = 0; i < face_set.ElemCount(); i++)
            {
                const Edge *first = &GetEdge(GetFace(FaceId(face_set.GetElem(i))).any_edge);

                // Find the first non-flat vertex.
                while (first->origin_vert_is_flat)
                    first = &GetEdge(first->next);

                const Edge *cur = first;

                do
                {
                    if (!cur->origin_vert_is_flat)
                        func(input.points[std::to_underlying(cur->origin_vert)], PointInfo{.type = PointType::normal, .closed = true});
                    cur = &GetEdge(cur->next);
                }
                while (cur != first);

                func(input.points[std::to_underlying(cur->origin_vert)], PointInfo{.type = PointType::last, .closed = true});
            }
        }

        // Get constant elements:

        [[nodiscard]] const Edge &GetEdge(EdgeId id) const {return const_cast<Topology &>(*this).GetMutEdge(id);}
        [[nodiscard]] const Face &GetFace(FaceId id) const {return const_cast<Topology &>(*this).GetMutFace(id);}

      private:
        // Get mutable elements:

        [[nodiscard]] Edge &GetMutEdge(EdgeId id)
        {
            assert(id != EdgeId::invalid);
            assert(std::to_underlying(id) < edges.size());
            return edges[std::to_underlying(id)];
        }
        [[nodiscard]] Face &GetMutFace(FaceId id)
        {
            assert(id != FaceId::invalid);
            assert(std::to_underlying(id) < faces.size());
            return faces[std::to_underlying(id)];
        }

      public:
        // Destroys an edge between two polygons, combining them.
        // The `GetEdge(edge_id).face` face keeps its id, and the face on the other side of the edge gets destroyed.
        void CollapseEdge(EdgeId edge_id)
        {
            Edge &edge = GetMutEdge(edge_id);
            assert(edge.face != FaceId::invalid);
            EdgeId neighbor_edge_id = edge.neighbor;
            Edge &neighbor_edge = GetMutEdge(neighbor_edge_id);
            assert(neighbor_edge.face != FaceId::invalid);

            FaceId kept_face_id = edge.face;
            FaceId destroyed_face_id = neighbor_edge.face;

            Face &kept_face = GetMutFace(kept_face_id);
            Face &destroyed_face = GetMutFace(destroyed_face_id);

            { // Replace all `Edge::face`s in the killed polygon with the other polygon's id.
                Edge *cur = &neighbor_edge;
                do
                {
                    assert(cur->face == destroyed_face_id);
                    cur->face = kept_face_id;
                    cur = &GetMutEdge(cur->next);
                }
                while (cur != &neighbor_edge);
            }

            // Fix up prev/next IDs.
            GetMutEdge(edge.next).prev = neighbor_edge.prev;
            GetMutEdge(edge.prev).next = neighbor_edge.next;
            GetMutEdge(neighbor_edge.next).prev = edge.prev;
            GetMutEdge(neighbor_edge.prev).next = edge.next;

            // Fix up face-to-edge mapping.
            // Just set to any related edge, in case the edge we're deleting is the one currently stored in the face.
            kept_face.any_edge = edge.next;

            // Update the vertex count.
            kept_face.num_nonflat_verts += destroyed_face.num_nonflat_verts;
            kept_face.num_nonflat_verts -= 2;

            // Destroy the face.
            face_set.EraseUnordered(std::to_underlying(destroyed_face_id));
            destroyed_face = {}; // Zero the face, just in case.

            // Destroy the edge.
            edge_set.EraseUnordered(std::to_underlying(edge_id));
            edge_set.EraseUnordered(std::to_underlying(neighbor_edge_id));
            edge = {}; // Zero the edges, just in case.
            neighbor_edge = {}; // ^
        }


        // Some minimal tests.
        static void RunTests()
        {
            CDT::Triangulation<float> tr;
            tr.insertVertices({
                {0,0},
                {1,0},
                {1,1},
                {0,1},
            });
            tr.insertEdges({
                {0,1},
                {2,3},
                {1,2},
                {3,0},
            });
            tr.eraseOuterTrianglesAndHoles();

            Topology topo(tr);

            assert(topo.face_set.Capacity() == 2);
            assert(topo.face_set.ElemCount() == 2);
            assert((topo.faces == std::vector{
                Face{ .any_edge = EdgeId(2) },
                Face{ .any_edge = EdgeId(5) },
            }));
            assert((topo.edges == std::vector{
                /*0*/ Edge{ .origin_vert = VertexId(3), .neighbor = EdgeId(3),       .prev = EdgeId(2), .next = EdgeId(1), .face = FaceId(0) },
                /*1*/ Edge{ .origin_vert = VertexId(1), .neighbor = EdgeId::invalid, .prev = EdgeId(0), .next = EdgeId(2), .face = FaceId(0) },
                /*2*/ Edge{ .origin_vert = VertexId(2), .neighbor = EdgeId::invalid, .prev = EdgeId(1), .next = EdgeId(0), .face = FaceId(0) },
                /*3*/ Edge{ .origin_vert = VertexId(1), .neighbor = EdgeId(0),       .prev = EdgeId(5), .next = EdgeId(4), .face = FaceId(1) },
                /*4*/ Edge{ .origin_vert = VertexId(3), .neighbor = EdgeId::invalid, .prev = EdgeId(3), .next = EdgeId(5), .face = FaceId(1) },
                /*5*/ Edge{ .origin_vert = VertexId(0), .neighbor = EdgeId::invalid, .prev = EdgeId(4), .next = EdgeId(3), .face = FaceId(1) },
            }));
        }
    };


    // A single function to perform edge-to-polygon conversion.
    // `T` is the internal type for triangulation, must be floating-point, usually `float`. (CDT library requires floating-point input.)
    //   It doesn't have to match the coordinate type (of `tri_input`), which can be e.g. `int` (or `float` too).
    // `tri_input` holds the input data.
    // `max_verts_per_polygon`, if positive, limits the number of vertices per polygon (use `b2_maxPolygonVertices` for Box2D).
    // `output` receives the output vertices. It's `(vec2<T> pos, PointInfo info) -> void`.
    //   It receives vertex loops, with the last vertex of each loop being the same as the first one, with `info.last == true`.
    // NOTE: Both input and output is automatically stripped of redundant vertices, you DON'T need to use `SimplifyStraightEdges`.
    template <std::floating_point T, typename U, typename F>
    void ConvertToPolygons(const TriangulationInput<U> &tri_input, int max_verts_per_polygon, F &&output)
    {
        auto tri = Triangulate<T>(tri_input);
        Topology topo(tri);
        topo.CombineToConvexPolygons(tri_input, max_verts_per_polygon);
        topo.DumpPolygons(tri_input, output);
    }
}

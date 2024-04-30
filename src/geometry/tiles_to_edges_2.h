#pragma once

#include "utils/mat.h"
#include "utils/multiarray.h"

#include <cassert>
#include <utility>
#include <vector>

// Converts tile maps to edge loops (closed or open), good for Box2D "chain" shapes (for static tiles).
// How to use:
// * Construct `Tileset`.
// * Bake it into `BakedTileset`.
// * Call `ConvertTilesToEdges(...)`.
// We can operate in two modes: either outputting only closed loops,
//   or peeking one tile outside of the map and outputting both closed and open loops (open when the tiles are on the map edge).
// The latter is good for chunking.
// NOTE: The resulting points can be redundant, consider feeding them to `SimplifyStraightEdges()`
//   (but NOT if you're converting them to polygons with `EdgesToPolygons`, that simplifies them automatically).

namespace Geom::TilesToEdges2
{
    struct Tileset
    {
        // Size of each tile.
        ivec2 tile_size;

        // Possible points inside of a tile.
        std::vector<ivec2> vertices;

        // Tile types -> vertex lops -> individual vertex indices.
        // Must use a specific winding direction (clockwise if Y points downwards).
        // Adjacent tiles must use the same points in their edges. E.g.:
        //
        //    Bad:       Good:
        //  ---o       ---o
        //     |  .'      |  .'
        //     |o'        oo'
        //     ||         ||
        //  ---oo---   ---oo---
        std::vector<std::vector<std::vector<std::uint32_t>>> tiles;
    };

    struct BakedTileset
    {
        // Size of each tile.
        ivec2 tile_size;

        // Possible points inside of a tile.
        std::vector<ivec2> vertices;

        enum class VertexId : std::uint32_t {invalid = std::uint32_t(-1)};
        enum class EdgeId : std::uint32_t {invalid = std::uint32_t(-1)};
        enum class TileId : std::uint32_t {invalid = std::uint32_t(-1)};

        struct EdgeType
        {
            // Start vertex ID.
            VertexId vert_a = VertexId::invalid;
            // End vertex ID.
            VertexId vert_b = VertexId::invalid;

            // The ID of the mirror edge on an adjacent tile, or -1 if none.
            EdgeId opposite_edge = EdgeId::invalid;
            // If the `opposite_edge` is specified, this is the offset direction for it (one of `ivec2::dir(...)`).
            ivec2 opposite_edge_dir;
        };
        // Edge types, indexed by edge IDs.
        std::vector<EdgeType> edge_types;

        struct EdgeConnectivity
        {
            // Prev and next edges in the same tile.
            EdgeId prev = EdgeId::invalid;
            EdgeId next = EdgeId::invalid;

            [[nodiscard]] EdgeId PrevOrNext(bool is_next) const {return is_next ? next : prev;}
        };

        // Maps an edge ID and a tile ID to the next/prev edges in the same tile.
        Array2D<EdgeConnectivity> edge_connectivity;

        // For each tile, a list of edges, one artbirary edge per edge loop.
        std::vector<std::vector<EdgeId>> tile_starting_edges;

        BakedTileset() {}

        explicit BakedTileset(Tileset &&input);

        // Number of registered tile types.
        [[nodiscard]] std::underlying_type_t<TileId> NumTileTypes() const
        {
            return tile_starting_edges.size();
        }
        // Number of registered edge types.
        [[nodiscard]] std::underlying_type_t<EdgeId> NumEdgeTypes() const
        {
            return edge_connectivity.size().x;
        }
        // Number of registered edge types.
        [[nodiscard]] std::underlying_type_t<VertexId> NumVertexTypes() const
        {
            return vertices.size();
        }

        // Returns a vertex by its ID.
        [[nodiscard]] ivec2 GetVertexPos(VertexId vert_id) const
        {
            assert(vert_id >= VertexId{} && vert_id < VertexId(NumVertexTypes()));
            return vertices[std::to_underlying(vert_id)];
        }

        // Given a tile type, for each edge loop in it, returns one arbitrary edge.
        [[nodiscard]] const std::vector<EdgeId> &GetTileStartingEdges(TileId tile) const
        {
            assert(tile >= TileId{} && tile < TileId(NumTileTypes()));
            return tile_starting_edges[std::to_underlying(tile)];
        }

        // Returns an information about an edge type.
        [[nodiscard]] const EdgeType &GetEdgeInfo(EdgeId edge) const
        {
            assert(edge >= EdgeId{} && edge < EdgeId(NumEdgeTypes()));
            return edge_types[std::to_underlying(edge)];
        }

        // Given a tile type and an edge, tell which edges it connects to in this tile.
        [[nodiscard]] const EdgeConnectivity &GetEdgeConnectivity(TileId tile, EdgeId edge) const
        {
            assert(tile >= TileId{} && tile < TileId(NumTileTypes()));
            assert(edge >= EdgeId{} && edge < EdgeId(NumEdgeTypes()));
            return edge_connectivity.safe_nonthrowing_at(xvec2(std::to_underlying(edge), std::to_underlying(tile)));
        }

        // Whether a specific tile type has a specific edge.
        [[nodiscard]] bool TileHasEdge(TileId tile, EdgeId edge) const
        {
            return GetEdgeConnectivity(tile, edge).next != EdgeId::invalid;
        }

        // Follow an edge loop in a tile.
        // Calls `func` once for every edge in the loop, `func` is `(EdgeId edge_id) -> bool`. If it returns true, stop and also return true.
        template <typename F>
        bool ForEveryEdgeInEdgeLoop(TileId tile, EdgeId edge, F &&func) const
        {
            EdgeId cur_edge = edge;
            do
            {
                if (func(std::as_const(cur_edge)))
                    return true;
                const EdgeConnectivity &info = GetEdgeConnectivity(tile, cur_edge);
                assert(info.next != EdgeId::invalid);
                cur_edge = info.next;
            }
            while (cur_edge != edge);
            return false;
        }
    };

    enum class Mode
    {
        // Don't read outside of the specified region, assume it's the complete map.
        // Produce closed loops, where the last vertex is the same as the first one, with `last == true`.
        closed,
        // Read 1 tile outside of the specified region, produce open loops. Good for splitting the map into chunks.
        // Produce either closed loops (when they're fully inside the region) or open loops (when they touch the edge of the map).
        // The format for closed loops is the same as in `Mode::closed` while for open loops the last vertex simply doesn't match the first one.
        // The first and last edge of an open loop are one tile outside of the specified map region.
        open,
    };

    // Coverts tiles to edges.
    // See the commends in `enum class Mode` above for the explanation of modes.
    // `region_size` is the tile rectangle size we're processing.
    // `input` is `(ivec2 pos) -> SomeIntegralType`, it reads the tile as the specified coordinates. The result will be casted to `BakedTileset::TileId`.
    // NOTE: When `mode == open`, `pos` can be one tile outside of the specified bounds.
    // `output` receives the resulting vertices. It's `(ivec2 pos, bool last) -> void`, and it's meaning depends on the `mode` (see above).
    template <typename F, typename G>
    void ConvertTilesToEdges(const BakedTileset &tileset, Mode mode, ivec2 region_size, F &&input, G &&output)
    {
        // Check if `tile_pos` is in bounds.
        auto TileIsInBounds = [&](ivec2 tile_pos) -> bool
        {
            return tile_pos(all) >= 0 && tile_pos(all) < region_size;
        };

        // Get a tile at a specific position using `input(...)`.
        auto GetTileAt = [&](ivec2 tile_pos) -> BakedTileset::TileId
        {
            // Check the bounds, just in case.
            // In `mode == open`, we can look one tile outside of the `region_size`.
            assert(mode == Mode::closed ? TileIsInBounds(tile_pos) : tile_pos(any) >= -1 && tile_pos(any) <= region_size);

            return BakedTileset::TileId(input(std::as_const(tile_pos)));
        };

        struct Cursor
        {
            ivec2 tile_pos;
            BakedTileset::TileId tile = BakedTileset::TileId::invalid; // Caches the tile ID at `tile_pos`.
            BakedTileset::EdgeId edge = BakedTileset::EdgeId::invalid;
        };
        // Moves forward or backward along an edge loop, across tiles.
        // Returns true on success. Can return false only in `mode == open`, when reaching a map boundary.
        auto MoveToNextEdge = [&](Cursor &cur, bool forward) -> bool
        {
            while (true)
            {
                cur.edge = tileset.GetEdgeConnectivity(cur.tile, cur.edge).PrevOrNext(forward);

                const BakedTileset::EdgeType &edge_info = tileset.GetEdgeInfo(cur.edge);
                if (edge_info.opposite_edge == BakedTileset::EdgeId::invalid)
                    return true; // No opposite edge for this edge type, nothing to check.
                ivec2 next_tile_pos = cur.tile_pos + edge_info.opposite_edge_dir;
                if (mode == Mode::closed && !TileIsInBounds(next_tile_pos))
                    return true; // The possible opposite edge is outside of the map, will not check it.
                BakedTileset::TileId next_tile = GetTileAt(next_tile_pos);
                if (!tileset.TileHasEdge(next_tile, edge_info.opposite_edge))
                    return true; // The adjacent tile doesn't have our opposite edge.

                // At this point the adjacent tile DOES have our opposite edge.
                cur.tile_pos = next_tile_pos;
                cur.tile = next_tile;
                cur.edge = edge_info.opposite_edge;
            }
        };

        using EdgeBitMaskType = std::uint64_t;
        assert(tileset.NumEdgeTypes() <= sizeof(EdgeBitMaskType) * 8);

        Array2D<EdgeBitMaskType> visited_edges(region_size);

        for (const ivec2 starting_tile_pos : vector_range(region_size))
        {
            const BakedTileset::TileId tile = GetTileAt(starting_tile_pos);

            for (const BakedTileset::EdgeId loop_starting_edge : tileset.GetTileStartingEdges(tile))
            {
                tileset.ForEveryEdgeInEdgeLoop(tile, loop_starting_edge, [&](const BakedTileset::EdgeId starting_edge)
                {
                    if ((visited_edges.safe_nonthrowing_at(starting_tile_pos) >> std::to_underlying(starting_edge)) & 1)
                        return false; // Already visited this edge.

                    const BakedTileset::EdgeType &starting_edge_info = tileset.GetEdgeInfo(starting_edge);

                    // Make sure this edge is not covered by an opposite edge on an adjacent tile.
                    if (starting_edge_info.opposite_edge != BakedTileset::EdgeId::invalid)
                    {
                        ivec2 other_tile_pos = starting_tile_pos + starting_edge_info.opposite_edge_dir;

                        // Make sure we don't go outside of the boundary when `mode == closed`.
                        if (mode == Mode::open || TileIsInBounds(other_tile_pos))
                        {
                            if (tileset.TileHasEdge(GetTileAt(other_tile_pos), starting_edge_info.opposite_edge))
                                return false; // The opposite edge covers this one, don't visit it.
                        }
                    }

                    Cursor cursor{
                        .tile_pos = starting_tile_pos,
                        .tile = tile,
                        .edge = starting_edge,
                    };
                    auto CursorIsAtStart = [&]
                    {
                        return cursor.tile_pos == starting_tile_pos && cursor.edge == starting_edge;
                    };

                    auto PointEmittingLoop = [&](bool loop_is_closed)
                    {
                        bool skip_one_visited_write = !loop_is_closed;
                        do
                        {
                            // Output vertex.
                            output(tileset.GetVertexPos(tileset.GetEdgeInfo(cursor.edge).vert_a) + cursor.tile_pos * tileset.tile_size, false);

                            // Mark edge as visited.
                            if (skip_one_visited_write)
                                skip_one_visited_write = false;
                            else
                                visited_edges.safe_nonthrowing_at(cursor.tile_pos) |= 1 << std::to_underlying(cursor.edge);

                            // Move to the next edge.
                            MoveToNextEdge(cursor, true);
                        }
                        while (loop_is_closed ? !CursorIsAtStart() : TileIsInBounds(cursor.tile_pos));

                        // Output the final vertex.
                        output(tileset.GetVertexPos(tileset.GetEdgeInfo(cursor.edge).vert_a) + cursor.tile_pos * tileset.tile_size, loop_is_closed);

                        // When in an open loop, output the actually final vertex.
                        if (!loop_is_closed)
                            output(tileset.GetVertexPos(tileset.GetEdgeInfo(cursor.edge).vert_b) + cursor.tile_pos * tileset.tile_size, true);
                    };

                    if (mode == Mode::closed)
                    {
                        PointEmittingLoop(true);
                    }
                    else // mode == Mode::open
                    {
                        // Backtrack until we go out of bounds or do a full circle.
                        do
                        {
                            MoveToNextEdge(cursor, false);
                        }
                        while (!CursorIsAtStart() && TileIsInBounds(cursor.tile_pos));

                        // Then start emitting points.
                        PointEmittingLoop(CursorIsAtStart());
                    }

                    return false;
                });
            }
        }
    }
}

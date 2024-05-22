#pragma once

#include "graph/pathfinding.h"
#include "macros/enum_flag_operators.h"
#include "meta/common.h"
#include "utils/hash.h"
#include "utils/mat.h"

#include <parallel_hashmap/phmap.h>

#include <cstdint>

namespace TileGrids
{
    // Index for unique connectivity component inside of a chunk.
    enum class ComponentIndex : std::uint16_t {invalid = std::uint16_t(-1)};

    // Uniqely describes a tile edge along all one of the 4 chunk borders.
    enum class BorderEdgeIndex : std::uint16_t {};

    // A list of tiles and the AABB of a single connectivity component in a chunk.
    template <std::integral CoordType>
    class Component
    {
        std::vector<vec2<CoordType>> tiles;
        rect2<CoordType> bounds;

      public:
        constexpr Component() {}

        [[nodiscard]] const std::vector<vec2<CoordType>> &GetTiles() const {return tiles;}
        [[nodiscard]] rect2<CoordType> GetBounds() const {return bounds;}

        void AddTile(vec2<CoordType> tile)
        {
            if (tiles.empty())
                bounds = tile.tiny_rect();
            else
                bounds = bounds.combine(tile);

            tiles.push_back(tile);
        }
    };

    // A list of connectivitiy components in a chunk, and the information about which of them touch which border edges.
    template <int N, std::integral CoordType>
    struct ChunkComponents
    {
        // The list of tiles in a component.
        using ComponentType = Component<CoordType>;

        // A connectivity mask of a single tile edge.
        enum class TileEdgeConnectivity : std::uint8_t {};
        IMP_ENUM_FLAG_OPERATORS_IN_CLASS(TileEdgeConnectivity)

        static constexpr BorderEdgeIndex num_border_edge_indices = BorderEdgeIndex(N * 4);

        // Bakes 4-dir plus offset along a chunk border into a single integer.
        // The integers are sequental, so we can have an array of them.
        [[nodiscard]] static BorderEdgeIndex GetBorderEdgeIndex(int dir, CoordType x_or_y)
        {
            return BorderEdgeIndex((dir & 3) | x_or_y << 2);
        }

        // Extract a 4-dir from a border edge index.
        [[nodiscard]] static int GetDirFromBorderEdgeIndex(BorderEdgeIndex index)
        {
            return int(std::to_underlying(index) & 2);
        }

        struct ComponentEdgeInfo
        {
            // Which edge this is.
            BorderEdgeIndex edge_index{};

            // Connectivity of this tile in this direction (regardless of what the opposing tile is).
            TileEdgeConnectivity conn_mask{};
        };

        struct ComponentEntry
        {
            ComponentType component;

            std::vector<ComponentEdgeInfo> border_edges;
        };

        // For each component, the list of tiles it contains, and the list of broder edges with connectivity masks.
        std::vector<ComponentEntry> components;

        struct BorderEdgeInfo
        {
            // Which component this is, or `invalid` if none.
            ComponentIndex component_index = ComponentIndex::invalid;

            // Connectivity of this tile in this direction (regardless of what the opposing tile is).
            TileEdgeConnectivity conn_mask{};
        };
        // Maps border edge index to component index (if any) and the connectivity mask.
        std::array<BorderEdgeInfo, std::size_t(num_border_edge_indices)> border_edge_info;

        // Removes a component, by swapping an index with the last component and popping that.
        void RemoveComponent(ComponentIndex i)
        {
            ASSERT(i >= ComponentIndex{} && i < ComponentIndex(components.size()), "Component index is out of range.");

            // Clean up edges.
            ComponentEntry &comp = components[std::to_underlying(i)];
            for (const ComponentEdgeInfo &edge : comp.border_edges)
                border_edge_info[std::to_underlying(edge.edge_index)] = {};

            // Do we need to swap with the last component?
            if (std::to_underlying(i) != components.size() - 1)
            {
                // Re-number the edges of the last component.
                for (const ComponentEdgeInfo &edge : components.back().border_edges)
                    border_edge_info[std::to_underlying(edge.edge_index)].component_index = i;

                std::swap(comp, components.back());
            }

            components.pop_back();
        }
    };

    // A single chunk as a grid of `CellType` objects of size N*N.
    template <int N, typename CellType, std::integral CoordType>
    class Chunk
    {
        std::array<std::array<CellType, N>, N> cells;

      public:
        using ComponentsType = ChunkComponents<N, CoordType>;

        static constexpr vec2<CoordType> size = vec2<CoordType>(N);

        // Access a cell.
        [[nodiscard]] auto at(this auto &&self, vec2<CoordType> pos) -> Meta::copy_cvref_qualifiers<decltype(self), CellType>
        {
            ASSERT(pos(all) >= CoordType(0) && pos(all) < CoordType(N), "Cell position in a chunk is out of bounds.");
            return static_cast<Meta::copy_cvref_qualifiers<decltype(self), CellType>>(self.cells[pos.y][pos.x]);
        }

        struct ConnectedComponentsReusedData
        {
            std::array<std::array<bool, N>, N> visited{};

            std::array<vec2<CoordType>, N * N> queue{};
            std::size_t queue_pos = 0;
        };

        // Compute the individual connectivity components within a chunk.
        void ComputeConnectedComponents(
            // Just the memory reused between calls for performance. Allocate it once (probably on the heap) and reuse between calls.
            ConnectedComponentsReusedData &reused,
            // Either a `ComponentsType` or `ComponentsType::ComponentType`.
            // The latter will contain less information, and is a bit cheaper to compute. Use the latter when this is a standalone chunk,
            //   not a part of a grid.
            // The latter can hold at most one component at a time, so you must move them elsewhere in `component_done()` (see below).
            Meta::same_as_any_of<ComponentsType, typename ComponentsType::ComponentType> auto &out,
            // `() -> void`. This is called after finishing writing each component to `out`.
            // If `out` is a `ComponentsType::ComponentType` you must move it elsewhere in this callback, else it will get overwritten.
            // If `out` is a `ComponentsType`, you don't have to do anything, as it can store multiple components.
            //   But you CAN `.RemoveComponent()` the newly added component, e.g. if you just separated it to a new entity.
            auto &&component_done,
            // `(const CellType &cell) -> bool`. Returns true if the tile exists, false if it's empty and can be ignored.
            auto &&tile_exists,
            // `(const CellType &cell, int dir) -> Chunk<...>::ComponentsType::TileEdgeConnectivity`.
            // For a tile, returns its connectivity mask in one of the 4 directions.
            auto &&tile_connectivity
        ) const
        {
            static constexpr bool output_full_info = std::is_same_v<decltype(out), ComponentsType &>;

            reused.visited = {};

            for (const vec2<CoordType> starting_pos : vector_range(vec2<CoordType>(N)))
            {
                if (reused.visited[starting_pos.y][starting_pos.x])
                    continue; // Already visited.
                if (!tile_exists(at(starting_pos)))
                    continue; // No tile here.

                // Only meaningful when `output_full_info == true`.
                ComponentIndex this_comp_index{};
                if constexpr (output_full_info)
                    this_comp_index = ComponentIndex(out.components.size());

                // A reference to the target set of tiles.
                typename ComponentsType::ComponentType &out_comp = [&]() -> auto &
                {
                    if constexpr (output_full_info)
                    {
                        return out.components.emplace_back().component;
                    }
                    else
                    {
                        out = {};
                        return out;
                    }
                }();

                // Only meaningful when `output_full_info == true`.
                std::vector<typename ComponentsType::ComponentEdgeInfo> *out_border_edges = nullptr;
                if constexpr (output_full_info)
                    out_border_edges = &out.components.back().border_edges;

                reused.queue_pos = 0;
                reused.queue[reused.queue_pos++] = starting_pos;
                reused.visited[starting_pos.y][starting_pos.x] = true;

                do
                {
                    const vec2<CoordType> pos = reused.queue[--reused.queue_pos];
                    out_comp.AddTile(pos);

                    for (int i = 0; i < 4; i++)
                    {
                        // Whether it's the chunk border in this direction.
                        bool is_chunk_edge =
                            i == 0 ? pos.x == N - 1 :
                            i == 1 ? pos.y == N - 1 :
                            i == 2 ? pos.x == 0 :
                                     pos.y == 0;

                        const typename ComponentsType::TileEdgeConnectivity conn_mask = tile_connectivity(at(pos), std::as_const(i));

                        // Dump information about a chunk edge, if this is one.
                        if constexpr (output_full_info)
                        {
                            if (is_chunk_edge)
                            {
                                const auto border_edge_index = ComponentsType::GetBorderEdgeIndex(i, pos[i % 2 == 0]);

                                out.border_edge_info[std::to_underlying(border_edge_index)] = {
                                    .component_index = this_comp_index,
                                    .conn_mask = conn_mask,
                                };

                                out_border_edges->push_back({
                                    .edge_index = border_edge_index,
                                    .conn_mask = conn_mask,
                                });
                            }
                        }

                        // Add adjacent tiles to the queue.
                        if (!is_chunk_edge && bool(conn_mask))
                        {
                            vec2<CoordType> next_pos = pos + vec2<CoordType>::dir4(i);

                            if (!reused.visited[next_pos.y][next_pos.x] && tile_exists(at(next_pos)) && bool(conn_mask & tile_connectivity(at(next_pos), i ^ 2)))
                            {
                                reused.visited[next_pos.y][next_pos.x] = true;
                                reused.queue[reused.queue_pos++] = next_pos;
                            }
                        }
                    }
                }
                while (reused.queue_pos > 0);

                component_done();
            }
        }
    };

    // Splits a chunk grid to several grids based on connectivity.
    // `ChunkCoordType` is for coordinates of whole chunks.
    template <std::integral ChunkCoordType>
    class ChunkGridSplitter
    {
      public:
        struct ComponentInChunkId
        {
            vec2<ChunkCoordType> chunk_coord;
            ComponentIndex comp_in_chunk = ComponentIndex::invalid;

            friend bool operator==(const ComponentInChunkId &, const ComponentInChunkId &) = default;

            // `phmap` understands this customization point.
            [[nodiscard]] friend std::size_t hash_value(const ComponentInChunkId &self)
            {
                return Hash::Compute(self.chunk_coord, self.comp_in_chunk);
            }
        };

        struct Pathfinder : Graph::Pathfinding::Pathfinder<ComponentInChunkId, ChunkCoordType, std::pair<ChunkCoordType, ChunkCoordType>>
        {
            using Base = Graph::Pathfinding::Pathfinder<ComponentInChunkId, ChunkCoordType, std::pair<ChunkCoordType, ChunkCoordType>>;
            using Base::Base;

            // `get_chunk_comps` is `(vec2<ChunkCoordType> chunk_coord) -> const ChunkComponents<...> &`.
            void Step(auto &&get_chunk_comps)
            {
                Base::Step(
                    [&](const ComponentInChunkId &pos, auto func)
                    {
                        const auto &comps = get_chunk_comps(std::as_const(pos));
                        for ()
                        #error to we bruteforce iteration over edges?
                    }
                );
            }
        };

        phmap::flat_hash_set<ComponentInChunkId> visited_components;

        // `get_chunk_comps` is `(vec2<ChunkCoordType> chunk_coord) -> const ChunkComponents<...> &`.
        void AnalyzeChunk(vec2<ChunkCoordType> chunk_coord, auto &&get_chunk_comps)
        {
            const auto &starting_comps = get_chunk_comps(std::as_const(chunk_coord));

            for (std::size_t i = 0; i < starting_comps.components.size(); i++)
            {
                ComponentInChunkId id{.chunk_coord = chunk_coord, .comp_in_chunk = ComponentIndex(i)};

                // Mark as visited, skip if already visited.
                if (!visited_components.insert(id).second)
                    continue;


            }
        }
    };
}

#pragma once

#include "meta/common.h"
#include "utils/hash.h"
#include "utils/mat.h"
#include "utils/sparse_set.h"

#include <parallel_hashmap/phmap.h>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <span>
#include <vector>

namespace TileGrids
{
    struct DefaultSystemTraits
    {
        // Tile coordinates across chunks.
        using GlobalTileCoord = int;

        // Tile coordinates inside chunks.
        using CoordInsideChunk = int;

        // Coordinates of the whole chunks in a grid.
        using WholeChunkCoord = int;

        // Connectivity mask for an individual tile edge.
        using TileEdgeConnectivity = std::uint8_t;

        // For connectivity components inside individual chunks.
        using ComponentIndexUnderlyingType = std::uint16_t;

        // Uniquely represents a border edge of a chunk. 2 bits encode one of the 4 sides, the remaining bits encode the position.
        using BorderEdgeIndexUnderlyingType = std::uint16_t;
    };

    // This class can be templated one day, to support different underlying types.
    template <typename SystemTraits = DefaultSystemTraits>
    struct System : SystemTraits
    {
        System() = delete;
        ~System() = delete;

        // Index for unique connectivity component inside of a chunk.
        enum class ComponentIndex : typename SystemTraits::ComponentIndexUnderlyingType {invalid = typename SystemTraits::ComponentIndexUnderlyingType(-1)};

        // Uniquely describes a tile edge along all one of the 4 chunk borders.
        enum class BorderEdgeIndex : std::uint16_t {};

        using typename SystemTraits::CoordInsideChunk;
        using typename SystemTraits::WholeChunkCoord;
        using typename SystemTraits::TileEdgeConnectivity;

        // Bakes 4-dir plus offset along a chunk border into a single integer.
        // The integers are sequental, so we can have an array of them.
        [[nodiscard]] static BorderEdgeIndex MakeBorderEdgeIndex(int dir, CoordInsideChunk x_or_y)
        {
            return BorderEdgeIndex((dir & 3) | x_or_y << 2);
        }

        // Extract a 4-dir from a border edge index.
        [[nodiscard]] static int GetDirFromBorderEdgeIndex(BorderEdgeIndex index)
        {
            return int(std::to_underlying(index) & 3);
        }
        // Extract the coordinate along one of the four chunk edges from a border edge index.
        [[nodiscard]] static CoordInsideChunk GetCoordFromBorderEdgeIndex(BorderEdgeIndex index)
        {
            return CoordInsideChunk(std::to_underlying(index) >> 2);
        }

        // Chunk coordinates, plus a component index inside of it,
        struct ComponentCoords
        {
            // Coordinates of this chunk.
            vec2<WholeChunkCoord> chunk_coord;

            // Index of the component in that chunk.
            ComponentIndex in_chunk_component = ComponentIndex::invalid;

            friend bool operator==(const ComponentCoords &, const ComponentCoords &) = default;

            // `phmap` uses this.
            friend std::size_t hash_value(const ComponentCoords &self)
            {
                return Hash::Compute(self.chunk_coord, self.in_chunk_component);
            }
        };

        // A list of tiles and the AABB of a single connectivity component in a chunk.
        class Component
        {
            std::vector<vec2<CoordInsideChunk>> tiles;
            rect2<CoordInsideChunk> bounds;

          public:
            constexpr Component() {}

            [[nodiscard]] const std::vector<vec2<CoordInsideChunk>> &GetTiles() const {return tiles;}
            [[nodiscard]] rect2<CoordInsideChunk> GetBounds() const {return bounds;}

            void AddTile(vec2<CoordInsideChunk> tile)
            {
                if (tiles.empty())
                    bounds = tile.tiny_rect();
                else
                    bounds = bounds.combine(tile);

                tiles.push_back(tile);
            }
        };

        // Information about a single edge (of a component) that touches the chunk boundary. A `ComponentEntry` stores a list of those.
        struct ComponentEdgeInfo
        {
            // Which edge this is.
            BorderEdgeIndex edge_index{};

            // Connectivity of this tile in this direction (regardless of what the opposing tile is).
            TileEdgeConnectivity conn_mask{};
        };

        // Information about a single connectivity component inside of a chunk.
        struct ComponentEntry
        {
            // The list of tiles.
            Component component;

            // Which border edges are in this component.
            std::vector<ComponentEdgeInfo> border_edges;
        };

        // Information about what component touches a specific edge on a chunk boundary. Chunks store lists of those.
        struct BorderEdgeInfo
        {
            // Which component this is, or `invalid` if none.
            ComponentIndex component_index = ComponentIndex::invalid;

            // Connectivity of this tile in this direction (regardless of what the opposing tile is).
            // If `component_index == invalid`, this is always zero.
            TileEdgeConnectivity conn_mask{};
        };

        // A list of connectivitiy components in a chunk, and the information about which of them touch which border edges.
        // `N` is the chunk size.
        template <int N>
        struct ChunkComponents
        {
            static constexpr BorderEdgeIndex num_border_edge_indices = BorderEdgeIndex(N * 4);

            // For each component, the list of tiles it contains, and the list of broder edges with connectivity masks.
            std::vector<ComponentEntry> components;

            // Maps border edge index to component index (if any) and the connectivity mask.
            std::array<BorderEdgeInfo, std::size_t(num_border_edge_indices)> border_edge_info;

            // Which components in the adjacent chunks touch this component.
            // This must be filled separately, using `ComputeConnectivityBetweenChunks()`.
            // The indices are: one of the 4 directions, then the component index, and the resulting vector just lists unique neighbors in any order.
            std::vector<std::vector<ComponentIndex>> neighbor_components[4];

            // Removes a component, by swapping an index with the last component and popping that.
            // NOTE: This invalidates `neighbor_components`.
            // If `assume_already_empty == true`, the component is expected to be empty (e.g. after `MoveComponentFrom()`),
            // and we trigger an assertion if it's not.
            void SwapWithLastAndRemoveComponent(ComponentIndex comp_index, bool assume_already_empty = false)
            {
                ASSERT(comp_index >= ComponentIndex{} && comp_index < ComponentIndex(components.size()), "Component index is out of range.");

                // Clean up edges.
                ComponentEntry &comp = components[std::to_underlying(comp_index)];
                if (assume_already_empty)
                {
                    ASSERT(comp.border_edges.empty(), "The component was assumed to be empty, but it's not.");
                }
                else
                {
                    for (const ComponentEdgeInfo &edge : comp.border_edges)
                        border_edge_info[std::to_underlying(edge.edge_index)] = {};
                }

                // Do we need to swap with the last component?
                if (std::to_underlying(comp_index) != components.size() - 1)
                {
                    // Re-number the edges of the last component.
                    for (const ComponentEdgeInfo &edge : components.back().border_edges)
                        border_edge_info[std::to_underlying(edge.edge_index)].component_index = comp_index;

                    std::swap(comp, components.back());
                }

                components.pop_back();
            }

            // Counts how many connections the component `i` has (to components in other chunks).
            [[nodiscard]] std::size_t GetNumConnections(ComponentIndex comp_index) const
            {
                // We could cache this somehow for a better performance?
                std::size_t ret = 0;
                for (int i = 0; i < 4; i++)
                    ret += neighbor_components[i].at(std::to_underlying(comp_index)).size();
                return ret;
            }
        };

        // Data that can be reused between calls of `ComputeConnectivityBetweenChunks()`.
        struct ComputeConnectivityBetweenChunksReusedData
        {
            phmap::flat_hash_set<std::array<ComponentIndex, 2>, Hash::Hasher<>> visited_pairs;
        };

        // Updates `.neighbor_components` in the two `ChunkComponents` objects to reflect the connectivity between the two.
        // `comps_a` and `comps_b` must be adjacent, and in the correct order (`comps_a` to the left or above the `comps_b`, with `vertical` set appropriately).
        // `reused` can be reused between calls for performance.
        // If both `comp_a` and `comp_b` are null, does nothing. If only one of them is null, zeroes the edge connectivity in the other chunk.
        template <int N>
        static void ComputeConnectivityBetweenChunks(
            ComputeConnectivityBetweenChunksReusedData &reused, ChunkComponents<N> *comps_a, ChunkComponents<N> *comps_b, bool vertical
        )
        {
            if (!comps_a && !comps_b)
                return; // Both chunks are null.

            int dir_in_a = int(vertical);
            int dir_in_b = dir_in_a + 2;

            // Here the first index is the component index. The second index is arbirary, it's just a list of components.
            std::vector<std::vector<ComponentIndex>> *neighbors_a = nullptr;
            std::vector<std::vector<ComponentIndex>> *neighbors_b = nullptr;

            if (comps_a)
            {
                neighbors_a = &comps_a->neighbor_components[dir_in_a];
                neighbors_a->clear();
                neighbors_a->resize(comps_a->components.size());
            }

            if (comps_b)
            {
                neighbors_b = &comps_b->neighbor_components[dir_in_b];
                neighbors_b->clear();
                neighbors_b->resize(comps_b->components.size());
            }

            if (!comps_a || !comps_b)
                return; // At least one of the two chunks is null.

            for (int i = 0; i < N; i++)
            {
                BorderEdgeInfo &edge_a = comps_a->border_edge_info[std::to_underlying(MakeBorderEdgeIndex(dir_in_a, i))];
                BorderEdgeInfo &edge_b = comps_b->border_edge_info[std::to_underlying(MakeBorderEdgeIndex(dir_in_b, i))];
                if (bool(edge_a.conn_mask & edge_b.conn_mask))
                {
                    assert(edge_a.component_index != ComponentIndex::invalid);
                    assert(edge_b.component_index != ComponentIndex::invalid);

                    if (reused.visited_pairs.insert({edge_a.component_index, edge_b.component_index}).second)
                    {
                        (*neighbors_a)[std::to_underlying(edge_a.component_index)].push_back(edge_b.component_index);
                        (*neighbors_b)[std::to_underlying(edge_b.component_index)].push_back(edge_a.component_index);
                    }
                }
            }

            reused.visited_pairs.clear();
        }

        // A single chunk as a grid of `CellType` objects of size N*N.
        template <int N, typename CellType>
        class Chunk
        {
          public:
            using ComponentsType = ChunkComponents<N>;

            // The first index is Y, the second is X.
            using UnderlyingArray = std::array<std::array<CellType, N>, N>;
            UnderlyingArray cells;

            static constexpr vec2<CoordInsideChunk> size = vec2<CoordInsideChunk>(N);

            // Access a cell.
            [[nodiscard]] auto at(this auto &&self, vec2<CoordInsideChunk> pos) -> Meta::copy_cvref_qualifiers<decltype(self), CellType>
            {
                ASSERT(pos(all) >= CoordInsideChunk(0) && pos(all) < CoordInsideChunk(N), "Cell position in a chunk is out of bounds.");
                return static_cast<Meta::copy_cvref_qualifiers<decltype(self), CellType>>(self.cells[pos.y][pos.x]);
            }

            struct ComputeConnectedComponentsReusedData
            {
                std::array<std::array<bool, N>, N> visited{};

                std::array<vec2<CoordInsideChunk>, N * N> queue{};
                std::size_t queue_pos = 0;
            };

            // Moves a single connectivity component from the other chunk into this one.
            // The component itself remains the source vector (`other_comps.components`) to not mess up the indices,
            //   and must be erased from there later manually.
            // You can erase using `SwapWithLastAndRemoveComponent(index, true)`. Or manually, but then you must update `border_edge_info` mapping.
            void MoveComponentFrom(ComponentIndex index, ComponentsType &self_comps, Chunk &other_chunk, ComponentsType &other_comps)
            {
                ComponentEntry &other_comp = other_comps.components[std::to_underlying(index)];
                ComponentIndex new_comp_index = ComponentIndex(self_comps.components.size());
                ComponentEntry &new_comp = self_comps.components.emplace_back(std::move(other_comp));
                other_comp = {};

                // Update border edge info.
                for (const ComponentEdgeInfo &edge : new_comp.border_edges)
                {
                    BorderEdgeInfo &self_edge = self_comps.border_edge_info[std::to_underlying(edge.edge_index)];
                    BorderEdgeInfo &other_edge = other_comps.border_edge_info[std::to_underlying(edge.edge_index)];
                    self_edge = std::move(other_edge);
                    self_edge.component_index = new_comp_index; // Override the component index in the copied edge!
                    other_edge = {};
                }

                // Move the cells.
                for (auto pos : new_comp.component.GetTiles())
                {
                    CellType &other_cell = other_chunk.at(pos);
                    CellType &self_cell = at(pos);
                    self_cell = std::move(other_cell);
                    other_cell = {};
                }
            }

            // Compute the individual connectivity components within a chunk.
            void ComputeConnectedComponents(
                // Just the memory reused between calls for performance. Allocate it once (probably on the heap) and reuse between calls.
                ComputeConnectedComponentsReusedData &reused,
                // Either a `ComponentsType` or `Component`.
                // The latter will contain less information, and is a bit cheaper to compute. Use the latter when this is a standalone chunk,
                //   not a part of a grid.
                // The latter can hold at most one component at a time, so you must move them elsewhere in `component_done()` (see below).
                // This is automatically zeroed when the function starts.
                Meta::same_as_any_of<ComponentsType, Component> auto &out,
                // `() -> void`. This is called after finishing writing each component to `out`.
                // If `out` is a `Component` you must move it elsewhere in this callback, else it will get overwritten.
                // If `out` is a `ComponentsType`, you don't have to do anything, as it can store multiple components.
                //   But you CAN `.SwapWithLastAndRemoveComponent()` the newly added component, e.g. if you just separated it to a new entity.
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

                // Reset the target, whatever it is.
                out = {};

                for (const vec2<CoordInsideChunk> starting_pos : vector_range(vec2<CoordInsideChunk>(N)))
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
                    Component &out_comp = [&]() -> auto &
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
                    std::vector<ComponentEdgeInfo> *out_border_edges = nullptr;
                    if constexpr (output_full_info)
                        out_border_edges = &out.components.back().border_edges;

                    reused.queue_pos = 0;
                    reused.queue[reused.queue_pos++] = starting_pos;
                    reused.visited[starting_pos.y][starting_pos.x] = true;

                    do
                    {
                        const vec2<CoordInsideChunk> pos = reused.queue[--reused.queue_pos];
                        out_comp.AddTile(pos);

                        for (int i = 0; i < 4; i++)
                        {
                            // Whether it's the chunk border in this direction.
                            bool is_chunk_edge =
                                i == 0 ? pos.x == N - 1 :
                                i == 1 ? pos.y == N - 1 :
                                i == 2 ? pos.x == 0 :
                                         pos.y == 0;

                            const typename SystemTraits::TileEdgeConnectivity conn_mask = tile_connectivity(at(pos), std::as_const(i));

                            // Dump information about a chunk edge, if this is one.
                            if constexpr (output_full_info)
                            {
                                if (is_chunk_edge)
                                {
                                    const auto border_edge_index = MakeBorderEdgeIndex(i, pos[i % 2 == 0]);

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
                                vec2<CoordInsideChunk> next_pos = pos + vec2<CoordInsideChunk>::dir4(i);

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

        // Splits a chunk grid into connected components.
        class ChunkGridSplitter
        {
            // Describes a component across the whole chunk grud.
            enum class GlobalComponentIndex : std::uint32_t {invalid = std::uint32_t(-1)};

            // A node in the min-heap, describing a component of a chunk to visit.
            struct NodeToVisit
            {
                // Chunk coordinates, and the component index inside of that chunk.
                ComponentCoords coords;

                // Index of the global component in `components`.
                GlobalComponentIndex global_component = GlobalComponentIndex::invalid;
            };

            // A min-heap of the nodes we need to visit.
            std::vector<NodeToVisit> nodes_to_visit_heap;

            // All known nodes, including all nodes from `nodes_to_visit_heap`.
            phmap::flat_hash_map<ComponentCoords, GlobalComponentIndex> known_nodes;

            // Describes a single connectivity component across chunks.
            struct ComponentInfo
            {
                // When a component has been merged into another one, this will be unequal to its index. But some fields still remain valid.
                // Visit recursively until you find an index pointing to itself, and fix all the indices along the way.
                // https://en.wikipedia.org/wiki/Disjoint-set_data_structure
                mutable GlobalComponentIndex canonical_component_index = GlobalComponentIndex::invalid;

                // The original chunk this component started from.
                vec2<WholeChunkCoord> origin_chunk_coord;

                // --- Everything below is merged and zeroed when this component is merged from.

                // How many more nodes in this component we need to visit.
                // When this component is merged from, this becomes zero.
                std::size_t num_unvisited_nodes = 0;

                // The bounds of all chunks contained in this component.
                rect2<WholeChunkCoord> chunk_coord_bounds;

                // The individual components (per-chunk ones) making up this whole component.
                std::vector<ComponentCoords> contents;
            };

            // All known components across chunks.
            std::vector<ComponentInfo> components;

            // We remove merged components from this set.
            SparseSet<std::underlying_type_t<GlobalComponentIndex>> components_set;

            // A list of (merged) components we emitted.
            std::vector<GlobalComponentIndex> emitted_components;

            // This lazily adjusts `canonical_component_index` to make future searches faster.
            [[nodiscard]] GlobalComponentIndex FindMergedComponent(GlobalComponentIndex i) const
            {
                // Repeatedly descend into `canonical_component_index`, and adjust it along the way to shorten the path.
                // We don't shorten all the way, just one step at a time, to avoid doing two passes or potentially stack-overflowing recursion.

                const ComponentInfo *info = &components[std::to_underlying(i)];
                while (i != info->canonical_component_index)
                {
                    const ComponentInfo *next_info = &components[std::to_underlying(info->canonical_component_index)];
                    info->canonical_component_index = next_info->canonical_component_index;
                    i = info->canonical_component_index;
                    info = next_info;
                }

                return i;
            }

          public:
            ChunkGridSplitter() {}

            // Call the functions below in order.

            // Use this to reset the object before reusing it for different tasks.
            // Reusing is recommended to avoid reallocating the memory.
            void Reset()
            {
                nodes_to_visit_heap.clear();
                known_nodes.clear();
                components.clear();
                components_set.EraseAllElements();
                emitted_components.clear();
            }

            // You can optionally call this to reserve memory.
            // The number of components is usually less (or equal) to the number of nodes.
            // `num_components` is how many times you plan to call `AddInitialComponent()`. `num_nodes` can be e.g. the same, or 2x, or 4x (anything works).
            void Reserve(std::size_t num_components, std::size_t num_nodes)
            {
                nodes_to_visit_heap.reserve(num_nodes);
                known_nodes.reserve(num_nodes);
                components.reserve(num_components);
                components_set.Reserve(num_components);
                emitted_components.reserve(num_components); // This could be less, but it doesn't hurt.
            }

            // A default argument for `AddInitialComponent()`, see below.
            static constexpr int default_per_component_capacity = 8;

            // Adds a component that needs to be updated. Don't add the same component multiple times.
            // `per_component_capacity` is how many individual components you expect to merge into the final component. Any small value should work.
            // Call this at the beginning before running `Step()`.
            void AddInitialComponent(vec2<WholeChunkCoord> chunk_coord, ComponentIndex in_chunk_component, std::size_t per_component_capacity = default_per_component_capacity)
            {
                GlobalComponentIndex this_comp_index = GlobalComponentIndex(components.size());

                ComponentInfo &comp_info = components.emplace_back();
                comp_info.canonical_component_index = this_comp_index;
                comp_info.origin_chunk_coord = chunk_coord;
                comp_info.num_unvisited_nodes = 1;
                comp_info.chunk_coord_bounds = chunk_coord.tiny_rect();
                comp_info.contents.reserve(per_component_capacity);
                comp_info.contents.push_back({.chunk_coord = chunk_coord, .in_chunk_component = in_chunk_component});

                NodeToVisit &node = nodes_to_visit_heap.emplace_back();
                node.coords.chunk_coord = chunk_coord;
                node.coords.in_chunk_component = in_chunk_component;
                node.global_component = this_comp_index;

                known_nodes.try_emplace(node.coords, this_comp_index);

                if (components_set.RemainingCapacity() == 0)
                    components_set.Reserve(std::max((unsigned int)1, components_set.Capacity() * 2));
                components_set.Insert(std::to_underlying(this_comp_index));
            }

            // Iterates over all coordinates that are yet to be visited.
            // This is especially useful right after finishing `AddInitialComponent()` calls to iterate over the initial components.
            // `func` is `(vec2<WholeChunkCoord> chunk_coord, ComponentIndex in_chunk_component) -> bool`. If it returns true, the loop stops and the whole function returns true.
            bool ForEachCoordToVisit(auto &&func) const
            {
                for (const NodeToVisit &node : nodes_to_visit_heap)
                {
                    if (func(node.coords.chunk_coord, node.coords.in_chunk_component))
                        return true;
                }
                return false;
            }

            // Call this in a loop until it returns true.
            // `get_chunk` is `(vec2<WholeChunkCoord> chunk_coord) -> const ChunkComponents<N> &`.
            // It should never be called with invalid chunk coords, and can crash in that case.
            [[nodiscard]] bool Step(auto &&get_chunk)
            {
                if (components_set.ElemCount() <= 1)
                    return true; // Only one component remaining. Zero components shouldn't normally happen, unless you didn't call `AddInitialComponent()` at all.

                if (nodes_to_visit_heap.empty())
                    return true; // No more nodes. This shouldn't normally happen, since we should run out of components (except the last one) first.

                // The comparator for the heap. Sort by (square) distance to the original chunk coordinates.
                auto heap_comparator = [&](const NodeToVisit &a, const NodeToVisit &b) -> bool
                {
                    // We intentionally don't use `FindMergedComponent()`, we want the distance to the original chunk.
                    auto orig_chunk_a = components[std::to_underlying(a.global_component)].origin_chunk_coord;
                    auto orig_chunk_b = components[std::to_underlying(b.global_component)].origin_chunk_coord;

                    // Compare distnaces to the original chunk.
                    auto dist_a = (a.coords.chunk_coord - orig_chunk_a).len_sq();
                    auto dist_b = (b.coords.chunk_coord - orig_chunk_b).len_sq();
                    if (dist_a != dist_b)
                        return dist_a > dist_b;

                    // Tie-break by the number of connections (smaller is better).
                    // This is especially important when processing the entire grid when it's initially loaded, since in there
                    // all distances are likely to be 0.
                    std::size_t con_a = get_chunk(a.coords.chunk_coord).GetNumConnections(a.coords.in_chunk_component);
                    std::size_t con_b = get_chunk(b.coords.chunk_coord).GetNumConnections(b.coords.in_chunk_component);

                    return con_a > con_b;
                };

                const NodeToVisit this_node = nodes_to_visit_heap.front();
                std::pop_heap(nodes_to_visit_heap.begin(), nodes_to_visit_heap.end(), heap_comparator);
                nodes_to_visit_heap.pop_back();

                const GlobalComponentIndex merged_component_index = FindMergedComponent(this_node.global_component);
                ComponentInfo &merged_component_info = components[std::to_underlying(merged_component_index)];

                const auto &chunk_comps = get_chunk(this_node.coords.chunk_coord);

                // For all neighbors...
                for (int i = 0; i < 4; i++)
                {
                    const vec2<WholeChunkCoord> other_chunk_coord = this_node.coords.chunk_coord + vec2<WholeChunkCoord>::dir4(i);
                    for (ComponentIndex other_comp_index : chunk_comps.neighbor_components[i][std::to_underlying(this_node.coords.in_chunk_component)])
                    {
                        const ComponentCoords other_coords{.chunk_coord = other_chunk_coord, .in_chunk_component = other_comp_index};

                        // Here we propagate the non-merged component index, because it affects which chunk we compute the distance to.
                        auto [iter, is_new] = known_nodes.try_emplace(other_coords, this_node.global_component);
                        if (is_new)
                        {
                            nodes_to_visit_heap.push_back({.coords = other_coords, .global_component = this_node.global_component});
                            std::push_heap(nodes_to_visit_heap.begin(), nodes_to_visit_heap.end(), heap_comparator);

                            merged_component_info.num_unvisited_nodes++;
                        }
                        else
                        {
                            const GlobalComponentIndex other_merged_component_index = FindMergedComponent(iter->second);

                            // Merge the components.
                            if (merged_component_index != other_merged_component_index)
                            {
                                ComponentInfo &other_merged_component_info = components[std::to_underlying(other_merged_component_index)];

                                other_merged_component_info.canonical_component_index = merged_component_index;

                                merged_component_info.num_unvisited_nodes += other_merged_component_info.num_unvisited_nodes;
                                other_merged_component_info.num_unvisited_nodes = 0;

                                merged_component_info.chunk_coord_bounds = merged_component_info.chunk_coord_bounds.combine(other_merged_component_info.chunk_coord_bounds);
                                other_merged_component_info.chunk_coord_bounds = {};

                                merged_component_info.contents.insert(merged_component_info.contents.end(), std::make_move_iterator(other_merged_component_info.contents.begin()), std::make_move_iterator(other_merged_component_info.contents.end()));

                                // Erase from the set.
                                components_set.EraseUnordered(std::to_underlying(other_merged_component_index));
                            }
                        }
                    }
                }

                merged_component_info.num_unvisited_nodes--;

                // If the component is complete, mark it.
                if (merged_component_info.num_unvisited_nodes == 0)
                {
                    // Erase from the set.
                    components_set.EraseUnordered(std::to_underlying(merged_component_index));

                    // Add to the list.
                    emitted_components.push_back(merged_component_index);
                }

                return false;
            }

            // How many additional components were separated from the main grid.
            [[nodiscard]] std::size_t NumComponentsToEmit() const
            {
                return emitted_components.size();
            }

            struct ComponentToEmit
            {
                // The coordinate bounds for `coords`.
                rect2<WholeChunkCoord> bounds;

                // The list of individual components.
                std::span<const ComponentCoords> contents;
            };

            // Returns a single component that you should separate from this grid, up to `NumComponentsToEmit()`.
            [[nodiscard]] ComponentToEmit GetComponentToEmit(std::size_t i) const
            {
                const ComponentInfo &info = components[std::to_underlying(emitted_components[i])];
                return {
                    .bounds = info.chunk_coord_bounds,
                    .contents = info.contents,
                };
            }
        };
    };
}

#include "tiles_to_edges.h"

#include <parallel_hashmap/phmap.h>

namespace Geom::TilesToEdges
{
    BakedTileset::BakedTileset(Tileset &&input)
        : tile_size(input.tile_size), vertices(std::move(input.vertices))
    {
        // Reverse map vertex positions to IDs.
        phmap::flat_hash_map<ivec2, VertexId> pos_to_vert_id;
        for (std::size_t i = 0; i < vertices.size(); i++)
            pos_to_vert_id.try_emplace(vertices[i], VertexId(i));


        // Assign edge IDs:

        // Maps two vertex IDs to an edge ID.
        phmap::flat_hash_map<std::pair<VertexId, VertexId>, EdgeId> vert_ids_to_edge_id;

        for (const auto &loops : input.tiles)
        for (const auto &loop : loops)
        {
            for (std::size_t i = 0; i < loop.size(); i++)
            {
                std::pair vert_ids{VertexId(loop[i]), VertexId(loop[(i + 1) % loop.size()])};

                auto [iter, ok] = vert_ids_to_edge_id.try_emplace(vert_ids);
                if (ok)
                    iter->second = EdgeId(vert_ids_to_edge_id.size() - 1);
            }
        }

        // Compute per-tile information:

        // Starting edges for each loop within a tile.
        tile_starting_edges.reserve(input.tiles.size());
        for (const auto &loops : input.tiles)
        {
            std::vector<EdgeId> edges;
            edges.reserve(loops.size());
            for (const auto &loop : loops)
                edges.push_back(vert_ids_to_edge_id.at({VertexId(loop.at(0)), VertexId(loop.at(1))}));

            tile_starting_edges.push_back(std::move(edges));
        }

        // Compute per-edge information:

        edge_types.resize(vert_ids_to_edge_id.size());
        for (const auto &[vert_ids, edge_id] : vert_ids_to_edge_id)
        {
            EdgeType &edge_type = edge_types[std::to_underlying(edge_id)];
            edge_type.vert_a = vert_ids.first;
            edge_type.vert_b = vert_ids.second;

            // Try finding the opposite edge (if not already set, that happens when that edge is processed first).
            if (edge_type.opposite_edge == EdgeId::invalid)
            {
                ivec2 vert_a = vertices[std::to_underlying(edge_type.vert_a)];
                ivec2 vert_b = vertices[std::to_underlying(edge_type.vert_b)];

                // Only try half of the direction (up and left). We don't need to care about the other two, because the opposite edge will find them.
                ivec2 opposite_dir;
                if (vert_a.x == 0 && vert_b.x == 0)
                    opposite_dir = ivec2(-1, 0);
                else if (vert_a.y == 0 && vert_b.y == 0)
                    opposite_dir = ivec2(0, -1);

                if (opposite_dir)
                {
                    ivec2 other_vert_a = vert_b - opposite_dir * tile_size; // Note the order reversal.
                    ivec2 other_vert_b = vert_a - opposite_dir * tile_size;

                    if (auto vert_iter_a = pos_to_vert_id.find(other_vert_a); vert_iter_a != pos_to_vert_id.end())
                    if (auto vert_iter_b = pos_to_vert_id.find(other_vert_b); vert_iter_b != pos_to_vert_id.end())
                    if (auto edge_iter = vert_ids_to_edge_id.find({vert_iter_a->second, vert_iter_b->second}); edge_iter != vert_ids_to_edge_id.end())
                    {
                        edge_type.opposite_edge_dir = opposite_dir;
                        edge_type.opposite_edge = edge_iter->second;

                        // And the opposite edge:
                        EdgeType &opposite_edge_type = edge_types[std::to_underlying(edge_iter->second)];
                        opposite_edge_type.opposite_edge_dir = -opposite_dir;
                        opposite_edge_type.opposite_edge = edge_id;
                    }
                }
                else
                {
                    // Find an edge in the SAME tile that cancels out this one. This usually doesn't matter, but why not.
                    if (auto it = vert_ids_to_edge_id.find({edge_type.vert_b, edge_type.vert_a}); it != vert_ids_to_edge_id.end())
                    {
                        edge_type.opposite_edge = it->second;
                        edge_types[std::to_underlying(it->second)].opposite_edge = edge_id;
                    }
                }
            }
        }

        // Compute edge connectivity for every tile type:

        edge_connectivity.resize(xvec2(vert_ids_to_edge_id.size(), input.tiles.size()));
        for (std::size_t i = 0; i < input.tiles.size(); i++)
        {
            for (const auto &loop : input.tiles[i])
            {
                for (std::size_t j = 0; j < loop.size(); j++)
                {
                    VertexId v1 = VertexId(loop[j]);
                    VertexId v2 = VertexId(loop[(j + 1) % loop.size()]);
                    VertexId v3 = VertexId(loop[(j + 2) % loop.size()]);

                    EdgeId e1 = vert_ids_to_edge_id.at({v1, v2});
                    EdgeId e2 = vert_ids_to_edge_id.at({v2, v3});

                    edge_connectivity.at(xvec2(std::to_underlying(e1), i)).next = e2;
                    edge_connectivity.at(xvec2(std::to_underlying(e2), i)).prev = e1;
                }
            }
        }
    }
}

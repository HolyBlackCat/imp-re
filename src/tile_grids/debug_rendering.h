#pragma once

#include "macros/enum_flag_operators.h"
#include "tile_grids/high_level.h"

#include <imgui.h>

namespace TileGrids
{
    enum class DebugDrawFlags
    {
        // Bounds of the entire chunk grid.
        chunk_grid_bounds = 1 << 0,
        // Bounds of individual chunks.
        chunk_bounds = 1 << 1,
        // Connectivity components inside chunks (an index over each tile).
        chunk_components = 1 << 2,
        // Which connectivity component each edge belongs to.
        chunk_border_edges = 1 << 3,

        all = chunk_grid_bounds | chunk_bounds | chunk_components | chunk_border_edges,
    };
    IMP_ENUM_FLAG_OPERATORS(DebugDrawFlags)

    // Draws debug information for the `grid`, using `list` draw list.
    // `local_to_screen_coords` maps `fvec2` from grid to screen coords. In grid coords the size of a tile is always considered to be 1.
    template <typename System, int N, typename CellType>
    void ImguiDebugDraw(const ChunkGrid<System, N, CellType> &grid, auto local_to_screen_coords, ImDrawList &list, DebugDrawFlags flags)
    {
        // Should we instead return `ImVec2`?
        static_assert(std::is_same_v<decltype(local_to_screen_coords(fvec2{})), fvec2>, "`local_to_screen_coords` returns the wrong type.");

        auto bounds = grid.ChunkGridBounds();

        if (bool(flags & DebugDrawFlags::chunk_grid_bounds))
        {
            for (auto pos : (bounds * N).to_contour())
                list.PathLineTo(ImVec2(local_to_screen_coords(fvec2(pos))));
            list.PathStroke(ImColor(1.f,0.f,0.f,0.5f), ImDrawFlags_Closed, 8);
        }

        if (bool(flags & DebugDrawFlags::chunk_bounds))
        {
            for (auto pos : vector_range(bounds))
            {
                if (!bool(grid.GetChunk(pos)))
                    continue; // No chunk here.

                for (auto pos : (pos * N).rect_size(N).template to<float>().shrink(0.05f).to_contour())
                    list.PathLineTo(ImVec2(local_to_screen_coords(fvec2(pos))));
                list.PathStroke(ImColor(1.f,1.f,0.f,1.f), ImDrawFlags_Closed, 1);
            }
        }

        if (bool(flags & DebugDrawFlags::chunk_components))
        {
            for (auto chunk_pos : vector_range(bounds))
            {
                const auto base_pos = chunk_pos * N;

                int comp_index = 0;
                if (auto comps = grid.GetChunkComponents(chunk_pos))
                {
                    for (const auto &comp : comps->components)
                    {
                        for (const auto &coord : comp.component.GetTiles())
                        {
                            fvec2 center = base_pos + coord + 0.5f;
                            std::string text = FMT("{}", comp_index);
                            list.AddText(ImVec2(local_to_screen_coords(center) - fvec2(ImGui::CalcTextSize(text.c_str())) / 2), ImColor(1.f,0.f,0.f,1.f), text.c_str());
                        }
                        comp_index++;
                    }
                }
            }
        }

        if (bool(flags & DebugDrawFlags::chunk_border_edges))
        {
            if (bool(flags & DebugDrawFlags::chunk_components))
            {
                for (auto chunk_pos : vector_range(bounds))
                {
                    if (auto comps = grid.GetChunkComponents(chunk_pos))
                    {
                        const auto base_pos = fvec2(chunk_pos * N);

                        for (int i = 0; i < N * 4; i++)
                        {
                            auto index = comps->border_edge_info[i].component_index;
                            if (index == System::ComponentIndex::invalid)
                                continue;

                            int dir = System::GetDirFromBorderEdgeIndex(typename System::BorderEdgeIndex(i));
                            auto offset = System::GetCoordFromBorderEdgeIndex(typename System::BorderEdgeIndex(i));

                            fvec2 pos(base_pos);
                            if (dir == 0)
                                pos.x += N;
                            else if (dir == 1)
                                pos.y += N;

                            pos[dir % 2 == 0] += offset + 0.5f;
                            pos -= fvec2::dir4(dir, 0.3f);

                            std::string text = fmt::format("{}", std::to_underlying(index));

                            list.AddText(local_to_screen_coords(pos) - fvec2(ImGui::CalcTextSize(text.c_str())) / 2, ImColor(0.f,1.f,1.f,1.f), text.c_str());
                        }
                    }
                }
            }
        }
    }
}

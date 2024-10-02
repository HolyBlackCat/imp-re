#pragma once

#include "utils/mat.h" // Not needed for the base algorithm, only for the variants with predefined heuristics.

#include <parallel_hashmap/phmap.h>

#include <algorithm>
#include <cassert>
#include <vector>
#include <utility>

// This header implements A* pathfinding (which, with the right choice of heuristic, can also degenerate into Dijkstra's search and some greedy algorithms).

namespace Graph::Pathfinding
{
    // `CoordType` is normally the coordinate type, such as `ivec2`. But it can be anything that represents a position in your graph.
    // `CostType` is the true cost type, it's normally `int` or `float`, but can also be anything.
    // `EstimatedCostType` is the true plus estimated cost type. It's usually `std::pair<int, int>` (with the second value used as a tiebreaker, see more below).
    // `CostType` must overload `+` and be default-constructible (probably to a zero value, but it's not really necessary).
    // `EstimatedCostType` must overload `<` (unless you customize `on_revisit`).
    //
    // How to use:
    // * Set the starting point, either using the constructor or `SetNewTask()`.
    // * Run the following loop:
    //       while (p.HasUnvisitedNodes()) // If this condition fails, there's no path.
    //       {
    //           if (p.CurrentNode() == goal)
    //               break; // Found path.
    //           p.Step(...);
    //       }
    // * On success, dump the path using `p.DumpPathBackwards()`.
    // * You can limit the number of loop iterations.
    template <typename CoordType, typename CostType, typename EstimatedCostType = CostType>
    class Pathfinder
    {
      public:
        using coord_t = CoordType;
        using cost_t = CostType;
        using estimated_cost_t = EstimatedCostType;

        struct Node
        {
            CoordType coord{};

            // Exact cost until this point plus an estimated cost to the goal.
            EstimatedCostType estimated_total_cost{};
        };
        using RemainingNodesHeap = std::vector<Node>;

        struct NodeInfo
        {
            // Exact cost from start to this node.
            CostType cost{};
            // The next node towards the starting point. In the starting point, it points at itself.
            CoordType prev_node{};

            // We're using this to avoid processing the same node more than once.
            // This improves performance, but my current understanding that it can result in
            //   suboptimal final path for non-"consistent" heuristics (see definitions below)
            //   (without this admissable but non-consistent heuristics can get optimal paths,
            //   but non-admissable heuristics produce more or less suboptimal paths either way);
            //   avoiding that requires a honest deduplication of items in `remaining_nodes_heap` (wikipedia suggests
            //   using a hashmap to know the position of each node in the queue, and then there's an algorithm to increase/decrease
            //   the priority of an element in the heap (not in `std::` though?); alternatively they suggest fibonacci heaps,
            //   or I assume you could use a binary search tree instead of a heap, such as phmap's btree).
            bool finished = false;
        };
        using NodeInfoMap = phmap::flat_hash_map<CoordType, NodeInfo>;

      private:
        // It seems this can contain duplicate nodes, even with good heuristics.
        // I'm not sure if pruning them can result in suboptimal path, so let's keep them.
        RemainingNodesHeap remaining_nodes_heap;

        NodeInfoMap node_info;

      public:
        // Initializes with capacity 0 (this only affects performance).
        // When using this constructor, must call `SetNewTask()` before using the object.
        Pathfinder() {}

        // Initializes with custom capacity (which only affects performance).
        // When using this constructor, must call `SetNewTask()` before using the object.
        explicit Pathfinder(std::size_t starting_capacity)
        {
            remaining_nodes_heap.reserve(starting_capacity);
            node_info.reserve(starting_capacity);
        }

        // Set the starting point and optionally capacity (which only affects performance).
        // The tree spreads out from `start`, but the resulting path goes from the goal towards `start`.
        // So you might want to swap the two if you need the path to start from the `start`.
        explicit Pathfinder(CoordType start, std::size_t starting_capacity = 16)
            : Pathfinder(starting_capacity)
        {
            SetNewTask(start);
        }

        // Resets the object, preparing for a new pathfinding task. But preserves the capacity.
        // The tree spreads out from `start`, but the resulting path goes from the goal towards `start`.
        // So you might want to swap the two if you need the path to start from the `start`.
        void SetNewTask(CoordType new_start)
        {
            remaining_nodes_heap.clear();
            node_info.clear();

            remaining_nodes_heap.emplace_back().coord = new_start;
            node_info.try_emplace(new_start).first->second.prev_node = new_start;
        }

        // Whether we have more nodes to visit.
        // If this becomes false before reaching the goal, there's no path.
        [[nodiscard]] bool HasUnvisitedNodes() const
        {
            return !remaining_nodes_heap.empty();
        }

        // The next node to visit. Initially the starting point.
        // If `HasUnvisitedNodes() == false`, throws.
        [[nodiscard]] CoordType CurrentNode() const
        {
            return remaining_nodes_heap.at(0).coord;
        }

        struct DefaultStepSettings
        {
            // Lets you choose what happens when a node is revisited.
            // We're revisiting `neighbor`, this time from `self`, and the new cost to it is `neighbor_cost`.
            // Returning true will replace the old path with the new path.
            // `neighbor.second.cost` is not updated yet when this is called, but if you return true, we assign `neighbor_cost` to it.
            // Always returning false here might lead to suboptimal paths (see the discussion of different heuristic types in the comments above `Step()`).
            bool ShouldUseNewPath(const NodeInfoMap::value_type &self, const NodeInfoMap::value_type &neighbor, const CostType &new_neighbor_cost)
            {
                (void)self;
                return new_neighbor_cost < neighbor.second.cost;
            }
        };

        // Runs a single pathfinding step.
        // `neighbors` iterates over each viable neighbor of a point, it's `(CoordType pos, auto func) -> void`,
        //   where `func` must be called for every neighbor, it's `(CoordType neighbor_coord, CostType step_cost) -> void`.
        // `heuristic` is `(CostType cost, CoordType pos) -> EstimatedCostType`.
        //   It's given the true cost from the start point to `pos`, and must return the total estimated cost to the goal.
        // Usually, when `EstimatedCostType` is a pair, you would return `{cost + heuristic, tiebreaker}`,
        //   e.g. `{cost + (end - pos).abs().sum(), (end - pos).len_sq()}` for 4-way movement.
        // NOTE: Using eucledian distance as the primary heuristic when your movement is 4-way grid-based
        //   will cause the result to SUCK (visits too many unnecessary nodes).
        // This doesn't happen for 8-way grid movement, but it's probably better to just avoid it altogether and use a honest heuristic instead.
        // You can use eucledian distance as a tiebreaker though (and then you don't need the exact distance, and can use a squared one).
        //
        // Heuristics can be classified, but it's a bit moot.
        // * A heuristic is "admissable" if it doesn't overestimate the path to the end. (And presumably you don't subtract from the initial `cost`,
        //     unless you scale all of it by a constant factor?)
        //     Admissable heuristics guarantee optimal path at the end (but the number of iterations can vary, of course).
        // * A heuristic is "consistent" (aka monotone) if it's admissable AND it's value can't be reduced by adding an intermediate point.
        //     This doesn't seem to affect much. In theory this guarantees that you don't visit the same node twice (so you could always
        //       return false from `ShouldUseNewPath()`), but to safely do that you also have to prune duplicates from the node queue,
        //       which sounds like too much effort.
        //
        // Some other possible heuristics:
        // 1. Return `cost` as is. Then this becomes the Dijkstra's algorithm, blindly searching in a circle from the starting point.
        // 2. Multiply the existing `cost` by x in range 0..1 before adding the estimated remaining distance to it.
        //      This adjusts the greediness, with 0 = maximum greed. The resulting path is no longer optimal (this is a non-admissible heuristic),
        //      but this can potentially boost performance in some cases.
        // There's no feedback loop in the heuristic, so you can get away with it being jank.
        template <typename Settings = DefaultStepSettings>
        void Step(auto &&neighbors, auto &&heuristic, Settings &&settings = {})
        {
            if (!HasUnvisitedNodes())
                return;

          retry:
            CoordType this_node = remaining_nodes_heap.front().coord;

            std::ranges::pop_heap(remaining_nodes_heap, std::greater{}, &Node::estimated_total_cost);
            remaining_nodes_heap.pop_back();

            auto this_node_info_iter = node_info.find(this_node);
            if (this_node_info_iter == node_info.end())
                throw std::logic_error("Pathfinding internal error: the node should be in the info map, but it's not.");
            NodeInfo &this_node_info = this_node_info_iter->second;

            // Avoid processing the same node twice.
            // This is an optimization, see the comment on `.finished` for details.
            if (this_node_info.finished)
                goto retry;
            this_node_info.finished = true;

            neighbors(std::as_const(this_node), [&](CoordType neighbor_coord, CostType step_cost)
            {
                if (neighbor_coord == this_node_info.prev_node)
                    return; // Refuse to backtrack into the previous node. This is purely an optimization.

                CostType neighbor_cost = this_node_info.cost + step_cost;

                auto [iter, is_new] = node_info.try_emplace(neighbor_coord);

                // Wikipedia says `<` will be always false if the heuristic is "consistent" (see above),
                // but trying to classify heuristics is tricky, and skipping it when the heuristic is not actuall consistent
                // would result in a non-optimal path. So it's easier to just always check.
                if (is_new || settings.ShouldUseNewPath(std::as_const(*this_node_info_iter, std::as_const(*iter), std::as_const(neighbor_cost))))
                {
                    iter->second.cost = std::move(neighbor_cost);
                    iter->second.prev_node = this_node;

                    remaining_nodes_heap.push_back({
                        .coord = neighbor_coord,
                        .estimated_total_cost = heuristic(std::as_const(neighbor_cost), std::as_const(neighbor_coord)),
                    });
                    std::ranges::push_heap(remaining_nodes_heap, std::greater{}, &Node::estimated_total_cost);
                }
            });
        }

        // Dumps the resulting path backwards, from `goal` to the starting point.
        // Passes each coordinate to `func`, which is `(CoordType point) -> void`.
        // If the goal matches the start, just outputs that coordinate once.
        void DumpPathBackwards(CoordType goal, auto &&func) const
        {
            while (true)
            {
                func(std::as_const(goal));

                const NodeInfo &info = node_info.at(goal);
                if (info.prev_node == goal)
                    return; // This is the starting node.
                goal = info.prev_node;
            }
        }

        // Various getters:

        // Returns the nodes that we still need to visit, as a "min heap" (the first node is the next one, while the rest are in a semi-sorted order).
        [[nodiscard]] const RemainingNodesHeap &GetRemainingNodesHeap() const {return remaining_nodes_heap;}
        // Returns the map with some node information.
        // You can use this to see which nodes were visited.
        [[nodiscard]] const NodeInfoMap &GetNodeInfoMap() const {return node_info;}
    };

    // A version of `Pathfinder` with a good predefined heuristic for 4-way grid movement.
    template <typename CoordType = ivec2>
    class Pathfinder_4Way : public Pathfinder<CoordType, typename CoordType::type, std::pair<typename CoordType::type, typename CoordType::type>>
    {
        using CostType = typename CoordType::type;
        using Base = Pathfinder<CoordType, CostType, std::pair<CostType, CostType>>;

      public:
        using Base::Base;

        // Runs a single pathfinding step, by calling into the base class with predefined heuristics.
        // `tile_is_solid` is `(CoordType pos) -> bool` that returns true if the tile is solid and can't be moved through.
        void Step(CoordType goal, auto &&tile_is_solid)
        {
            return Base::Step(
                [&](CoordType pos, auto func)
                {
                    for (int i = 0; i < 4; i++)
                    {
                        CoordType next_pos = pos + CoordType::dir4(i);
                        if (!bool(tile_is_solid(std::as_const(next_pos))))
                            func(next_pos, CostType(1));
                    }
                },
                [&](CostType cost, CoordType pos) -> std::pair<CostType, CostType>
                {
                    CoordType delta = goal - pos;
                    return {cost + delta.abs().sum(), delta.len_sq()};
                }
            );
        }
    };
}

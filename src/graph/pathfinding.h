#pragma once

#include "macros/enum_flag_operators.h"

#include <parallel_hashmap/phmap.h>

#include <algorithm>
#include <cassert>
#include <vector>
#include <utility>

// This header implements A* pathfinding (which, with the right choice of heuristic, can also degenerate into Dijkstra's search and some greedy algorithms).

namespace Graph::Pathfinding
{
    enum class Result
    {
        success, // Found the path.
        fail, // There is no path.
        incomplete, // Need more iterations.
    };

    enum class Flags
    {
        // This lets you perform more iterations after reaching the goal, to explore the near nodes.
        // If this is true, you can run the search after it returns `ok`, which will give you more nodes,
        // until it finally fails after exhausting all nodes.
        can_continue_after_goal = 1 << 0,
    };
    IMP_ENUM_FLAG_OPERATORS(Flags)

    // `CoordType` is normally the coordinate type, such as `ivec2`. But it can be anything that represents a position in your graph.
    // `CostType` is the true cost type, it's normally `int` or `float`, but can also be anything.
    // `EstimatedCostType` is the true plus estimated cost type. It's usually `std::pair<int, int>` (with the second value used as a tiebreaker, see more below).
    // `CostType` must overload `+` and be default-constructible (probably to a zero value, but it's not really necessary).
    // `EstimatedCostType` must overload `<`.
    template <typename CoordType, typename CostType, typename EstimatedCostType = CostType>
    class Pathfinder
    {
      public:
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
        };
        using NodeInfoMap = phmap::flat_hash_map<CoordType, NodeInfo>;

      private:
        CoordType goal{};

        // It seems this can contain duplicate nodes, even with good heuristics.
        // I'm not sure if pruning them can result in suboptimal path, so let's keep them.
        RemainingNodesHeap remaining_nodes_heap;

        NodeInfoMap node_info;

      public:
        Pathfinder() {}

        // The tree spreads out from `start`, but the resulting path goes from `goal` towards `start`.
        // So you might want to swap the two if you need the path to start from `goal`.
        Pathfinder(CoordType start, CoordType goal, std::size_t starting_capacity = 16)
            : goal(std::move(goal))
        {
            remaining_nodes_heap.reserve(starting_capacity);
            node_info.reserve(starting_capacity);

            remaining_nodes_heap.emplace_back().coord = start;

            node_info.try_emplace(start).first->second.prev_node = start;
        }

        // Runs a single pathfinding step.
        // Run this in a loop until it returns something other than `incomplete`.
        // `flags` can be just `{}`, or see `enum class Flags` for possible flags.
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
        //     This doesn't seem to affect much. In theory this guarantees that you don't visit the same node twice (so you could remove
        //       the `neighbor_cost < iter->second.cost` check from this function), but to safely remove the check you also need to
        //       prune duplicates from the node queue, which sounds like too much effort.
        //
        // Some other possible heuristics:
        // 1. Return `cost` as is. Then this becomes the Dijkstra's algorithm, blindly searching in a circle from the starting point.
        // 2. Multiply the existing `cost` by x in range 0..1 before adding the estimated remaining distance to it.
        //      This adjusts the greediness, with 0 = maximum greed. The resulting path is no longer optimal (this is a non-admissible heuristic),
        //      but this can potentially boost performance in some cases.
        // There's no feedback loop in the heuristic, so you can get away with it being jank.
        Result Step(Flags flags, auto &&neighbors, auto &&heuristic)
        {
            if (remaining_nodes_heap.empty())
                return Result::fail;

            CoordType this_node = remaining_nodes_heap.front().coord;

            if (!bool(flags & Flags::can_continue_after_goal) && this_node == goal)
                return Result::success;

            std::ranges::pop_heap(remaining_nodes_heap, std::greater{}, &Node::estimated_total_cost);
            remaining_nodes_heap.pop_back();

            if (bool(flags & Flags::can_continue_after_goal) && this_node == goal)
                return Result::success;

            const NodeInfo &this_node_info = node_info.at(this_node);

            neighbors(std::as_const(this_node), [&](CoordType neighbor_coord, CostType step_cost)
            {
                if (neighbor_coord == this_node_info.prev_node)
                    return; // Refuse to backtrack into the previous node. This is purely an optimization.

                CostType neighbor_cost = this_node_info.cost + step_cost;

                auto [iter, is_new] = node_info.try_emplace(neighbor_coord);

                // Wikipedia says `<` will be always false if the heuristic is "consistent" (see above),
                // but trying to classify heuristics is tricky, and skipping it when the heuristic is not actuall consistent
                // would result in a non-optimal path. So it's easier to just always check.
                if (is_new || neighbor_cost < iter->second.cost)
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

            return Result::incomplete;
        }

        // Dumps the resulting path backwards to `func`, which is `(CoordType point) -> void`.
        // Because it's returned backwards, you can swap the start and the goal (in the constructor) to receive the path in the right direction here.
        // If the goal matches the start, just outputs that coordinate once.
        void DumpPathBackwards(auto &&func) const
        {
            DumpPathBackwards(goal, func);
        }
        // This overload starts from an arbitrary node rather than the goal.
        // So you can use this one even before computing the full path.
        void DumpPathBackwards(CoordType starting_node, auto &&func) const
        {
            while (true)
            {
                func(std::as_const(starting_node));

                const NodeInfo &info = node_info.at(starting_node);
                if (info.prev_node == starting_node)
                    return; // This is the starting node.
                starting_node = info.prev_node;
            }
        }

        // Various getters:

        // Returns the goal position, as passed to the constructor.
        [[nodiscard]] const CoordType &GetGoal() const {return goal;}
        // Returns the nodes that we still need to visit, as a "min heap" (the first node is the next one, while the rest are in a semi-sorted order).
        [[nodiscard]] const RemainingNodesHeap &GetRemainingNodesHeap() const {return remaining_nodes_heap;}
        // Returns the map with some node information.
        // You can use this to see which nodes were visited.
        [[nodiscard]] const NodeInfoMap &GetNodeInfoMap() const {return node_info;}
    };
}

#pragma once
//----------------------------------------------------------------------------------------------------------------------
#include <optional>
#include <vector>
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2025 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
struct Partition;
struct AssignmentPlannerState;
struct PipelineId;
struct NodeId;
struct Cluster;
struct Task;
class PartitionManager;
class DistributedQueryPlan;
//----------------------------------------------------------------------------------------------------------------------
struct Greedy {
  /// Find nodes that have the partition
  static void findEligibleNodes(const Partition& p, const PartitionManager& state, std::vector<NodeId>& result,
                                const Cluster& cluster);
  /// Find nodes that have some of the input data of a task
  static void findEligibleNodes(const Task& t, const PartitionManager& state, std::vector<NodeId>& result,
                                const Cluster& cluster);
  /// Get one best node according to data affinity
  static std::optional<NodeId> getBestNode(const Task& t, const std::vector<NodeId>& nodes,
                                           const PartitionManager& partitions, const DistributedQueryPlan& plan);
  /// Get the best nodes according to data affinity
  static void getBestNodes(const Task& t, std::vector<NodeId>& nodes, const PartitionManager& partitions,
                           const DistributedQueryPlan& plan);
};
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

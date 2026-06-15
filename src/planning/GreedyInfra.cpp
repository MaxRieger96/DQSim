#include "src/planning/GreedyInfra.hpp"

#include <cassert>
#include <optional>
#include <vector>

#include "src/partitioning/PartitionManager.hpp"
#include "src/planning/AssignmentPlannerState.hpp"
#include "src/tasks/Task.hpp"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2025 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
void Greedy::findEligibleNodes(const Partition& p, const PartitionManager& partitions, std::vector<NodeId>& result,
                               const Cluster& cluster) {
  {
    auto owningNode = partitions.getOwningNode().find(p);
    if (owningNode != partitions.getOwningNode().end() && cluster.isComputeNode(owningNode->second)) {
      result.push_back(owningNode->second);
    }
  }
  {
    auto cachingNodes = partitions.getCachingNodes().find(p);
    if (cachingNodes != partitions.getCachingNodes().end()) {
      for (const auto& n : cachingNodes->second)
        if (cluster.isComputeNode(n)) result.push_back(n);
    }
  }
}
//----------------------------------------------------------------------------------------------------------------------
void Greedy::findEligibleNodes(const Task& t, const PartitionManager& partitions, std::vector<NodeId>& result,
                               const Cluster& cluster) {
  assert(result.empty());
  for (const auto& p : t.getInputPartitions()) {
    findEligibleNodes(p, partitions, result, cluster);
  }
}
//----------------------------------------------------------------------------------------------------------------------
std::optional<NodeId> Greedy::getBestNode(const Task& t, const std::vector<NodeId>& nodes,
                                          const PartitionManager& partitions, const DistributedQueryPlan& plan) {
  double bestFraction = 0;
  std::optional<NodeId> result;
  for (const auto& n : nodes) {
    double currentFraction = AssignmentPlannerState::computePresentDataFraction(n, t, partitions, plan);
    if (currentFraction > bestFraction) {
      result = n;
      bestFraction = currentFraction;
    }
  }
  return result;
}
//----------------------------------------------------------------------------------------------------------------------
void Greedy::getBestNodes(const Task& t, std::vector<NodeId>& nodes, const PartitionManager& partitions,
                          const DistributedQueryPlan& plan) {
  double bestFraction = 0;
  std::vector<NodeId> result;
  for (const auto& n : nodes) {
    double currentFraction = AssignmentPlannerState::computePresentDataFraction(n, t, partitions, plan);
    if (currentFraction > bestFraction) {
      result.clear();
      bestFraction = currentFraction;
    }
    if (currentFraction == bestFraction) {
      result.push_back(n);
    }
  }
  nodes = result;
}
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim

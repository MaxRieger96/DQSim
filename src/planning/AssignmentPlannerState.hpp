#pragma once
//----------------------------------------------------------------------------------------------------------------------
#include <memory>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "src/CustomTypes.hpp"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2024 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
struct AssignmentProblem;
struct TaskAssignment;
class ShuffleStage;
class PartitionManager;
struct Task;
class DistributedQueryPlan;
//----------------------------------------------------------------------------------------------------------------------
/// State of an assignment planner, that changes during computation
struct AssignmentPlannerState {
  /// The problem that is currently solved
  const AssignmentProblem* problem;
  /// The current result
  std::unique_ptr<TaskAssignment> result;
  /// The shuffled data units
  std::unordered_map<DataUnitId, const ShuffleStage*> shuffledDUs;
  /// The partition manager used to maintain partition locations
  std::unique_ptr<PartitionManager> partitionManager;
  /// Tasks flattened
  std::vector<const Task*> flatTasks;
  /// All using nodes for each data unit. Only created by assignBroadcasts()
  std::optional<std::unordered_map<DataUnitId, std::unordered_set<NodeId>>> usingNodesCache;

  /// Load a problem and fill members
  void loadProblem(const AssignmentProblem* problem);
  /// Assign broadcast tasks to all nodes that are still used
  void assignBroadcastTasks(bool storeUsingNodes = false);
  /// Create necessary shuffles and transfers when located tasks are there
  void createShufflesAndTransfers();
  /// Given a flat task assignment compute the complete TaskAssignment and store it in result
  void liftFlatAssignment(std::span<size_t const> flatAssignment);
  /// Check how much of the data a node has
  static double computePresentDataFraction(NodeId node, const Task& task, const PartitionManager& partitions,
                                           const DistributedQueryPlan& plan);
  /// Remove shuffle targets that are never used
  void trimShuffleTargets();

 private:
  /// Find the nodes that use a data unit
  std::unordered_map<DataUnitId, std::unordered_set<NodeId>> getUsingNodes();
};
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

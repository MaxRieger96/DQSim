#include "src/planning/GreedyPlanner.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "src/distributedplan/ProblemWriter.hpp"
#include "src/planning/AssignmentPlannerState.hpp"
#include "src/planning/GreedyInfra.hpp"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2024 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
TaskAssignment GreedyPlanner1::assignTasks(const AssignmentProblem& newProblem) {
  problem = &newProblem;
  AssignmentPlannerState state;
  state.loadProblem(problem);
  size_t nNodes = problem->cluster.getComputeNodes().size();
  NodeId currentNode{0};
  std::vector<NodeId> nodes;
  PartitionManager currentPartitions = *state.partitionManager;
  // std::unordered_set<DataUnitId> shuffledDUs;
  // Here we rely on the fact, that the flat tasks are ordered in dependency order
  for (const auto* t : state.flatTasks) {
    // Make broadcasts available at all nodes
    // for (const auto& i : t->getInputPartitions()) {
    //    if (!shuffledDUs.contains(i.dataUnitId) &&
    //        problem->plan.getDataUnit(i.dataUnitId).partitionLayout->type == PartitionLayoutType::Broadcast &&
    //        !state.partitionManager->partitionStored(i)) {
    //       state.partitionManager->registerPartition(i, NodeId{0});
    //       for (NodeId n{1}; n < problem->cluster.getComputeNodes().size(); ++n) {
    //          state.partitionManager->cachePartition(i, n);
    //       }
    //       shuffledDUs.insert(i.dataUnitId);
    //    }
    // }
    nodes.clear();
    Greedy::findEligibleNodes(*t, *state.partitionManager, nodes, problem->cluster);
    std::optional<NodeId> bestNode = Greedy::getBestNode(*t, nodes, *state.partitionManager, problem->plan);
    if (!bestNode) {
      // Fall back to round-robin when we do not have any information
      bestNode = currentNode;
      currentNode = (currentNode + 1) % nNodes;
    }
    assert(bestNode);
    state.result->locatedTasks.emplace_back(*t, *bestNode);
    // Make result of task available at this node
    state.partitionManager->registerPartition(t->outputPartition, *bestNode);
    // for (const auto& i : t->getInputPartitions()) {
    //    if (!state.partitionManager->partitionStored(i)) {
    //       state.partitionManager->registerPartition(i, *bestNode);
    //    }
    // }
  }
  state.assignBroadcastTasks();
  *state.partitionManager = currentPartitions;
  state.createShufflesAndTransfers();
  return *state.result;
}
//----------------------------------------------------------------------------------------------------------------------
std::string GreedyPlanner1::getName() const { return "Greedy1"; }
//----------------------------------------------------------------------------------------------------------------------
std::string GreedyPlanner2::getName() const { return "Greedy2"; }
//----------------------------------------------------------------------------------------------------------------------
TaskAssignment GreedyPlanner2::assignTasks(const AssignmentProblem& newProblem) {
  problem = &newProblem;
  AssignmentPlannerState state;
  state.loadProblem(problem);
  size_t nNodes = problem->cluster.getComputeNodes().size();
  std::vector<NodeId> nodes;
  PartitionManager currentPartitions = *state.partitionManager;
  for (const auto* t : state.flatTasks) {
    nodes.clear();
    Greedy::findEligibleNodes(*t, *state.partitionManager, nodes, problem->cluster);
    std::optional<NodeId> bestNode = Greedy::getBestNode(*t, nodes, *state.partitionManager, problem->plan);
    if (!bestNode) {
      // Fall back to aligned round-robin when we do not have any information
      bestNode = t->scannedPartition.partitionIndex % nNodes;
    }
    assert(bestNode);
    state.result->locatedTasks.emplace_back(*t, *bestNode);
    // Make result of task available at this node
    state.partitionManager->registerPartition(t->outputPartition, *bestNode);
  }
  state.assignBroadcastTasks();
  *state.partitionManager = currentPartitions;
  state.createShufflesAndTransfers();
  return *state.result;
}
//----------------------------------------------------------------------------------------------------------------------
std::string GreedyPlanner3::getName() const { return "Greedy3"; }
//----------------------------------------------------------------------------------------------------------------------
TaskAssignment GreedyPlanner3::assignTasks(const AssignmentProblem& newProblem) {
  problem = &newProblem;
  AssignmentPlannerState state;
  state.loadProblem(problem);
  std::vector<NodeId> nodes;
  PartitionManager currentPartitions = *state.partitionManager;

  // Keeping track of time spent using computation
  std::vector<double> nodeComputationTimes(problem->cluster.getComputeNodes().size(), 0.0);
  std::unordered_map<uint64_t, NodeId> assignedPartitions;

  for (const auto* t : state.flatTasks) {
    nodes.clear();
    Greedy::findEligibleNodes(*t, *state.partitionManager, nodes, problem->cluster);
    std::optional<NodeId> bestNode = Greedy::getBestNode(*t, nodes, *state.partitionManager, problem->plan);
    if (!bestNode) {
      uint64_t pIndex = t->scannedPartition.partitionIndex;
      if (assignedPartitions.contains(pIndex)) {
        bestNode = assignedPartitions[pIndex];
      } else {
        // select the node with the earliest start time
        assert(bestNode == std::nullopt);
        double bestFinishTime = std::numeric_limits<double>::max();
        for (size_t iNode = 0; iNode < problem->cluster.getComputeNodes().size(); ++iNode) {
          double currentFinishTime = nodeComputationTimes[iNode];
          if (currentFinishTime < bestFinishTime) {
            bestFinishTime = currentFinishTime;
            bestNode = iNode;
          }
        }
      }
    }
    assert(bestNode);
    state.result->locatedTasks.emplace_back(*t, *bestNode);
    nodeComputationTimes[*bestNode] +=
        problem->plan.estimateRuntime(problem->cluster.nodes[*bestNode].nCores, t->pipeline, {}, {}, {});
    if (!assignedPartitions.contains(t->scannedPartition.partitionIndex)) {
      assignedPartitions[t->scannedPartition.partitionIndex] = *bestNode;
    }
    // Make result of task available at this node
    state.partitionManager->registerPartition(t->outputPartition, *bestNode);
  }
  state.assignBroadcastTasks();
  *state.partitionManager = currentPartitions;
  state.createShufflesAndTransfers();
  return *state.result;
}
//----------------------------------------------------------------------------------------------------------------------
std::string GreedyPlanner4::getName() const { return "Greedy4"; }
//----------------------------------------------------------------------------------------------------------------------
TaskAssignment GreedyPlanner4::assignTasks(const AssignmentProblem& newProblem) {
  problem = &newProblem;
  AssignmentPlannerState state;
  state.loadProblem(problem);
  std::vector<NodeId> nodes;
  PartitionManager currentPartitions = *state.partitionManager;

  // Keeping track of time spent using computation
  std::vector<double> nodeComputationTimes(problem->cluster.getComputeNodes().size(), 0.0);
  std::unordered_map<uint64_t, NodeId> assignedPartitions;

  for (const auto* t : state.flatTasks) {
    nodes.clear();
    Greedy::findEligibleNodes(*t, *state.partitionManager, nodes, problem->cluster);
    std::optional<NodeId> bestNode = Greedy::getBestNode(*t, nodes, *state.partitionManager, problem->plan);
    if (!bestNode) {
      uint64_t pIndex = t->scannedPartition.partitionIndex;
      if (assignedPartitions.contains(pIndex)) {
        bestNode = assignedPartitions[pIndex];
      } else {
        // select the node with the earliest finish time
        assert(bestNode == std::nullopt);
        double bestFinishTime = std::numeric_limits<double>::max();
        for (size_t iNode = 0; iNode < problem->cluster.getComputeNodes().size(); ++iNode) {
          double currentFinishTime =
              nodeComputationTimes[iNode] +
              problem->plan.estimateRuntime(problem->cluster.nodes[iNode].nCores, t->pipeline, {}, {}, {});
          if (currentFinishTime < bestFinishTime) {
            bestFinishTime = currentFinishTime;
            bestNode = iNode;
          }
        }
      }
    }
    assert(bestNode);
    state.result->locatedTasks.emplace_back(*t, *bestNode);
    nodeComputationTimes[*bestNode] +=
        problem->plan.estimateRuntime(problem->cluster.nodes[*bestNode].nCores, t->pipeline, {}, {}, {});
    if (!assignedPartitions.contains(t->scannedPartition.partitionIndex)) {
      assignedPartitions[t->scannedPartition.partitionIndex] = *bestNode;
    }
    // Make result of task available at this node
    state.partitionManager->registerPartition(t->outputPartition, *bestNode);
  }
  state.assignBroadcastTasks();
  *state.partitionManager = currentPartitions;
  state.createShufflesAndTransfers();
  return *state.result;
}
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

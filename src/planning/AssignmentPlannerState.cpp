#include "src/planning/AssignmentPlannerState.hpp"

#include <cassert>
#include <cstddef>
#include <memory>
#include <ranges>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "src/CustomTypes.hpp"
#include "src/Util.hpp"
#include "src/distributedplan/DataUnit.hpp"
#include "src/distributedplan/ProblemWriter.hpp"
#include "src/partitioning/Partition.hpp"
#include "src/partitioning/PartitionLayout.hpp"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2024 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace {
//----------------------------------------------------------------------------------------------------------------------
using namespace dqsim;
//----------------------------------------------------------------------------------------------------------------------
void cachePartition(PartitionManager& partitionManager, NodeId node, const Partition& partition,
                    std::vector<TaskAssignment::Transfer>& transfers) {
  if (!partitionManager.hasPartition(node, partition)) {
    assert(partitionManager.getOwningNode().contains(partition));
    NodeId owningNode = partitionManager.getOwningNode().find(partition)->second;
    transfers.emplace_back(TaskAssignment::Transfer{partition, owningNode, node});
    partitionManager.cachePartition(partition, node);
  }
}
//----------------------------------------------------------------------------------------------------------------------
void createTransfers(PartitionManager& partitionManager, NodeId node, const Task& task,
                     std::vector<TaskAssignment::Transfer>& transfers) {
  cachePartition(partitionManager, node, task.scannedPartition, transfers);
  for (const auto& partition : task.requiredPartitions) {
    cachePartition(partitionManager, node, partition, transfers);
  }
}
//----------------------------------------------------------------------------------------------------------------------
std::vector<Task> getDependencyOrder(const std::unordered_map<PipelineId, Task>& tasks,
                                     const DistributedQueryPlan& plan) {
  std::vector<Task> result;
  for (PipelineId p : plan.dependencyOrder) {
    if (tasks.contains(p)) result.push_back(tasks.find(p)->second);
  }
  assert(result.size() == tasks.size());
  return result;
}
//----------------------------------------------------------------------------------------------------------------------
}  // namespace
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
void AssignmentPlannerState::loadProblem(const AssignmentProblem* newProblem) {
  flatTasks.clear();
  problem = newProblem;
  for (const auto& shuffle : problem->plan.shuffleStages) {
    shuffledDUs[shuffle.from] = &shuffle;
  }
  partitionManager =
      std::make_unique<PartitionManager>(PartitionManager::loadPartitioning(problem->partitioning, &problem->cluster));
  for (PipelineId pipelineId : problem->plan.dependencyOrder) {
    if (problem->tasks.contains(pipelineId)) {
      for (const auto& task : problem->tasks.find(pipelineId)->second) {
        flatTasks.push_back(&task);
      }
    }
  }
  result = std::make_unique<TaskAssignment>();
}
//----------------------------------------------------------------------------------------------------------------------
void AssignmentPlannerState::assignBroadcastTasks(bool storeUsingNodes) {
  // assign broadcast tasks to all nodes, but maybe remove them from the nodes, that don't need to be assigned
  std::unordered_map<DataUnitId, std::vector<Task>> inputIds;
  for (const auto& [_, task] : problem->broadcastTasks) {
    inputIds[task.scannedPartition.dataUnitId].push_back(task);
    for (const auto& requiredPartition : task.requiredPartitions) {
      inputIds[requiredPartition.dataUnitId].push_back(task);
    }
  }
  std::vector<Task> dependencyOrder = getDependencyOrder(problem->broadcastTasks, problem->plan);

  // find all required nodes for each task
  auto usingNodes = getUsingNodes();
  for (const auto& task : std::ranges::views::reverse(dependencyOrder)) {
    DataUnitId outDU = task.outputPartition.dataUnitId;
    if (problem->plan.isShuffleInput(outDU)) {
      auto shuffledOutDU = problem->plan.getShuffleOutput(outDU);
      assert(shuffledOutDU.has_value());
      outDU = *shuffledOutDU;
      assert(problem->plan.getDataUnit(outDU).partitionLayout->type == PartitionLayoutType::SingleNode);
    }
    [[maybe_unused]] bool foundUser = false;
    for (NodeId nodeId{0}, iMax{problem->cluster.getComputeNodes().size()}; nodeId < iMax; ++nodeId) {
      if (usingNodes.contains(outDU) && usingNodes[outDU].contains(nodeId)) {
        foundUser = true;
        // add the task
        result->locatedTasks.emplace_back(TaskAssignment::LocatedTask{task, nodeId});
        // add the using nodes of this task
        usingNodes[task.scannedPartition.dataUnitId].insert(nodeId);
        for (const auto& requirement : task.requiredPartitions) {
          usingNodes[requirement.dataUnitId].insert(nodeId);
        }
      }
    }
    assert(foundUser);
  }
  if (storeUsingNodes) usingNodesCache = std::move(usingNodes);
}
//----------------------------------------------------------------------------------------------------------------------
void AssignmentPlannerState::createShufflesAndTransfers() {
  // compute all required locations for all partitions
  std::unordered_map<Partition, std::vector<NodeId>> requiredLocations;
  auto resultDU = problem->plan.getResultDataUnit();
  assert(problem->plan.getDataUnit(resultDU).partitionLayout->nPartitions == 1);
  // We always require the result to be at node 0
  requiredLocations[Partition(resultDU, 0)].push_back(NodeId{0});
  for (const auto& task : result->locatedTasks) {
    requiredLocations[task.task.scannedPartition].push_back(task.node);
    for (const auto& partition : task.task.requiredPartitions) {
      requiredLocations[partition].push_back(task.node);
    }
  }

  // assign shuffle targets according to located tasks
  for (const DataUnit& du : problem->plan.dataUnits) {
    if (shuffledDUs.contains(du.id)) {
      DataUnitId outputDU = shuffledDUs[du.id]->to;
      if (problem->plan.dataUnits[outputDU].partitionLayout->type == PartitionLayoutType::Broadcast) {
        Partition outPartition{outputDU, 0};
        assert(!(requiredLocations[outPartition].empty()));
        bool first = true;
        for (const auto& node : requiredLocations[outPartition]) {
          bool skipped = false;
          if (first)
            partitionManager->registerPartition(outPartition, node);
          else if (!partitionManager->hasPartition(node, outPartition))
            partitionManager->cachePartition(outPartition, node);
          else
            skipped = true;
          if (!skipped) result->shuffleTargets.emplace_back(TaskAssignment::ShuffleTarget{outPartition, node});
          first = false;
        }
      } else {
        auto nOutPartitions = problem->plan.dataUnits[outputDU].partitionLayout->nPartitions;
        for (size_t i = 0; i < nOutPartitions; ++i) {
          Partition shuffleOutPartition{outputDU, i};
          assert(requiredLocations.contains(shuffleOutPartition));
          assert(!requiredLocations[shuffleOutPartition].empty());
          NodeId node = requiredLocations[shuffleOutPartition].front();
          assert(!partitionManager->partitionStored(shuffleOutPartition));
          partitionManager->registerPartition(shuffleOutPartition, node);
          result->shuffleTargets.emplace_back(TaskAssignment::ShuffleTarget{shuffleOutPartition, node});
        }
      }
    }
  }

  // create missing transfers
  // register output partitions
  for (const auto& task : result->locatedTasks) {
    if (partitionManager->getOwningNode().contains(task.task.outputPartition)) {
      assert(problem->plan.getDataUnit(task.task.outputPartition.dataUnitId).partitionLayout->type ==
             PartitionLayoutType::Broadcast);
      assert(!partitionManager->hasPartition(task.node, task.task.outputPartition));
      partitionManager->cachePartition(task.task.outputPartition, task.node);
    } else {
      partitionManager->registerPartition(task.task.outputPartition, task.node);
    }
  }
  // check for locations of partitions and introduce necessary transfers
  for (const auto& task : result->locatedTasks) {
    createTransfers(*partitionManager, task.node, task.task, result->transfers);
  }
  // transfer result to result node if not there yet
  if (problem->resultNode.has_value()) {
    assert(!problem->plan.getDataUnit(problem->plan.getResultDataUnit()).partitionLayout->isPartitioned());
    if (!partitionManager->hasPartition(*problem->resultNode, Partition{problem->plan.getResultDataUnit(), 0}))
      cachePartition(*partitionManager, *problem->resultNode, Partition{problem->plan.getResultDataUnit(), 0},
                     result->transfers);
  }
}
//----------------------------------------------------------------------------------------------------------------------
void AssignmentPlannerState::liftFlatAssignment(std::span<const size_t> flatAssignment) {
  // assign tasks
  for (size_t i = 0; i < flatAssignment.size(); ++i) {
    result->locatedTasks.emplace_back(TaskAssignment::LocatedTask{*flatTasks[i], NodeId{flatAssignment[i]}});
  }
  // assign broadcast tasks to all nodes that are used
  assignBroadcastTasks();
  // create the necessary shuffles and transfers
  createShufflesAndTransfers();
}
//----------------------------------------------------------------------------------------------------------------------
std::unordered_map<DataUnitId, std::unordered_set<NodeId>> AssignmentPlannerState::getUsingNodes() {
  std::unordered_map<DataUnitId, std::unordered_set<NodeId>> nodes;
  for (const auto& task : result->locatedTasks) {
    nodes[task.task.scannedPartition.dataUnitId].insert(task.node);
    for (const auto& requiredPartition : task.task.requiredPartitions) {
      nodes[requiredPartition.dataUnitId].insert(task.node);
    }
  }
  for (const auto& transfer : result->transfers) {
    nodes[transfer.partition.dataUnitId].insert(transfer.from);
  }
  nodes[problem->plan.getResultDataUnit()].insert(NodeId{0});
  return nodes;
}
//----------------------------------------------------------------------------------------------------------------------
double AssignmentPlannerState::computePresentDataFraction(NodeId node, const Task& task,
                                                          const PartitionManager& partitionManager,
                                                          const DistributedQueryPlan& plan) {
  // For now, planners are unaware of skew
  auto partitionSize = [&plan](const Partition& p) { return plan.getDataUnit(p.dataUnitId).partitionSize({}); };

  double wholeSize = partitionSize(task.scannedPartition);
  for (const auto& p : task.requiredPartitions) {
    wholeSize += partitionSize(p);
  }

  if (wholeSize == 0) return 1;

  double presentSize = 0;
  if (partitionManager.hasPartition(node, task.scannedPartition)) {
    presentSize += partitionSize(task.scannedPartition);
  }
  for (const auto& p : task.requiredPartitions) {
    if (partitionManager.hasPartition(node, p)) {
      presentSize += partitionSize(p);
    }
  }
  return presentSize / wholeSize;
}
//----------------------------------------------------------------------------------------------------------------------
void AssignmentPlannerState::trimShuffleTargets() {
  assert(usingNodesCache);
  std::vector<size_t> deleteIndexes;
  for (size_t i = 0; i < result->shuffleTargets.size(); ++i) {
    const TaskAssignment::ShuffleTarget& t = result->shuffleTargets[i];
    DataUnitId du = t.partition.dataUnitId;
    if (!(usingNodesCache->contains(du) && (*usingNodesCache)[du].contains(t.location))) {
      deleteIndexes.push_back(i);
    }
  }
  filter(result->shuffleTargets, deleteIndexes);
}
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

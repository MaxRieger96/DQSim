#include "src/planning/PolarisPlanner.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <deque>
#include <limits>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "src/CustomTypes.hpp"
#include "src/SimulatorImpl.hpp"
#include "src/SimulatorSettings.hpp"
#include "src/Util.hpp"
#include "src/distributedplan/ProblemWriter.hpp"
#include "src/partitioning/PartitionLayout.hpp"
#include "src/planning/AssignmentPlannerState.hpp"
#include "src/planning/GreedyInfra.hpp"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2025 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
namespace {
//----------------------------------------------------------------------------------------------------------------------
void addInputTransfers(SimulatorImpl& sim, std::deque<TaskAssignment::Transfer>& transfers, const Task& task,
                       NodeId node, NodeId storageNode, const std::unordered_set<DataUnitId>& ignoredDUs,
                       std::unordered_set<Partition>& partitionsAvailableOnStorage) {
  for (const auto& d : task.getInputPartitions()) {
    assert(sim.partitions.partitionStored(d));
    if (ignoredDUs.contains(d.dataUnitId) || sim.partitions.hasPartition(node, d)) continue;
    // In this version, we always download from storage, no P2P
    if (!partitionsAvailableOnStorage.contains(d)) {
      auto& transfer = transfers.emplace_back(d, sim.partitions.getOwningNode().find(d)->second, storageNode);
      sim.pendingTransfers.push_back(&transfer);
      partitionsAvailableOnStorage.insert(d);
    }
    auto& transfer = transfers.emplace_back(d, storageNode, node);
    sim.pendingTransfers.push_back(&transfer);
  }
}
//----------------------------------------------------------------------------------------------------------------------
void addInputTransfersP2P(SimulatorImpl& sim, std::deque<TaskAssignment::Transfer>& transfers, const Task& task,
                          NodeId node, const std::unordered_set<DataUnitId>& ignoredDUs,
                          const std::unordered_set<Partition>& ignoredPartitions) {
  for (const auto& d : task.getInputPartitions()) {
    assert(sim.partitions.partitionStored(d));
    if (ignoredDUs.contains(d.dataUnitId) || ignoredPartitions.contains(d) || sim.partitions.hasPartition(node, d))
      continue;
    NodeId sourceNode;
    double bytesTransferred = sim.plan->getDataUnit(d.dataUnitId).partitionSize({});
    double bestTime = std::numeric_limits<double>::max();
    for (const auto& n : sim.partitions.owningAndCachingNodes(d)) {
      double duration =
          bytesTransferred / std::min(sim.cluster->nodes[n].byteBandwidth(), sim.cluster->nodes[node].byteBandwidth());
      duration += std::max(sim.cluster->nodes[n].networkLatency, sim.cluster->nodes[node].networkLatency);
      if (duration < bestTime) {
        bestTime = duration;
        sourceNode = n;
      }
    }
    assert(bestTime < std::numeric_limits<double>::max());
    auto& transfer = transfers.emplace_back(d, sourceNode, node);
    sim.pendingTransfers.push_back(&transfer);
  }
}
//----------------------------------------------------------------------------------------------------------------------
NodeId bestNode(const PartitionManager& partitions, const Task& task, const Cluster& cluster,
                const DistributedQueryPlan& plan, NodeId& currentNode) {
  size_t nNodes = cluster.getComputeNodes().size();
  std::vector<NodeId> eligibleNodes;
  Greedy::findEligibleNodes(task, partitions, eligibleNodes, cluster);
  std::optional<NodeId> bestNode;
  // std::optional<NodeId> bestNode = Greedy::getBestNode(task, eligibleNodes, partitions, plan);
  Greedy::getBestNodes(task, eligibleNodes, partitions, plan);
  std::sort(eligibleNodes.begin(), eligibleNodes.end());
  if (eligibleNodes.empty()) {
    // if (!bestNode) {
    // Fall back to round-robin when we do not have any information
    bestNode = currentNode;
    currentNode = (currentNode + 1) % nNodes;
  } else if (eligibleNodes.size() > 1) {
    auto it = std::lower_bound(eligibleNodes.begin(), eligibleNodes.end(), currentNode);
    if (it != eligibleNodes.end()) {
      bestNode = *it;
    } else {
      bestNode = eligibleNodes.front();
    }
    currentNode = (*bestNode + 1) % nNodes;
  } else {
    bestNode = eligibleNodes.front();
  }
  assert(bestNode);
  return *bestNode;
}
//----------------------------------------------------------------------------------------------------------------------
}  // namespace
//----------------------------------------------------------------------------------------------------------------------
TaskAssignment PolarisPlanner::assignTasks(const AssignmentProblem& newProblem) {
  assert(allowP2P || forceUpload);  // non p2p only works with forced upload
  problem = &newProblem;

  // We use deques for pointer stability
  std::deque<TaskAssignment::LocatedTask> taskLocations;
  std::deque<TaskAssignment::Transfer> transfers;
  std::vector<TaskAssignment::ShuffleTarget> shuffleTargets;

  // Mark dataUnits which will be present thanks to broadcasts anyway
  std::unordered_set<DataUnitId> DUsAvailableByBroadcastTasks;
  std::unordered_set<Partition> partitionsAvailableByBroadcastShuffle;
  std::unordered_set<Partition> partitionsAvailableOnStorage;

  assert(problem->cluster.storageService);
  NodeId storageNode{problem->cluster.getComputeNodes().size()};
  assert(storageNode == NodeId{problem->cluster.nodes.size() - 1});

  {
    auto partitioning = PartitionManager::loadPartitioning(problem->partitioning, &problem->cluster);
    assert(partitioning.getOwnedPartitions().contains(storageNode));
    for (const auto& p : partitioning.getOwnedPartitions().find(storageNode)->second) {
      partitionsAvailableOnStorage.insert(p);
    }
    assert(partitioning.getCachedPartitions().contains(storageNode));
    for (const auto& p : partitioning.getCachedPartitions().find(storageNode)->second) {
      partitionsAvailableOnStorage.insert(p);
    }
  }

  // All shuffle targets go to storage node
  for (const auto& s : problem->plan.shuffleStages) {
    const DataUnit& du = problem->plan.getDataUnit(s.to);
    if (allowP2P && allowBroadcastPipelines && du.partitionLayout->type == PartitionLayoutType::Broadcast) {
      // shuffles of broadcast shuffles should go to all nodes if P2P is allowed
      Partition p{du.id, 0};
      assert(du.partitionLayout->nPartitions == 1);
      for (NodeId n{0}; n < problem->cluster.getComputeNodes().size(); ++n) {
        shuffleTargets.emplace_back(p, n);
      }
      partitionsAvailableByBroadcastShuffle.insert(p);
    } else {
      for (size_t iPartition = 0; iPartition < du.partitionLayout->nPartitions; ++iPartition) {
        Partition p{du.id, iPartition};
        NodeId targetNode;
        if (allowP2P && !forceUpload) {
          targetNode = p.partitionIndex % problem->cluster.getComputeNodes().size();
        } else {
          targetNode = storageNode;
          partitionsAvailableOnStorage.insert(p);
        }
        shuffleTargets.emplace_back(p, targetNode);
      }
    }
  }

  TaskAssignment dummy{};
  dummy.shuffleTargets = shuffleTargets;
  SimulatorImpl sim(problem->cluster, &dummy, problem->partitioning, &problem->plan, problem->resultNode,
                    SimulatorSettings::getDefault());

  std::vector<Task> pendingTasks;
  for (const auto& [p, ts] : problem->tasks) {
    pendingTasks.insert(pendingTasks.end(), ts.begin(), ts.end());
  }

  if (allowBroadcastPipelines) {
    // Place broadcast tasks everywhere
    for (const auto& [_, task] : problem->broadcastTasks) {
      DUsAvailableByBroadcastTasks.insert(task.outputPartition.dataUnitId);
    }
    for (const auto& [_, task] : problem->broadcastTasks) {
      for (NodeId n{0}; n < problem->cluster.getComputeNodes().size(); ++n) {
        auto& locatedTask = taskLocations.emplace_back(task, n);
        sim.pendingTasks.push_back(&locatedTask);
        // Broadcast partitions should be transferred to all nodes if P2P is not allowed
        for (const auto& i : task.getInputPartitions()) {
          const auto& du = problem->plan.getDataUnit(i.dataUnitId);
          if (DUsAvailableByBroadcastTasks.contains(i.dataUnitId)) continue;
          if (!allowP2P && !sim.partitions.hasPartition(n, i)) {
            auto& transfer = transfers.emplace_back(i, storageNode, n);
            sim.pendingTransfers.push_back(&transfer);
          } else if (!du.isIntermediate) {
            // For base relations, data could be missing even if P2P is allowed
            //  (for intermediate results we always shuffle to all nodes)
            if (!sim.partitions.hasPartition(n, i)) {
              auto& transfer = transfers.emplace_back(i, storageNode, n);
              sim.pendingTransfers.push_back(&transfer);
            }
          }
        }
      }
      // Only upload result once
      if (forceUpload) {
        // We changed this to only upload at shuffle boundaries -> shuffle targets
        // auto& transfer = transfers.emplace_back(task.outputPartition, NodeId{0}, storageNode);
        // sim.pendingTransfers.push_back(&transfer);
      }
    }
  } else {
    for (const auto& [p, t] : problem->broadcastTasks) {
      pendingTasks.push_back(t);
    }
  }

  std::vector<size_t> tasksToStart;
  NodeId currentNode{0};
  while (!pendingTasks.empty()) {
    // get available tasks
    tasksToStart.clear();
    for (size_t iTask = 0; iTask < pendingTasks.size(); ++iTask) {
      if (sim.inputAvailable(pendingTasks[iTask])) {
        tasksToStart.push_back(iTask);
      }
    }
    // schedule available tasks
    for (const auto& iTask : tasksToStart) {
      const Task& task = pendingTasks[iTask];
      // find best node
      NodeId node = bestNode(sim.partitions, task, problem->cluster, problem->plan, currentNode);
      // Perform all necessary transfers
      if (allowP2P)
        addInputTransfersP2P(sim, transfers, task, node, DUsAvailableByBroadcastTasks,
                             partitionsAvailableByBroadcastShuffle);
      else
        addInputTransfers(sim, transfers, task, node, storageNode, DUsAvailableByBroadcastTasks,
                          partitionsAvailableOnStorage);
      // store located task
      auto& locatedTask = taskLocations.emplace_back(task, node);
      sim.pendingTasks.push_back(&locatedTask);
      // We changed this to only upload at shuffle boundaries -> shuffle targets
      // if (forceUpload) {
      //   // push output to storage service
      //   auto& transfer = transfers.emplace_back(task.outputPartition, node, storageNode);
      //   sim.pendingTransfers.push_back(&transfer);
      // }
    }

    // run the simulator
    filter(pendingTasks, tasksToStart);
    assert(!sim.aborted);
    sim.findReadyJobs();
    sim.runStep(true);
    sim.updatePartialJobs();
    assert(!sim.aborted);
  }

  bool resultPlaced = false;
  Partition resultPartition{problem->plan.getResultDataUnit(), 0};
  while (sim.isRunning()) {
    // transfer result to node 0
    if (!resultPlaced && sim.partitions.partitionStored(resultPartition)) {
      NodeId sourceNode = sim.partitions.getOwningNode().find(resultPartition)->second;
      resultPlaced = true;
      if (sourceNode != NodeId{0}) {
        auto& transfer = transfers.emplace_back(resultPartition, sourceNode, NodeId{0});
        sim.pendingTransfers.push_back(&transfer);
      }
    }
    sim.findReadyJobs();
    sim.runStep(true);
    sim.updatePartialJobs();
  }
  assert(sim.done);

  std::optional<std::vector<Partition>> durablePartitions;
  if (writeToDisk) {
    durablePartitions = std::vector<Partition>();
    for (const auto& sh : problem->plan.shuffleStages) {
      const auto& du = problem->plan.getDataUnit(sh.from);
      for (std::size_t iPartition = 0; iPartition < du.partitionLayout->nPartitions; ++iPartition) {
        durablePartitions->push_back(Partition{du.id, iPartition});
      }
    }
  }
  return {{taskLocations.begin(), taskLocations.end()},
          {transfers.begin(), transfers.end()},
          shuffleTargets,
          durablePartitions};
}
//----------------------------------------------------------------------------------------------------------------------
std::string PolarisPlanner::getName() const {
  std::string result;
  if (allowP2P && !forceUpload) {
    result = "StateSepNoUp";
  } else if (allowP2P) {
    result = "StateSepP2P";
  } else if (forceUpload) {
    assert(!allowP2P);
    result = "StateSep";
  } else {
    return "Unknown StateSep";
  }
  if (writeToDisk) {
    result += "D";
  }
  if (allowBroadcastPipelines) {
    result += "+B";
  }
  return result;
}
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim

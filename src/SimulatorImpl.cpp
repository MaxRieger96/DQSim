#include "src/SimulatorImpl.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <format>
#include <limits>
#include <numeric>
#include <optional>
#include <print>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "src/Cluster.hpp"
#include "src/CustomTypes.hpp"
#include "src/MemorySimulator.hpp"
#include "src/Simulator.hpp"
#include "src/Util.hpp"
#include "src/distributedplan/DistributedQueryPlan.hpp"
#include "src/distributedplan/Pipeline.hpp"
#include "src/distributedplan/ProblemWriter.hpp"
#include "src/network/NetworkSimulator.hpp"
#include "src/partitioning/Partition.hpp"
#include "src/partitioning/PartitionLayout.hpp"
#include "src/partitioning/PartitionManager.hpp"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2022 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace {
//----------------------------------------------------------------------------------------------------------------------
using namespace dqsim;
//----------------------------------------------------------------------------------------------------------------------
double taskProgress(double previousProgress, double progressLimit, double remainingTime, double stepTime) {
  if (remainingTime == std::numeric_limits<double>::infinity()) return 0;
  if (remainingTime == 0) return progressLimit - previousProgress;
  double completionFraction = stepTime / remainingTime;
  assert(!(completionFraction != completionFraction));
  return (progressLimit - previousProgress) * completionFraction;
}
//----------------------------------------------------------------------------------------------------------------------
bool checkReady(const TaskAssignment::LocatedTask& task, const PartitionManager& partitions) {
  for (Partition partition : task.task.getInputPartitions()) {
    if (!partitions.hasPartition(task.node, partition)) return false;
  }
  return true;
}
//----------------------------------------------------------------------------------------------------------------------
bool checkReady(const TaskAssignment::Transfer& transfer, const PartitionManager& partitions) {
  return partitions.hasPartition(transfer.from, transfer.partition);
}
//----------------------------------------------------------------------------------------------------------------------
}  // namespace
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
SimulatorImpl::SimulatorImpl(const Cluster& cluster, const TaskAssignment* solution, const Partitioning& partitioning,
                             const DistributedQueryPlan* plan, std::optional<NodeId> resultNode,
                             SimulatorSettings settings)
    : shuffledDataUnits(),
      pendingTasks(),
      pendingTransfers(),
      shuffleTargets(),
      runningTasks(),
      runningTransfers(),
      runningShuffles(),
      collectedShuffles(),
      partitions(PartitionManager::loadPartitioning(partitioning, &cluster)),
      memorySimulator(std::span{plan->dataUnits.begin(), plan->dataUnits.end()}, cluster),
      networkSimulator(&cluster),
      diskTimes(cluster.nodes.size(), 0.0),
      pendingDiskWrites(),
      cluster(&cluster),
      plan(plan),
      resultNode(std::move(resultNode)),
      settings(std::move(settings)) {
  for (const auto& task : solution->locatedTasks) {
    pendingTasks.push_back(&task);
  }
  for (const auto& transfer : solution->transfers) {
    pendingTransfers.push_back(&transfer);
  }
  for (const auto& shuffleTarget : solution->shuffleTargets) {
    shuffleTargets[shuffleTarget.partition].push_back(shuffleTarget.location);
  }
  for (const auto& shuffleStage : plan->shuffleStages) {
    shuffledDataUnits[shuffleStage.from] = &shuffleStage;
    const DataUnit& fromDU = plan->getDataUnit(shuffleStage.from);
    for (size_t iPartition = 0; iPartition < fromDU.partitionLayout->nPartitions; ++iPartition) {
      Partition partition{fromDU.id, iPartition};
      pendingShuffles.push_back(partition);
    }
  }
  for (const auto& [p, n] : partitioning.owningNode) {
    memorySimulator.storePartition(n, p, getSkewFactor(p));
  }
  for (const auto& [p, nodes] : partitioning.cachingNodes) {
    for (NodeId n : nodes) {
      memorySimulator.storePartition(n, p, getSkewFactor(p));
    }
  }
  for (NodeId n{0}; n < cluster.nodes.size(); ++n) {
    partialPartitions[n];
  }
  if (solution->durablePartitions) {
    pendingDiskWrites = *solution->durablePartitions;
  }
  consistencyCheck();
}
//----------------------------------------------------------------------------------------------------------------------
void SimulatorImpl::consistencyCheck() const {
  if (settings.partitionSkewFactors) {
    assert(settings.partitionSkewFactors->size() == plan->dataUnits.size());
    for (std::size_t i = 0; i < settings.partitionSkewFactors->size(); ++i) {
      auto nPartitions = plan->dataUnits[i].partitionLayout->nPartitions;
      assert((*settings.partitionSkewFactors)[i].size() == nPartitions);
      double sum = 0;
      for (std::size_t j = 0; j < (*settings.partitionSkewFactors)[i].size(); ++j) {
        sum += (*settings.partitionSkewFactors)[i][j];
      }
      assert(std::abs(sum - static_cast<double>(nPartitions)) < 1e-9);
    }
  }
  if (settings.taskSlowdownFactors) {
    if (settings.taskSlowdownFactors->size() != plan->pipelines.size()) {
      std::println(stderr, "[ERROR] taskSlowdownFactors size mismatch: expected {} (pipelines), got {}",
                   plan->pipelines.size(), settings.taskSlowdownFactors->size());
    }
    assert(settings.taskSlowdownFactors->size() == plan->pipelines.size());

    for (size_t i = 0; i < settings.taskSlowdownFactors->size(); ++i) {
      DataUnitId scanDUId = plan->getPipelines()[i].scan;
      const DataUnit& scanDU = plan->getDataUnit(scanDUId);

      if ((*settings.taskSlowdownFactors)[i].size() != scanDU.partitionLayout->nPartitions) {
        std::println(stderr, "[ERROR] pipeline {} slowdown factor count mismatch: expected {} (partitions), got {}", i,
                     scanDU.partitionLayout->nPartitions, (*settings.taskSlowdownFactors)[i].size());
      }
      assert((*settings.taskSlowdownFactors)[i].size() == scanDU.partitionLayout->nPartitions);

      double sum = 0;
      for (const auto& factor : (*settings.taskSlowdownFactors)[i]) {
        sum += factor;
      }

      if (std::abs(sum - static_cast<double>((*settings.taskSlowdownFactors)[i].size())) >= 1e-4) {
        std::println(stderr, "[ERROR] pipeline {} slowdown factors sum mismatch: sum = {}, expected {}", i, sum,
                     (*settings.taskSlowdownFactors)[i].size());
      }
      assert(std::abs(sum - static_cast<double>((*settings.taskSlowdownFactors)[i].size())) < 1e-4);
    }
  }
}
//----------------------------------------------------------------------------------------------------------------------
bool SimulatorImpl::isRunning() const { return (!done) & (!aborted); }
//----------------------------------------------------------------------------------------------------------------------
Simulator::Result SimulatorImpl::simulate() {
  while (isRunning()) {
    findReadyJobs();
    runStep();
    updatePartialJobs();
  }
  waitForDisk();

  return Simulator::Result{runtime, done, abortMessage};
}
//----------------------------------------------------------------------------------------------------------------------
void SimulatorImpl::findReadyJobs() {
  // Shuffles need to be started before tasks because local shuffles will run immediately and then task should be able
  // to start
  // start shuffles
  instantlyFinishedShuffles = 0;
  std::vector<size_t> startedShuffles;
  for (size_t i = 0; i < pendingShuffles.size(); ++i) {
    auto p = pendingShuffles[i];
    const DataUnit& sourceDU = plan->getDataUnit(p.dataUnitId);
    assert(shuffledDataUnits.contains(sourceDU.id));
    DataUnitId targetDU = shuffledDataUnits.find(sourceDU.id)->second->to;
    if (partitions.partitionStored(p)) {
      const DataUnit& sourceDU = plan->getDataUnit(p.dataUnitId);
      assert(shuffledDataUnits.contains(sourceDU.id));
      DataUnitId targetDU = shuffledDataUnits.find(sourceDU.id)->second->to;
      NodeId sourceNode = partitions.getOwningNode().find(p)->second;

      Partition dummyTargetPartition{targetDU, 0};
      // small optimization for shuffling broadcast to single node
      NodeId perfectNode = shuffleTargets[dummyTargetPartition].front();
      bool inputIsBroadcast = sourceDU.partitionLayout->type == PartitionLayoutType::Broadcast;
      if (inputIsBroadcast && partitions.hasPartition(perfectNode, p)) sourceNode = perfectNode;
      if (inputIsBroadcast) completedShuffles.insert(sourceDU.id);
      startShuffle(ShuffleStage{sourceDU.id, targetDU}, sourceNode, p, 1);
      startedShuffles.push_back(i);
    } else if (settings.allowStreamingInput) {
      // start shuffles unconditionally
      if (!firstPartialPartitionNodes.contains(p)) continue;
      NodeId sourceNode = firstPartialPartitionNodes.find(p)->second;
      double limit = partiallyAvailableFraction(p, sourceNode);
      startShuffle(ShuffleStage{sourceDU.id, targetDU}, sourceNode, p, limit);
      startedShuffles.push_back(i);
    }
  }
  filter(pendingShuffles, startedShuffles);

  std::vector<size_t> nTasksPerNode(cluster->nodes.size(), 0);
  for (const auto& t : runningTasks) {
    ++nTasksPerNode[t.task.node];
  }
  std::vector<size_t> startedTasks;
  for (size_t i = 0; i < pendingTasks.size(); ++i) {
    std::size_t node = pendingTasks[i]->node;
    bool started = false;
    if (checkReady(*pendingTasks[i], partitions) && nTasksPerNode[node] < settings.nParallelTasks) {
      runningTasks.emplace_back(pendingTasks[i]);
      started = true;
    } else if (settings.allowStreamingInput && checkPartiallyReady(*pendingTasks[i], 0).first) {
      runningTasks.emplace_back(pendingTasks[i], checkPartiallyReady(*pendingTasks[i], 0).second);
      started = true;
    }
    if (started) {
      ++nTasksPerNode[node];
      startedTasks.push_back(i);
      const auto& p = pendingTasks[i]->task.outputPartition;
      memorySimulator.storePartition(pendingTasks[i]->node, p, getSkewFactor(p));
    }
  }
  filter(pendingTasks, startedTasks);

  std::vector<size_t> startedTransfers;
  for (size_t i = 0; i < pendingTransfers.size(); ++i) {
    auto transferAbort = [this]() {
      // When this encounters, we assume there is a problem in the assignment algorithm
      aborted = true;
      abortMessage =
          "Transferred a partition to a node that already has it. "
          "This implies planning inefficiencies.";
    };

    const TaskAssignment::Transfer* currentTransfer = pendingTransfers[i];

    double latency = std::max(cluster->nodes[currentTransfer->from].networkLatency,
                              cluster->nodes[currentTransfer->to].networkLatency);
    bool started = false;
    if (checkReady(*currentTransfer, partitions)) {
      runningTransfers.emplace_back(currentTransfer, latency);
      started = true;
    } else if (settings.allowStreamingInput && checkPartiallyReady(*currentTransfer, 0).first) {
      runningTransfers.emplace_back(currentTransfer, latency, checkPartiallyReady(*currentTransfer, 0).second);
      started = true;
    }

    if (started) {
      if (partitions.hasPartition(currentTransfer->to, currentTransfer->partition)) {
        transferAbort();
        return;
      }
      startedTransfers.push_back(i);
      memorySimulator.storePartition(currentTransfer->to, currentTransfer->partition,
                                     getSkewFactor(currentTransfer->partition));
    }
  }
  filter(pendingTransfers, startedTransfers);

  std::vector<std::size_t> startedDiskWrites;
  for (std::size_t iDurable = 0; iDurable < pendingDiskWrites.size(); ++iDurable) {
    Partition partition = pendingDiskWrites[iDurable];
    auto it = partitions.getOwningNode().find(partition);
    if (it != partitions.getOwningNode().end()) {
      NodeId writingNode = it->second;
      double writingTime = getDiskWritingTime(partitionSize(partition));
      assert(writingTime > 0);
      assert(diskTimes.size() >= writingNode);
      diskTimes[writingNode] += writingTime;
      startedDiskWrites.push_back(iDurable);
    }
  }
  filter(pendingDiskWrites, startedDiskWrites);
}
//----------------------------------------------------------------------------------------------------------------------
void SimulatorImpl::updatePartialJobs() {
  if (!settings.allowStreamingInput) return;
  for (auto& task : runningTasks) {
    if (task.progressLimit == 1) continue;
    if (checkReady(task.task, partitions)) {
      // Input is fully present
      task.progressLimit = 1;
      task.waitingForInput = false;
    } else {
      auto [canRun, progressLimit] = checkPartiallyReady(task.task, task.progress);
      if (canRun) {
        // Task progress limit is updated
        task.progressLimit = progressLimit;
        task.waitingForInput = false;
      } else {
        // Task needs to be suspendend
        task.waitingForInput = true;
      }
    }
  }

  for (auto& transfer : runningTransfers) {
    if (transfer.progressLimit == 1) continue;
    if (checkReady(transfer.transfer, partitions)) {
      // Input is fully present
      transfer.progressLimit = 1;
      transfer.waitingForInput = false;
    } else {
      auto [canRun, progressLimit] = checkPartiallyReady(transfer.transfer, transfer.progress);
      if (canRun) {
        // Transfer progress limit is updated
        transfer.progressLimit = progressLimit;
        transfer.waitingForInput = false;
      } else {
        // Transfer needs to be suspended
        transfer.waitingForInput = true;
      }
    }
  }

  for (auto& shuffle : runningShuffles) {
    for (std::size_t iInPart = 0; iInPart < shuffle.inPartitions.size(); ++iInPart) {
      if (partitions.hasPartition(shuffle.from, shuffle.inPartitions[iInPart])) {
        shuffle.progressLimits[iInPart] = 1;
      } else {
        assert(partialPartitions.contains(shuffle.from));
        assert(partialPartitions.find(shuffle.from)->second.contains(shuffle.inPartitions[iInPart]));
        shuffle.progressLimits[iInPart] =
            partialPartitions.find(shuffle.from)->second.find(shuffle.inPartitions[iInPart])->second;
      }
    }
  }
}
//----------------------------------------------------------------------------------------------------------------------
void SimulatorImpl::finishShuffle(const RunningShuffle& shuffle) {
  // update shuffle collects, if a partition is fully collected, store it in partition manager
  for (const auto& partition : shuffle.outPartitions) {
    LocatedPartition target{partition, shuffle.to};
    assert(collectedShuffles.contains(target));
    collectedShuffles[target].progress += inputFraction(shuffle);
    assert(collectedShuffles[target].progress <= 1 + progressTolerance);
    if (collectedShuffles[target].progress >= 1.0 - progressTolerance) {
      auto targetNode = collectedShuffles[target].node;

      if (settings.allowStreamingInput) {
        assert(partialPartitions.contains(targetNode));
        assert(partialPartitions.find(targetNode)->second.contains(partition));
        assert(partialPartitions.find(targetNode)->second.find(partition)->second >= progressTolerance);
        partialPartitions[targetNode].erase(partition);
      }

      if (partitions.partitionStored(partition)) {
        partitions.cachePartition(partition, targetNode);
      } else {
        partitions.registerPartition(partition, targetNode);
      }
      collectedShuffles.erase(target);
    }
  }
}
//----------------------------------------------------------------------------------------------------------------------
bool SimulatorImpl::inputAvailable(const dqsim::Task& task) const {
  for (const auto& d : task.getInputPartitions()) {
    if (!partitions.partitionStored(d)) return false;
  }
  return true;
}
//----------------------------------------------------------------------------------------------------------------------
std::string SimulatorImpl::getStuckInfo() const {
  std::unordered_map<PipelineId, size_t> dependencyIndex;
  for (size_t i = 0; i < plan->dependencyOrder.size(); ++i) {
    dependencyIndex[plan->dependencyOrder[i]] = i;
  }
  size_t bestDepIndex = plan->dependencyOrder.size();
  size_t smallestPartitionId = std::numeric_limits<size_t>::max();
  const TaskAssignment::LocatedTask* bestTask = nullptr;
  if (pendingTasks.empty()) return "All tasks have finished";
  for (const auto& t : pendingTasks) {
    if (bestTask && bestDepIndex < dependencyIndex[t->task.pipeline]) continue;
    if (bestTask && smallestPartitionId < t->task.scannedPartition.partitionIndex) continue;
    bestTask = t;
    bestDepIndex = dependencyIndex[t->task.pipeline];
    smallestPartitionId = t->task.scannedPartition.partitionIndex;
  }
  if (!bestTask) return "";
  std::string missingPartitionInfo;
  std::vector<Partition> missingPartitions;
  for (const auto& p : bestTask->task.getInputPartitions()) {
    if (partitions.hasPartition(bestTask->node, p)) continue;
    std::string nodes;
    for (const auto& n : partitions.owningAndCachingNodes(p)) {
      nodes.append(std::format("Node {}, ", static_cast<size_t>(n)));
    }
    if (nodes.empty()) nodes = "No node stores this partition";
    missingPartitionInfo.append(std::format("{}.{}: present on [{}]", static_cast<size_t>(p.dataUnitId),
                                            static_cast<size_t>(p.partitionIndex), nodes));
  }
  return std::format("Task {}.{} should be executed on Node {}.\n{}", static_cast<size_t>(bestTask->task.pipeline),
                     static_cast<size_t>(bestTask->task.scannedPartition.partitionIndex),
                     static_cast<size_t>(bestTask->node), missingPartitionInfo);
}
//----------------------------------------------------------------------------------------------------------------------
void SimulatorImpl::runStep(bool allowEmptyTasks) {
  // Check for deadlock
  if (runningTasks.empty() && runningTransfers.empty() && runningShuffles.empty() && instantlyFinishedShuffles == 0 &&
      !done && !allowEmptyTasks) {
    aborted = true;
    abortMessage = "No tasks or transfers can run anymore before the simulation finished\n";
    abortMessage.append(getStuckInfo());
    return;
  }
  // {
  //    auto resultDU = plan->getDataUnit(plan->getResultDataUnit());
  //    assert(!resultDU.partitionLayout->isPartitioned());
  //    if (partitions.hasPartition(NodeId{0}, Partition{resultDU.id, 0})) {
  //       std::cout << "WHY ARE YOU RUNNING?" << std::endl;
  //    }
  // }
  {
    std::vector<size_t> nTasksPerNode(cluster->nodes.size(), 0);
    for (const auto& t : runningTasks) {
      ++nTasksPerNode[t.task.node];
      assert(nTasksPerNode[t.task.node] <= settings.nParallelTasks);
    }
  }

  // Compute step time
  auto taskTimes = taskRuntimes();
  auto [transferTimes, shuffleTimes] = transferRuntimes();

  // Return early if there are not jobs in this step
  if (taskTimes.size() + transferTimes.size() + shuffleTimes.size() == 0) {
    assert(instantlyFinishedShuffles > 0 || allowEmptyTasks);
    finishStep(allowEmptyTasks, instantlyFinishedShuffles);
    return;
  }

  double stepTime = std::numeric_limits<double>::infinity();
  if (taskTimes.size() > 0) stepTime = std::ranges::min_element(taskTimes)[0];
  if (transferTimes.size() > 0) stepTime = std::min(stepTime, std::ranges::min_element(transferTimes)[0]);
  if (shuffleTimes.size() > 0) stepTime = std::min(stepTime, std::ranges::min_element(shuffleTimes)[0]);
  assert(stepTime != std::numeric_limits<double>::infinity());
  assert(stepTime >= 0);  // Sometimes DUs have zero bytes. Then the step time is 0

  if (settings.limitStepSize) stepTime = std::min(stepTime, settings.stepLimit);

  // Update progress and find all tasks that finish in step
  [[maybe_unused]] size_t progressedPartials = 0;
  std::vector<size_t> finishingTasks;
  for (size_t i = 0; i < taskTimes.size(); ++i) {
    if (runningTasks[i].waitingForInput) continue;
    double progressDelta =
        taskProgress(runningTasks[i].progress, runningTasks[i].progressLimit, taskTimes[i], stepTime);
    runningTasks[i].progress += progressDelta;
    if (runningTasks[i].progressLimit < 1) ++progressedPartials;
    updatePartialPartition(runningTasks[i].task.task.outputPartition, runningTasks[i].task.node, progressDelta);
    if (runningTasks[i].progress >= 1 - progressTolerance && runningTasks[i].progress == 1) {
      finishingTasks.push_back(i);
    }
    assert(runningTasks[i].progress <= 1 + progressTolerance);
  }

  [[maybe_unused]] size_t finishingLatencies = 0;
  std::vector<size_t> finishingTransfers;
  for (size_t i = 0; i < transferTimes.size(); ++i) {
    if (runningTransfers[i].waitingForInput) continue;
    if (runningTransfers[i].latencyTime > 0) {
      assert(runningTransfers[i].latencyTime >= stepTime);
      runningTransfers[i].latencyTime -= stepTime;
      if (runningTransfers[i].latencyTime < timeTolerance) {
        runningTransfers[i].latencyTime = 0;
        ++finishingLatencies;
      }
    } else {
      double progressDelta =
          taskProgress(runningTransfers[i].progress, runningTransfers[i].progressLimit, transferTimes[i], stepTime);
      runningTransfers[i].progress += progressDelta;
      assert(runningTransfers[i].progress <= runningTransfers[i].progressLimit + progressTolerance);
      if (runningTransfers[i].progressLimit < 1) ++progressedPartials;
      updatePartialPartition(runningTransfers[i].transfer.partition, runningTransfers[i].transfer.to, progressDelta);
      if (runningTransfers[i].progress >= 1 - progressTolerance && runningTransfers[i].progressLimit == 1) {
        finishingTransfers.push_back(i);
      }
    }
    assert(runningTransfers[i].progress <= 1 + progressTolerance);
  }

  std::vector<size_t> finishingShuffles;
  for (size_t i = 0; i < runningShuffles.size(); ++i) {
    if (shuffleWaitingForInput(runningShuffles[i], settings.minimumBlockSize)) continue;
    if (runningShuffles[i].latencyTime > 0) {
      assert(runningShuffles[i].latencyTime >= stepTime);
      runningShuffles[i].latencyTime -= stepTime;
      if (runningShuffles[i].latencyTime < timeTolerance) {
        runningShuffles[i].latencyTime = 0;
        ++finishingLatencies;
      }
    } else {
      double limit = runningShuffles[i].progressLimit();
      double progressDelta = taskProgress(runningShuffles[i].progress, limit, shuffleTimes[i], stepTime);
      if (runningShuffles[i].from == runningShuffles[i].to) progressDelta = limit - runningShuffles[i].progress;
      runningShuffles[i].progress += progressDelta;
      assert(runningShuffles[i].progress <= limit + progressTolerance);
      if (limit < 1) ++progressedPartials;
      for (const auto& p : runningShuffles[i].outPartitions) {
        updatePartialPartition(p, runningShuffles[i].to, progressDelta * inputFraction(runningShuffles[i]));
      }
      if (runningShuffles[i].progress >= 1 - progressTolerance) {
        finishingShuffles.push_back(i);
      }
    }
  }

  if (settings.allowStreamingInput)
    assert(settings.limitStepSize || (finishingTasks.size() + finishingTransfers.size() + finishingShuffles.size() +
                                          finishingLatencies + progressedPartials >
                                      0));
  else
    assert(settings.limitStepSize ||
           finishingTasks.size() + finishingTransfers.size() + finishingShuffles.size() + finishingLatencies > 0);

  // move output partitions of finished tasks to target nodes
  for (size_t task : finishingTasks) {
    auto node = runningTasks[task].task.node;
    auto outPartition = runningTasks[task].task.task.outputPartition;

    if (settings.allowStreamingInput) {
      assert(partialPartitions.contains(node));
      assert(partialPartitions.find(node)->second.contains(outPartition));
      assert(partialPartitions.find(node)->second.find(outPartition)->second >= 1 - progressTolerance);
      partialPartitions[node].erase(outPartition);
    }

    if (partitions.getOwningNode().contains(runningTasks[task].task.task.outputPartition)) {
      assert(plan->getDataUnit(runningTasks[task].task.task.outputPartition.dataUnitId).partitionLayout->type ==
             PartitionLayoutType::Broadcast);
      partitions.cachePartition(runningTasks[task].task.task.outputPartition, node);
    } else {
      partitions.registerPartition(runningTasks[task].task.task.outputPartition, node);
    }
  }
  for (size_t transfer : finishingTransfers) {
    auto targetNode = runningTransfers[transfer].transfer.to;
    auto partition = runningTransfers[transfer].transfer.partition;

    if (settings.allowStreamingInput) {
      assert(partialPartitions.contains(targetNode));
      assert(partialPartitions.find(targetNode)->second.contains(partition));
      assert(partialPartitions.find(targetNode)->second.find(partition)->second >= 1 - progressTolerance);
      partialPartitions[targetNode].erase(partition);
    }

    partitions.cachePartition(partition, targetNode);
  }
  for (size_t shuffle : finishingShuffles) {
    finishShuffle(runningShuffles[shuffle]);
  }

  // remove finished tasks from running tasks
  filter(runningTasks, finishingTasks);
  filter(runningTransfers, finishingTransfers);
  filter(runningShuffles, finishingShuffles);

  for (auto& diskTime : diskTimes) diskTime = std::max(0.0, diskTime - stepTime);

  runtime += stepTime;
  // checking for finished execution
  size_t finishingJobs =
      finishingShuffles.size() + finishingTasks.size() + finishingLatencies + finishingTransfers.size();
  finishStep(allowEmptyTasks, finishingJobs);
}
//----------------------------------------------------------------------------------------------------------------------
void SimulatorImpl::finishStep(bool allowEmptyTasks, size_t finishingJobs) {
  if (pendingTasks.size() + pendingTransfers.size() + pendingShuffles.size() + runningTasks.size() +
          runningTransfers.size() + runningShuffles.size() ==
      0) {
    auto resultDU = plan->getDataUnit(plan->getResultDataUnit());
    assert(!resultNode.has_value() || !resultDU.partitionLayout->isPartitioned());
    if (!resultNode.has_value() || partitions.hasPartition(*resultNode, Partition{resultDU.id, 0})) {
      done = true;
    } else if (!(allowEmptyTasks && finishingJobs != 0)) {
      aborted = true;
      abortMessage = std::format("All tasks and transfers have finished but the result is not present on node {}",
                                 static_cast<size_t>(*resultNode));
      return;
    }
  }
  ++nSteps;
}
//----------------------------------------------------------------------------------------------------------------------
std::vector<double> SimulatorImpl::taskRuntimes() {
  // for each node compute the number of tasks that are executed
  std::vector<size_t> nodeTasks;
  nodeTasks.resize(cluster->nodes.size(), 0);
  for (const auto& task : runningTasks) {
    if (task.waitingForInput) continue;
    nodeTasks[task.task.node] += 1;
  }
  std::vector<double> result;
  // for each task compute the remaining time
  result.reserve(runningTasks.size());
  for (const auto& task : runningTasks) {
    std::optional<double> slowdownFactor = std::nullopt;
    if (settings.taskSlowdownFactors)
      slowdownFactor =
          (*settings.taskSlowdownFactors)[task.task.task.pipeline][task.task.task.scannedPartition.partitionIndex];
    if (settings.partitionSkewFactors) {
      if (!slowdownFactor) slowdownFactor = 1;
      *slowdownFactor *= *getSkewFactor(task.task.task.scannedPartition);
    }
    if (task.waitingForInput)
      result.push_back(std::numeric_limits<double>::infinity());
    else {
      double availableCores = static_cast<double>(cluster->nodes[task.task.node].nCores) / nodeTasks[task.task.node];
      result.push_back(executionTime(task.task.task, availableCores, slowdownFactor, settings.nLinearScaling,
                                     settings.discountFactor) *
                       (task.progressLimit - task.progress));
    }
  }
  return result;
}
//----------------------------------------------------------------------------------------------------------------------
double SimulatorImpl::executionTime(const Task& task, double nCores, std::optional<double> slowdownFactor,
                                    std::optional<std::size_t> nLinearScaling, std::optional<double> discountFactor) {
  return plan->estimateRuntime(nCores, task.pipeline, slowdownFactor, nLinearScaling, discountFactor);
}
//----------------------------------------------------------------------------------------------------------------------
std::pair<std::vector<double>, std::vector<double>> SimulatorImpl::transferRuntimes() {
  std::vector<NetworkSimulator::Connection> connections;
  // Exclude transfers and shuffles that are still waiting for network latency
  std::vector<size_t> activeTransferIndex;
  activeTransferIndex.reserve(runningTransfers.size());
  std::vector<double> transferTimes;
  transferTimes.reserve(runningTransfers.size());
  connections.reserve(runningTransfers.size() + runningShuffles.size());
  for (size_t i = 0; i < runningTransfers.size(); ++i) {
    const auto& transfer = runningTransfers[i];
    if (transfer.waitingForInput) {
      transferTimes.push_back(std::numeric_limits<double>::infinity());
    } else if (transfer.latencyTime == 0) {
      connections.emplace_back(NetworkSimulator::Connection{transfer.transfer.from, transfer.transfer.to});
      activeTransferIndex.push_back(i);
      transferTimes.push_back(0);
    } else {
      transferTimes.push_back(transfer.latencyTime);
    }
  }
  std::vector<size_t> activeShuffleIndex;
  activeShuffleIndex.reserve(runningShuffles.size());
  std::vector<double> shuffleTimes;
  shuffleTimes.reserve(runningShuffles.size());
  for (size_t i = 0; i < runningShuffles.size(); ++i) {
    const auto& shuffle = runningShuffles[i];
    if (shuffleWaitingForInput(shuffle, settings.minimumBlockSize)) {
      shuffleTimes.push_back(std::numeric_limits<double>::infinity());
    } else if (shuffle.latencyTime == 0) {
      connections.emplace_back(NetworkSimulator::Connection{shuffle.from, shuffle.to});
      activeShuffleIndex.push_back(i);
      shuffleTimes.push_back(0);
    } else {
      shuffleTimes.push_back(shuffle.latencyTime);
    }
  }

  assert(activeTransferIndex.size() + activeShuffleIndex.size() == connections.size());
  assert(transferTimes.size() == runningTransfers.size());
  assert(shuffleTimes.size() == runningShuffles.size());

  std::vector speeds = networkSimulator.computeTransferSpeeds(connections);

  for (size_t i = 0; i < activeTransferIndex.size(); ++i) {
    size_t resultIndex = activeTransferIndex[i];
    const RunningTransfer& transfer = runningTransfers[activeTransferIndex[i]];
    double transferTime =
        partitionSize(transfer.transfer.partition) / speeds[i] * (transfer.progressLimit - transfer.progress);
    assert(!transfer.waitingForInput);
    assert(transferTime >= 0);
    if (transferTime == 0) assert(partitionSize(runningTransfers[activeTransferIndex[i]].transfer.partition) == 0);
    transferTimes[resultIndex] = transferTime;
  }
  for (size_t j = 0; j < activeShuffleIndex.size(); ++j) {
    size_t resultIndex = activeShuffleIndex[j];
    const RunningShuffle& shuffle = runningShuffles[activeShuffleIndex[j]];
    double speed = speeds[activeTransferIndex.size() + j];
    double shuffleTime = bytesTransferred(shuffle) / speed * (shuffle.progressLimit() - shuffle.progress);
    assert(!shuffleWaitingForInput(shuffle, settings.minimumBlockSize));
    assert(shuffleTime >= 0);
    // if (shuffleTime == 0) assert(partitionSize(shuffle.targetPartition) == 0);
    shuffleTimes[resultIndex] = shuffleTime;
  }

  return {transferTimes, shuffleTimes};
}
//----------------------------------------------------------------------------------------------------------------------
double SimulatorImpl::partitionSize(const Partition& partition) const {
  return plan->dataUnits[partition.dataUnitId].partitionSize(getSkewFactor(partition));
}
//----------------------------------------------------------------------------------------------------------------------
void SimulatorImpl::updatePartialPartition(Partition partition, NodeId node, double delta) {
  // Default initialization will make sure that partial partitions start at 0
  if (!settings.allowStreamingInput) return;
  if (!firstPartialPartitionNodes.contains(partition)) firstPartialPartitionNodes[partition] = node;
  partialPartitions[node][partition] += delta;
}
//----------------------------------------------------------------------------------------------------------------------
void SimulatorImpl::startShuffle(ShuffleStage stage, NodeId sourceNode, Partition sourcePartition,
                                 double progressLimit) {
  auto nOutPartitions = plan->dataUnits[stage.to].partitionLayout->nPartitions;
  assert(nOutPartitions);
  std::unordered_map<NodeId, std::vector<Partition>> nodeTargets;
  for (size_t i = 0; i < nOutPartitions; ++i) {
    Partition targetPartition{stage.to, i};
    assert(shuffleTargets.contains(targetPartition));
    for (NodeId targetNode : shuffleTargets[targetPartition]) {
      // if collected shuffle does not exist for partition, create it
      LocatedPartition locatedTargetPartition{targetPartition, targetNode};
      if (!collectedShuffles.contains(locatedTargetPartition)) {
        collectedShuffles[locatedTargetPartition] = ShuffleOutPartition{targetPartition, targetNode};
        memorySimulator.storePartition(targetNode, targetPartition, getSkewFactor(targetPartition));
      }
      if (sourceNode == targetNode && progressLimit == 1) {
        RunningShuffle localShuffle{{sourcePartition}, {targetPartition}, stage, sourceNode, targetNode, 0, 1, {1}};
        updatePartialPartition(targetPartition, targetNode, inputFraction(localShuffle));
        finishShuffle(localShuffle);
        ++instantlyFinishedShuffles;
      } else {
        nodeTargets[targetNode].push_back(targetPartition);
        if (sourceNode == targetNode) {
          auto nInPartitions = plan->dataUnits[stage.from].partitionLayout->nPartitions;
          double inputFraction = 1.0 / nInPartitions;
          updatePartialPartition(targetPartition, targetNode, progressLimit * inputFraction);
        }
      }
    }
  }

  for (const auto& [targetNode, targetPartitions] : nodeTargets) {
    if (sourceNode == targetNode) {
      startShuffle(stage, sourceNode, targetNode, sourcePartition, targetPartitions, progressLimit, progressLimit);
    } else {
      startShuffle(stage, sourceNode, targetNode, sourcePartition, targetPartitions, progressLimit, 0);
    }
  }
}
//----------------------------------------------------------------------------------------------------------------------
void SimulatorImpl::startShuffle(ShuffleStage stage, NodeId sourceNode, NodeId targetNode, Partition sourcePartition,
                                 std::vector<Partition> targetPartitions, double progressLimit, double progress) {
  RunningShuffle* existingShuffle = nullptr;
  for (auto& s : runningShuffles) {
    if (s.from == sourceNode && s.to == targetNode && s.stage == stage) {
      existingShuffle = &s;
      break;
    }
  }

  std::sort(targetPartitions.begin(), targetPartitions.end());
  if (existingShuffle) {
    // update the shuffle
    assert(existingShuffle->outPartitions == targetPartitions);

    double newProgress = existingShuffle->progress * existingShuffle->inPartitions.size();
    newProgress += progress;
    newProgress /= existingShuffle->inPartitions.size() + 1;
    existingShuffle->progress = newProgress;
    existingShuffle->inPartitions.push_back(sourcePartition);
    existingShuffle->progressLimits.push_back(progressLimit);
  } else {
    // create a new shuffle
    double latency = std::max(cluster->nodes[sourceNode].networkLatency, cluster->nodes[targetNode].networkLatency);
    RunningShuffle newShuffle{
        {sourcePartition}, std::move(targetPartitions), stage, sourceNode, targetNode, latency, progress,
        {progressLimit}};
    runningShuffles.push_back(newShuffle);
  }
}
//----------------------------------------------------------------------------------------------------------------------
double SimulatorImpl::partiallyAvailableFraction(Partition partition, NodeId node) const {
  assert(partialPartitions.contains(node));
  auto nodePartPartitions = partialPartitions.find(node)->second;
  if (!nodePartPartitions.contains(partition)) return 0;
  return nodePartPartitions.find(partition)->second;
}
//----------------------------------------------------------------------------------------------------------------------
double SimulatorImpl::partiallyAvailableBytes(Partition partition, NodeId node) const {
  return partitionSize(partition) * partiallyAvailableFraction(partition, node);
}
//----------------------------------------------------------------------------------------------------------------------
std::pair<bool, double> SimulatorImpl::checkPartiallyReady(const TaskAssignment::LocatedTask& task,
                                                           double previousProgress) const {
  for (Partition partition : task.task.requiredPartitions) {
    if (!partitions.hasPartition(task.node, partition)) return {false, 0};
  }
  auto node = task.node;
  auto partition = task.task.scannedPartition;
  double previousAvailableBytes = previousProgress * partitionSize(partition);
  return {partiallyAvailableBytes(partition, node) - previousAvailableBytes > settings.minimumBlockSize,
          partiallyAvailableFraction(partition, node)};
}
//----------------------------------------------------------------------------------------------------------------------
std::pair<bool, double> SimulatorImpl::checkPartiallyReady(const TaskAssignment::Transfer& transfer,
                                                           double previousProgress) const {
  auto node = transfer.from;
  auto partition = transfer.partition;
  double previousAvailableBytes = previousProgress * partitionSize(partition);
  return {partiallyAvailableBytes(partition, node) - previousAvailableBytes > settings.minimumBlockSize,
          partiallyAvailableFraction(partition, node)};
}
//----------------------------------------------------------------------------------------------------------------------
std::optional<double> SimulatorImpl::getSkewFactor(const Partition& partition) const {
  if (settings.partitionSkewFactors)
    return (*settings.partitionSkewFactors)[partition.dataUnitId][partition.partitionIndex];
  else
    return {};
}
//----------------------------------------------------------------------------------------------------------------------
double SimulatorImpl::RunningShuffle::progressLimit() const {
  assert(progressLimits.size() == inPartitions.size());
  double sum = 0;
  for (double limit : progressLimits) {
    sum += limit;
  }
  return sum / static_cast<double>(progressLimits.size());
}
//----------------------------------------------------------------------------------------------------------------------
double SimulatorImpl::inputFraction(const RunningShuffle& shuffle) const {
  double inputData = 0;
  for (const auto& p : shuffle.inPartitions) {
    inputData += partitionSize(p);
  }
  const auto& fromDU = plan->getDataUnit(shuffle.stage.from);
  double totalInputData = fromDU.tupleSize() * fromDU.estimatedCard;
  if (totalInputData == 0) {
    assert(inputData == 0);
    const auto nInputPartitions = fromDU.partitionLayout->nPartitions;
    assert(nInputPartitions > 0);
    return static_cast<double>(shuffle.inPartitions.size()) / static_cast<double>(nInputPartitions);
  }
  return inputData / totalInputData;
}
//----------------------------------------------------------------------------------------------------------------------
bool SimulatorImpl::shuffleWaitingForInput(const RunningShuffle& shuffle, double minimumBlockSize) const {
  double transferredBytes = bytesTransferred(shuffle);
  double limit = shuffle.progressLimit();
  if (limit >= 1.0 - progressTolerance) return false;
  double newBytes = transferredBytes * (limit - shuffle.progress);
  return newBytes < minimumBlockSize;
}
//----------------------------------------------------------------------------------------------------------------------
double SimulatorImpl::bytesTransferred(const RunningShuffle& shuffle) const {
  const auto& toDU = plan->getDataUnit(shuffle.stage.to);
  // double outputSize = toDU.tupleSize() * toDU.estimatedCard;
  double outputSize = 0;
  for (const auto& p : shuffle.outPartitions) {
    outputSize += partitionSize(p);
  };
  return inputFraction(shuffle) * outputSize;
}
//----------------------------------------------------------------------------------------------------------------------
double SimulatorImpl::getDiskWritingTime(double bytes) const {
  // We assume a 560 MB/s SATA drive
  return bytes / (1024.0 * 1024.0 * 560.0);
}
//----------------------------------------------------------------------------------------------------------------------
void SimulatorImpl::waitForDisk() {
  double maxTime =
      std::reduce(diskTimes.begin(), diskTimes.end(), 0.0, [](double a, double b) { return std::max(a, b); });
  runtime += maxTime;
  if (!pendingDiskWrites.empty()) {
    aborted = true;
    abortMessage = "Execution finished but disk writes are still pending";
  }
}
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

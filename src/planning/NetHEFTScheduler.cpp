//----------------------------------------------------------------------------------------------------------------------
#include "src/planning/NetHEFTScheduler.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <tuple>
#include <utility>
#include <variant>

#include "src/Cluster.hpp"
#include "src/CustomTypes.hpp"
#include "src/distributedplan/Pipeline.hpp"
#include "src/partitioning/PartitionLayout.hpp"
#include "src/planning/AssignmentPlannerState.hpp"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2025 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace {
//----------------------------------------------------------------------------------------------------------------------
using namespace dqsim;
//----------------------------------------------------------------------------------------------------------------------
template <class... Ts>
struct Overloaded : Ts... {
  using Ts::operator()...;
};
//----------------------------------------------------------------------------------------------------------------------
}  // namespace
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
double NetHEFTScheduler::availableTime(const NetHEFTScheduler::PartitionLoad& load) {
  return std::visit(Overloaded{
                        [](const NetHEFTScheduler::Transfer& t) { return t.schedule.finishTime; },
                        [](NetHEFTScheduler::PartitionShuffle s) { return s.downloadSchedule.finishTime; },
                        [](NetHEFTScheduler::NoTransfer n) { return n.availableTime; },
                    },
                    load);
}
//----------------------------------------------------------------------------------------------------------------------
NodeId NetHEFTScheduler::getAssignment(const Task& t) const {
  assert(pipelineAssignments.contains(t.pipeline));
  assert(pipelineAssignments.find(t.pipeline)->second.size() > t.scannedPartition.partitionIndex);
  return pipelineAssignments.find(t.pipeline)->second[t.scannedPartition.partitionIndex];
}
//----------------------------------------------------------------------------------------------------------------------
std::pair<uint64_t, double> NetHEFTScheduler::earliestStartSlot(const NetHEFTScheduler::ResourceSchedule& schedule,
                                                                double lowerBound, double duration,
                                                                uint64_t lowerBoundSlot) {
  assert(lowerBoundSlot <= schedule.size());
  uint64_t current = lowerBoundSlot;
  double prevFinish = (current > 0) ? schedule[current - 1].finishTime : 0;
  // First advance to lower bound
  for (; current < schedule.size(); ++current) {
    assert(current < schedule.size());
    if (schedule[current].startTime >= lowerBound) break;
    prevFinish = schedule[current].finishTime;
  }
  prevFinish = std::max(prevFinish, lowerBound);
  // Then find slot that is large enough
  for (; current < schedule.size(); ++current) {
    if (schedule[current].startTime - prevFinish >= duration) break;
    prevFinish = schedule[current].finishTime;
  }
  double startTime = std::max(prevFinish, lowerBound);
  return {current, startTime};
}
//----------------------------------------------------------------------------------------------------------------------
std::tuple<uint64_t, uint64_t, double> NetHEFTScheduler::earliestStartSlot(
    const NetHEFTScheduler::ResourceSchedule& schedA, const NetHEFTScheduler::ResourceSchedule& schedB,
    double lowerBound, double duration) {
  uint64_t iA = 0;
  uint64_t iB = 0;
  double startA = 0;
  double startB = 0;
  do {
    std::tie(iA, startA) = earliestStartSlot(schedA, lowerBound, duration, iA);
    std::tie(iB, startB) = earliestStartSlot(schedB, lowerBound, duration, iB);
    lowerBound = std::max(startA, startB);
  } while (startA != startB);
  return {iA, iB, startA};
}
//----------------------------------------------------------------------------------------------------------------------
std::tuple<uint64_t, uint64_t, uint64_t, uint64_t, double> NetHEFTScheduler::earliestStartSlot(
    const NetHEFTScheduler::ResourceSchedule& schedA, const NetHEFTScheduler::ResourceSchedule& schedB,
    const NetHEFTScheduler::ResourceSchedule& schedC, const NetHEFTScheduler::ResourceSchedule& schedD,
    double lowerBound, double duration) {
  uint64_t iA = 0;
  uint64_t iB = 0;
  uint64_t iC = 0;
  uint64_t iD = 0;
  double startA = 0;
  double startB = 0;
  double startC = 0;
  double startD = 0;
  do {
    std::tie(iA, startA) = earliestStartSlot(schedA, lowerBound, duration, iA);
    std::tie(iB, startB) = earliestStartSlot(schedB, lowerBound, duration, iB);
    std::tie(iC, startC) = earliestStartSlot(schedC, lowerBound, duration, iC);
    std::tie(iD, startD) = earliestStartSlot(schedD, lowerBound, duration, iD);
    lowerBound = std::max(std::max(startA, startB), std::max(startC, startD));
  } while (startA != startB || startA != startC || startA != startD);
  return {iA, iB, iC, iD, startA};
}
//----------------------------------------------------------------------------------------------------------------------
NetHEFTScheduler::NetHEFTScheduler(const AssignmentProblem& problem)
    : problem(problem),
      nodeSchedules(problem.cluster.getComputeNodes().size()),
      partitionManager(PartitionManager::loadPartitioning(problem.partitioning)) {
  for (const auto& [p, n] : partitionManager.getOwningNode()) {
    partitionTimes[p].push_back({n, 0});
  }
  for (const auto& [p, nodes] : partitionManager.getCachingNodes()) {
    for (const auto& n : nodes) {
      partitionTimes[p].push_back({n, 0});
    }
  }
  // make base relations available at time 0
  for (const auto& s : problem.plan.shuffleStages) {
    if (!problem.plan.getDataUnit(s.from).isIntermediate) {
      shuffleInputCollectedTimes[s.from] = 0;
    }
  }
  // resize assignments
  for (const auto& [p, tasks] : problem.tasks) {
    pipelineAssignments[p].resize(tasks.size());
  }
}
//----------------------------------------------------------------------------------------------------------------------
NetHEFTScheduler::PartitionLoad NetHEFTScheduler::availableTime(Partition p, NodeId n) const {
  assert(n < problem.cluster.getComputeNodes().size());
  if (partitionTimes.contains(p)) {
    for (const auto& [node, time] : partitionTimes.find(p)->second) {
      if (node == n) return NoTransfer{time};
    }
  }

  double partitionSize = problem.plan.getDataUnit(p.dataUnitId).partitionSize({});

  // if partition comes from shuffled DU, we can re-shuffle
  if (problem.plan.isShuffleOutput(p.dataUnitId)) {
    DataUnitId shuffleInputDU{0};
    [[maybe_unused]] bool found = false;
    for (const auto& s : problem.plan.shuffleStages) {
      if (s.to == p.dataUnitId) {
        shuffleInputDU = s.from;
        found = true;
      }
    }
    assert(found);
    assert(shuffleInputCollectedTimes.contains(shuffleInputDU));
    double startTimeLowerBound = shuffleInputCollectedTimes.find(shuffleInputDU)->second;
    const Node& node = problem.cluster.nodes[n];
    double downloadDuration = partitionSize / node.byteBandwidth();
    auto& schedule = nodeSchedules[n].download;
    auto [iSlot, startTime] = earliestStartSlot(schedule, startTimeLowerBound + node.networkLatency, downloadDuration);
    double finishTime = startTime + downloadDuration;
    assert(iSlot == schedule.size() || schedule[iSlot].startTime >= finishTime);
    return PartitionShuffle{n, iSlot, {startTime, finishTime}, p};
  }
  assert(partitionTimes.contains(p));

  Transfer best{n, n, 0, 0, {std::numeric_limits<double>::max(), std::numeric_limits<double>::max()}, p};
  for (const auto& [node, time] : partitionTimes.find(p)->second) {
    assert(node < nodeSchedules.size() || (problem.cluster.storageService && problem.cluster.nodes.size() == node + 1));
    auto current = Transfer{node, n, 0, 0, {time, std::numeric_limits<double>::max()}, p};
    if (node == n) {
      current.schedule.finishTime = time;
    } else if (!problem.cluster.isComputeNode(node)) {
      current = transferScheduleFromStorage(node, n, time, partitionSize, p);
    } else {
      current = transferSchedule(node, n, time, partitionSize, p);
    }
    if (current.schedule.finishTime < best.schedule.finishTime) {
      best = current;
    }
  }
  return best;
}
//----------------------------------------------------------------------------------------------------------------------
NetHEFTScheduler::PartitionLoad NetHEFTScheduler::availableTime(Partition p, NodeId n,
                                                                const NetHEFTScheduler::Delta& delta) const {
  assert(n < problem.cluster.getComputeNodes().size());
  if (partitionTimes.contains(p)) {
    for (const auto& [node, time] : partitionTimes.find(p)->second) {
      if (node == n) return NoTransfer{time};
    }
  }
  if (delta.partitionTimes.contains(p)) {
    for (const auto& [node, time] : delta.partitionTimes.find(p)->second) {
      if (node == n) return NoTransfer{time};
    }
  }

  double partitionSize = problem.plan.getDataUnit(p.dataUnitId).partitionSize({});

  // if partition comes from shuffled DU, we can re-shuffle
  if (problem.plan.isShuffleOutput(p.dataUnitId)) {
    DataUnitId shuffleInputDU{0};
    [[maybe_unused]] bool found = false;
    for (const auto& s : problem.plan.shuffleStages) {
      if (s.to == p.dataUnitId) {
        shuffleInputDU = s.from;
        found = true;
      }
    }
    assert(found);
    assert(shuffleInputCollectedTimes.contains(shuffleInputDU) ||
           delta.shuffleInputCollectedTimes.contains(shuffleInputDU));
    double startTimeLowerBound = shuffleInputCollectedTimes.contains(shuffleInputDU)
                                     ? shuffleInputCollectedTimes.find(shuffleInputDU)->second
                                     : delta.shuffleInputCollectedTimes.find(shuffleInputDU)->second;
    const Node& node = problem.cluster.nodes[n];
    double downloadDuration = partitionSize / node.byteBandwidth();
    auto& s1 = nodeSchedules[n].download;
    auto& s2 = delta.nodeSchedules[n].download;
    auto [iSlot1, iSlot2, startTime] =
        earliestStartSlot(s1, s2, startTimeLowerBound + node.networkLatency, downloadDuration);
    double finishTime = startTime + downloadDuration;
    assert(iSlot1 == s1.size() || s1[iSlot1].startTime >= finishTime);
    assert(iSlot2 == s2.size() || s2[iSlot2].startTime >= finishTime);
    return PartitionShuffle{n, iSlot2, {startTime, finishTime}, p};
  }
  assert(partitionTimes.contains(p) || delta.partitionTimes.contains(p));

  Transfer best{n, n, 0, 0, {std::numeric_limits<double>::max(), std::numeric_limits<double>::max()}, p};
  const auto& times =
      partitionTimes.contains(p) ? partitionTimes.find(p)->second : delta.partitionTimes.find(p)->second;
  for (const auto& [node, time] : times) {
    assert(node < nodeSchedules.size() || (problem.cluster.storageService && problem.cluster.nodes.size() == node + 1));
    auto current = Transfer{node, n, 0, 0, {time, std::numeric_limits<double>::max()}, p};
    if (node == n) {
      current.schedule.finishTime = time;
    } else if (!problem.cluster.isComputeNode(node)) {
      current = transferScheduleFromStorage(node, n, time, partitionSize, p, delta);
    } else {
      current = transferSchedule(node, n, time, partitionSize, p, delta);
    }
    if (current.schedule.finishTime < best.schedule.finishTime) {
      best = current;
    }
  }
  return best;
}
//----------------------------------------------------------------------------------------------------------------------
NetHEFTScheduler::Transfer NetHEFTScheduler::transferSchedule(NodeId n1, NodeId n2, double lowerBound,
                                                              double bytesTransferred, Partition p) const {
  // from n1 to n2
  double duration =
      bytesTransferred / std::min(problem.cluster.nodes[n1].byteBandwidth(), problem.cluster.nodes[n2].byteBandwidth());
  lowerBound += std::max(problem.cluster.nodes[n1].networkLatency, problem.cluster.nodes[n2].networkLatency);
  assert(n1 < nodeSchedules.size());
  assert(n2 < nodeSchedules.size());
  auto [s1, s2, time] = earliestStartSlot(nodeSchedules[n1].upload, nodeSchedules[n2].download, lowerBound, duration);
  return {n1, n2, s1, s2, {time, time + duration}, p};
}
//----------------------------------------------------------------------------------------------------------------------
NetHEFTScheduler::Transfer NetHEFTScheduler::transferSchedule(NodeId n1, NodeId n2, double lowerBound,
                                                              double bytesTransferred, Partition p,
                                                              const Delta& delta) const {
  // from n1 to n2
  double duration =
      bytesTransferred / std::min(problem.cluster.nodes[n1].byteBandwidth(), problem.cluster.nodes[n2].byteBandwidth());
  lowerBound += std::max(problem.cluster.nodes[n1].networkLatency, problem.cluster.nodes[n2].networkLatency);
  assert(n1 < nodeSchedules.size());
  assert(n2 < nodeSchedules.size());
  assert(n1 < delta.nodeSchedules.size());
  assert(n2 < delta.nodeSchedules.size());
  auto [s1, s2, s3, s4, time] =
      earliestStartSlot(nodeSchedules[n1].upload, nodeSchedules[n2].download, delta.nodeSchedules[n1].upload,
                        delta.nodeSchedules[n2].download, lowerBound, duration);
  return {n1, n2, s3, s4, {time, time + duration}, p};
}
//----------------------------------------------------------------------------------------------------------------------
NetHEFTScheduler::Transfer NetHEFTScheduler::transferScheduleFromStorage(NodeId n1, NodeId n2, double lowerBound,
                                                                         double bytesTransferred, Partition p) const {
  // from n1 to n2
  double duration =
      bytesTransferred / std::min(problem.cluster.nodes[n1].byteBandwidth(), problem.cluster.nodes[n2].byteBandwidth());
  lowerBound += std::max(problem.cluster.nodes[n1].networkLatency, problem.cluster.nodes[n2].networkLatency);
  assert(n1 == NodeId{problem.cluster.getComputeNodes().size()});
  assert(n2 < nodeSchedules.size());
  auto [schedule, time] = earliestStartSlot(nodeSchedules[n2].download, lowerBound, duration);
  return {n1, n2, 0, schedule, {time, time + duration}, p};
}
//----------------------------------------------------------------------------------------------------------------------
NetHEFTScheduler::Transfer NetHEFTScheduler::transferScheduleFromStorage(NodeId n1, NodeId n2, double lowerBound,
                                                                         double bytesTransferred, Partition p,
                                                                         const Delta& delta) const {
  // from n1 to n2
  double duration =
      bytesTransferred / std::min(problem.cluster.nodes[n1].byteBandwidth(), problem.cluster.nodes[n2].byteBandwidth());
  lowerBound += std::max(problem.cluster.nodes[n1].networkLatency, problem.cluster.nodes[n2].networkLatency);
  assert(n1 == NodeId{problem.cluster.getComputeNodes().size()});
  assert(n2 < nodeSchedules.size());
  assert(n2 < delta.nodeSchedules.size());
  auto [s1, s2, time] =
      earliestStartSlot(nodeSchedules[n2].download, delta.nodeSchedules[n2].download, lowerBound, duration);
  return {n1, n2, 0, s2, {time, time + duration}, p};
}
//----------------------------------------------------------------------------------------------------------------------
void NetHEFTScheduler::addPartitionLoad(NetHEFTScheduler::PartitionLoad load) {
  std::visit(Overloaded{
                 [&](NetHEFTScheduler::Transfer t) { addTransfer(std::move(t)); },
                 [&](NetHEFTScheduler::PartitionShuffle s) { addPartitionShuffle(s); },
                 [&](NetHEFTScheduler::NoTransfer) {},
             },
             load);
}
//----------------------------------------------------------------------------------------------------------------------
void NetHEFTScheduler::Delta::addPartitionLoad(NetHEFTScheduler::PartitionLoad load,
                                               const AssignmentProblem& problem_) {
  std::visit(Overloaded{
                 [&](NetHEFTScheduler::Transfer t) { addTransfer(std::move(t), problem_); },
                 [&](NetHEFTScheduler::PartitionShuffle s) { addPartitionShuffle(s); },
                 [&](NetHEFTScheduler::NoTransfer) {},
             },
             load);
}
//----------------------------------------------------------------------------------------------------------------------
void NetHEFTScheduler::insertSlot(NetHEFTScheduler::ResourceSchedule& schedule, NetHEFTScheduler::ResourceAlloc slot,
                                  uint64_t slotIndex) {
  assert(slotIndex <= schedule.size());
  assert(slotIndex == 0 || schedule[slotIndex - 1].finishTime <= slot.startTime);
  assert(slotIndex == schedule.size() || schedule[slotIndex].startTime >= slot.finishTime);
  if (slotIndex > 0 && schedule[slotIndex - 1].finishTime == slot.startTime) {
    // merge with previous slot if possible
    schedule[slotIndex - 1].finishTime = slot.finishTime;
  } else {
    schedule.insert(schedule.begin() + slotIndex, slot);
  }
}
//----------------------------------------------------------------------------------------------------------------------
void NetHEFTScheduler::addTransfer(NetHEFTScheduler::Transfer transfer) {
  if (transfer.from == transfer.to) return;
  if (problem.cluster.isComputeNode(transfer.from)) {
    auto& s1 = nodeSchedules[transfer.from].upload;
    insertSlot(s1, transfer.schedule, transfer.fromSlot);
  }
  auto& s2 = nodeSchedules[transfer.to].download;
  insertSlot(s2, transfer.schedule, transfer.toSlot);
  transferAssignments.emplace_back(transfer.partition, transfer.from, transfer.to);
}
//----------------------------------------------------------------------------------------------------------------------
void NetHEFTScheduler::Delta::addTransfer(NetHEFTScheduler::Transfer transfer, const AssignmentProblem& problem_) {
  if (transfer.from == transfer.to) return;
  if (problem_.cluster.isComputeNode(transfer.from)) {
    auto& s1 = nodeSchedules[transfer.from].upload;
    insertSlot(s1, transfer.schedule, transfer.fromSlot);
  }
  auto& s2 = nodeSchedules[transfer.to].download;
  insertSlot(s2, transfer.schedule, transfer.toSlot);
}
//----------------------------------------------------------------------------------------------------------------------
void NetHEFTScheduler::addPartitionShuffle(NetHEFTScheduler::PartitionShuffle shuffle) {
  auto& s = nodeSchedules[shuffle.to].download;
  insertSlot(s, shuffle.downloadSchedule, shuffle.toSlot);
  shuffleTargetAssignments.emplace_back(shuffle.partition, shuffle.to);
}
//----------------------------------------------------------------------------------------------------------------------
void NetHEFTScheduler::Delta::addPartitionShuffle(NetHEFTScheduler::PartitionShuffle shuffle) {
  auto& s = nodeSchedules[shuffle.to].download;
  insertSlot(s, shuffle.downloadSchedule, shuffle.toSlot);
}
//----------------------------------------------------------------------------------------------------------------------
double NetHEFTScheduler::addExecution(NodeId node, const Task& t, double earliestStartTime) {
  assert(node < problem.cluster.getComputeNodes().size());
  double executionTime = problem.plan.estimateRuntime(problem.cluster.nodes[node].nCores, t.pipeline, {}, {}, {});
  auto [iExSlot, exStartTime] = earliestStartSlot(nodeSchedules[node].cpu, earliestStartTime, executionTime);
  ResourceAlloc exSlot{exStartTime, exStartTime + executionTime};
  insertSlot(nodeSchedules[node].cpu, exSlot, iExSlot);
  // make result partition available
  partitionTimes[t.outputPartition].push_back({node, exSlot.finishTime});
  return exSlot.finishTime;
}
//----------------------------------------------------------------------------------------------------------------------
double NetHEFTScheduler::addExecutionToDelta(NodeId node, const Task& t, double earliestStartTime,
                                             NetHEFTScheduler::Delta& delta) const {
  assert(node < problem.cluster.getComputeNodes().size());
  double executionTime = problem.plan.estimateRuntime(problem.cluster.nodes[node].nCores, t.pipeline, {}, {}, {});
  auto [iExSlot1, iExSlot2, exStartTime] =
      earliestStartSlot(nodeSchedules[node].cpu, delta.nodeSchedules[node].cpu, earliestStartTime, executionTime);
  ResourceAlloc exSlot{exStartTime, exStartTime + executionTime};
  insertSlot(delta.nodeSchedules[node].cpu, exSlot, iExSlot2);
  // make result partition available
  delta.partitionTimes[t.outputPartition].push_back({node, exSlot.finishTime});
  return exSlot.finishTime;
}
//----------------------------------------------------------------------------------------------------------------------
double NetHEFTScheduler::addLoads(NodeId node, const Task& t) {
  assert(node < problem.cluster.getComputeNodes().size());
  auto load = availableTime(t.scannedPartition, node);
  addPartitionLoad(load);
  partitionTimes[t.scannedPartition].push_back({node, availableTime(load)});
  double earliestStartTime = availableTime(load);
  for (const auto& r : t.requiredPartitions) {
    auto tr = availableTime(r, node);
    addPartitionLoad(tr);
    partitionTimes[r].push_back({node, availableTime(tr)});
    earliestStartTime = std::max(earliestStartTime, availableTime(tr));
  }
  return earliestStartTime;
}
//----------------------------------------------------------------------------------------------------------------------
double NetHEFTScheduler::addLoadsToDelta(NodeId node, const Task& t, NetHEFTScheduler::Delta& delta) const {
  assert(node < problem.cluster.getComputeNodes().size());
  auto load = availableTime(t.scannedPartition, node, delta);
  delta.addPartitionLoad(load, problem);
  delta.partitionTimes[t.scannedPartition].push_back({node, availableTime(load)});
  double earliestStartTime = availableTime(load);
  for (const auto& r : t.requiredPartitions) {
    auto tr = availableTime(r, node, delta);
    delta.addPartitionLoad(tr, problem);
    delta.partitionTimes[r].push_back({node, availableTime(tr)});
    earliestStartTime = std::max(earliestStartTime, availableTime(tr));
  }
  return earliestStartTime;
}
//----------------------------------------------------------------------------------------------------------------------
TaskAssignment NetHEFTScheduler::convertToAssignment(NetHEFTScheduler&& scheduler) {
  // collect assignment in TaskAssignment object
  AssignmentPlannerState result;
  result.result = std::make_unique<TaskAssignment>();
  result.problem = &scheduler.problem;
  result.result->transfers = std::move(scheduler.transferAssignments);
  result.result->shuffleTargets = std::move(scheduler.shuffleTargetAssignments);
  for (const auto& [p, nodes] : scheduler.pipelineAssignments) {
    const auto& taskList = scheduler.problem.tasks.find(p)->second;
    for (size_t i = 0; i < nodes.size(); ++i) {
      const auto& n = nodes[i];
      result.result->locatedTasks.push_back(TaskAssignment::LocatedTask{taskList[i], n});
    }
  }
  result.assignBroadcastTasks(true);
  result.trimShuffleTargets();
  return std::move(*result.result);
}
//----------------------------------------------------------------------------------------------------------------------
void NetHEFTScheduler::doShuffle(PipelineId p, double latestFinishTime) {
  const Pipeline& pipeline = problem.plan.getPipeline(p);
  // add shuffle finished times
  assert(problem.plan.isShuffleInput(pipeline.output));
  shuffleInputCollectedTimes[pipeline.output] = latestFinishTime;
  // block shuffle output times ?
  double outputPartitionSize = problem.plan.getDataUnit(pipeline.output).partitionSize({});
  [[maybe_unused]] bool isPipelineInput = false;
  {
    for (const auto& p2 : problem.plan.getPipelines()) {
      for (const auto& input : p2.requiredData) {
        if (input == pipeline.output) isPipelineInput = true;
      }
      if (p2.scan == pipeline.output) isPipelineInput = true;
      if (isPipelineInput) break;
    }
  }
  for (size_t i = 0; i < pipelineAssignments[p].size(); ++i) {
    const auto& n = pipelineAssignments[p][i];
    const Node& node = problem.cluster.nodes[n];
    assert(!partitionTimes[Partition(pipeline.output, i)].empty());
    assert(partitionTimes[Partition(pipeline.output, i)].size() == 1 || isPipelineInput);
    double lowerBound = partitionTimes[Partition(pipeline.output, i)].front().second;
    double uploadDuration = node.networkLatency + outputPartitionSize / node.byteBandwidth();
    auto [iUploadSlot, shuffleUploadStartTime] = earliestStartSlot(nodeSchedules[n].upload, lowerBound, uploadDuration);
    NetHEFTScheduler::ResourceAlloc uploadSlot{shuffleUploadStartTime, shuffleUploadStartTime + uploadDuration};
    insertSlot(nodeSchedules[n].upload, uploadSlot, iUploadSlot);
  }
}
//----------------------------------------------------------------------------------------------------------------------
void NetHEFTScheduler::doShuffleOnDelta(PipelineId p, double latestFinishTime, NetHEFTScheduler::Delta& delta) const {
  const Pipeline& pipeline = problem.plan.getPipeline(p);
  // add shuffle finished times
  assert(problem.plan.isShuffleInput(pipeline.output));
  delta.shuffleInputCollectedTimes[pipeline.output] = latestFinishTime;
  // block shuffle output times ?
  double outputPartitionSize = problem.plan.getDataUnit(pipeline.output).partitionSize({});
  assert(pipelineAssignments.contains(p) || delta.pipelineAssignments.contains(p));
  const auto& assignment =
      pipelineAssignments.contains(p) ? pipelineAssignments.find(p)->second : delta.pipelineAssignments.find(p)->second;
  for (size_t i = 0; i < assignment.size(); ++i) {
    const auto& n = assignment[i];
    const Node& node = problem.cluster.nodes[n];
    assert(delta.partitionTimes[Partition(pipeline.output, i)].size() == 1);
    double lowerBound = delta.partitionTimes[Partition(pipeline.output, i)].front().second;
    double uploadDuration = outputPartitionSize / node.byteBandwidth();
    lowerBound += node.networkLatency;
    auto [iUploadSlot1, iUploadSlot2, shuffleUploadStartTime] =
        earliestStartSlot(nodeSchedules[n].upload, delta.nodeSchedules[n].upload, lowerBound, uploadDuration);
    NetHEFTScheduler::ResourceAlloc uploadSlot{shuffleUploadStartTime, shuffleUploadStartTime + uploadDuration};
    insertSlot(delta.nodeSchedules[n].upload, uploadSlot, iUploadSlot2);
  }
}
//----------------------------------------------------------------------------------------------------------------------
void NetHEFTScheduler::placeResultPartition() {
  auto resultDU = problem.plan.getResultDataUnit();
  [[maybe_unused]] auto& res = problem.plan.getDataUnit(resultDU);
  assert(res.partitionLayout->nPartitions == 1);
  assert(res.partitionLayout->type == PartitionLayoutType::SingleNode);
  Partition resPartition{resultDU, 0};
  if (problem.plan.isShuffleOutput(resultDU)) {
    shuffleTargetAssignments.emplace_back(resPartition, NodeId{0});
  } else {
    assert(partitionTimes.contains(resPartition));
    auto tr = availableTime(resPartition, NodeId{0});
    addPartitionLoad(tr);
  }
}
//----------------------------------------------------------------------------------------------------------------------
void NetHEFTScheduler::runBroadcastPipeline(PipelineId p) {
  // this is a broadcast pipeline
  [[maybe_unused]] const Pipeline& pipeline = problem.plan.getPipeline(p);
  assert(problem.broadcastTasks.contains(p));
  assert(problem.plan.getDataUnit(pipeline.scan).partitionLayout->type == PartitionLayoutType::Broadcast);
  // perform pipeline anyway to fill state
  assert(problem.broadcastTasks.contains(p));
  const auto& t = problem.broadcastTasks.find(p)->second;
  for (NodeId n{0}; n < problem.cluster.getComputeNodes().size(); ++n) {
    double startTime = 0;
    auto load = availableTime(t.scannedPartition, n);
    startTime = std::max(startTime, availableTime(load));
    for (const auto& r : t.requiredPartitions) {
      auto loadr = availableTime(r, n);
      startTime = std::max(startTime, NetHEFTScheduler::availableTime(loadr));
    }
    // block load
    addLoads(n, t);
    // block execution time
    addExecution(n, t, startTime);
  }
}
//----------------------------------------------------------------------------------------------------------------------
double NetHEFTScheduler::finishTime(std::span<const Task> tasks, NodeId node, Delta& delta) const {
  double result = 0;
  for (const auto& t : tasks) {
    double start = addLoadsToDelta(node, t, delta);
    double currentFinish = addExecutionToDelta(node, t, start, delta);
    result = std::max(result, currentFinish);
  }
  return result;
}
//----------------------------------------------------------------------------------------------------------------------
void NetHEFTScheduler::Delta::clear() {
  for (auto& n : nodeSchedules) {
    n.download.clear();
    n.upload.clear();
    n.cpu.clear();
  }
  partitionTimes.clear();
  shuffleInputCollectedTimes.clear();
}
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

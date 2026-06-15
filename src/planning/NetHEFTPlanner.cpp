#include "src/planning/NetHEFTPlanner.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "src/CustomTypes.hpp"
#include "src/distributedplan/ProblemWriter.hpp"
#include "src/partitioning/PartitionLayout.hpp"
#include "src/planning/NetHEFTScheduler.hpp"
#include "src/tasks/Task.hpp"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2025 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
namespace {
//----------------------------------------------------------------------------------------------------------------------
void assignPipeline(NetHEFTScheduler& scheduler, PipelineId p) {
  const auto& problem = scheduler.problem;
  // We assume that all dependencies are already located
  auto pipeline = problem.plan.getPipeline(p);
  uint64_t nNodes = problem.cluster.getComputeNodes().size();
  const auto& taskList = problem.tasks.find(p)->second;
  uint64_t nTasks = taskList.size();

  // Order tasks by their possible gain from a good assignment
  std::vector<size_t> priority = std::ranges::to<std::vector>(std::views::iota(0uz, nTasks));
  NetHEFTScheduler::Delta delta(problem.cluster.getComputeNodes().size());

  {
    std::vector<double> optimalTaskFinishTimes(nTasks, std::numeric_limits<double>::max());
    std::vector<double> sumTaskFinishTimes(nTasks, 0);
    assert(problem.tasks.contains(p));
    for (NodeId n{0}; n < problem.cluster.getComputeNodes().size(); ++n) {
      for (size_t i = 0; i < taskList.size(); ++i) {
        const auto& t = taskList[i];
        delta.clear();
        auto currentFinishTime = scheduler.finishTime(std::span(&t, 1), n, delta);
        sumTaskFinishTimes[i] += currentFinishTime;
        optimalTaskFinishTimes[i] = std::min(optimalTaskFinishTimes[i], currentFinishTime);
      }
    }
    std::vector<double> weight;
    weight.reserve(nTasks);
    for (uint64_t i = 0; i < sumTaskFinishTimes.size(); ++i) {
      double avgTime = sumTaskFinishTimes[i] / nNodes;
      weight.push_back(avgTime - optimalTaskFinishTimes[i]);
    }
    std::ranges::sort(priority, [&](size_t a, size_t b) { return weight[a] > weight[b]; });
  }

  double latestFinishTime = 0;
  // assign each task to the node with the earliest finish time
  for (const auto& iTask : priority) {
    assert(iTask == taskList[iTask].scannedPartition.partitionIndex ||
           problem.plan.getDataUnit(taskList[iTask].scannedPartition.dataUnitId).partitionLayout->type ==
               PartitionLayoutType::Broadcast);
    // try out for all nodes
    NodeId bestNode;
    double bestTime = std::numeric_limits<double>::max();
    for (NodeId n{0}; n < problem.cluster.getComputeNodes().size(); ++n) {
      delta.clear();
      auto currentFinishTime = scheduler.finishTime(std::span<const Task>(&taskList[iTask], 1), n, delta);
      if (currentFinishTime < bestTime) {
        bestNode = n;
        bestTime = currentFinishTime;
      }
    }

    double earliestStartTime = scheduler.addLoads(bestNode, taskList[iTask]);
    latestFinishTime = std::max(latestFinishTime, scheduler.addExecution(bestNode, taskList[iTask], earliestStartTime));
    scheduler.pipelineAssignments[p][iTask] = bestNode;
  }

  // perform shuffle if output is shuffled
  if (problem.plan.isShuffleInput(pipeline.output)) scheduler.doShuffle(p, latestFinishTime);
}
//----------------------------------------------------------------------------------------------------------------------
}  // namespace
//----------------------------------------------------------------------------------------------------------------------
std::string NetHEFTPlanner::getName() const { return "NetHEFT"; }
//----------------------------------------------------------------------------------------------------------------------
TaskAssignment NetHEFTPlanner::assignTasks(const AssignmentProblem& problem) {
  NetHEFTScheduler scheduler(problem);
  // Assign pipelines in dependency order, assign shuffles if pipeline output will be shuffled
  for (PipelineId p : problem.plan.dependencyOrder) {
    if (problem.tasks.contains(p)) {
      assignPipeline(scheduler, p);
    } else {
      scheduler.runBroadcastPipeline(p);
    }
  }

  // do final shuffle
  scheduler.placeResultPartition();

  return NetHEFTScheduler::convertToAssignment(std::move(scheduler));
}
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

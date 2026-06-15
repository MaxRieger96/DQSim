#include "src/planning/HEFTScheduler.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <ranges>
#include <utility>
#include <vector>

#include "src/Util.hpp"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2025 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace {
//----------------------------------------------------------------------------------------------------------------------
using namespace dqsim;
//----------------------------------------------------------------------------------------------------------------------
std::pair<size_t, double> findFirstAvailableSlot(const std::vector<SchedulingProblem::ScheduledTask>& tasks,
                                                 const std::vector<size_t>& processorTasks, double minTime,
                                                 double processingTime) {
  size_t selectedSlot = 0;
  for (size_t slot = 0; slot < processorTasks.size(); ++slot) {
    selectedSlot = slot + 1;
    size_t iSlotTask = processorTasks[slot];
    const auto& slotTask = tasks[iSlotTask];
    if (slotTask.finishTime < minTime) {
      continue;
    }
    if (slotTask.startTime > minTime) {
      // we are in a gap
      if (slotTask.startTime - minTime > processingTime)
        // gap is large enough
        return {slot, minTime};
    }
    // if we are at blocked slot or the gap is not large enough set min time to beginning of next gap
    minTime = slotTask.finishTime;
  }
  return {selectedSlot, minTime};
}
//----------------------------------------------------------------------------------------------------------------------
}  // namespace
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
SchedulingProblem::SchedulingProblem(uint64_t nTasks, uint64_t nProcessors,
                                     const std::vector<TaskDependency>& dependencies, Matrix<double> computationTimes,
                                     Matrix<double> bandwidths, std::vector<double> latencies)
    : nTasks(nTasks),
      nProcessors(nProcessors),
      dependencies(nTasks),
      successors(nTasks),
      dependencyData(nTasks, nTasks),
      computationTimes(std::move(computationTimes)),
      bandwidths(std::move(bandwidths)),
      latencies(std::move(latencies)) {
  assignDependencies(dependencies);
}
//----------------------------------------------------------------------------------------------------------------------
SchedulingProblem::SchedulingProblem(uint64_t nTasks, uint64_t nProcessors,
                                     const std::vector<TaskDependency>& dependencies,
                                     std::vector<double> computationTimes, double bandwidth, double latency)
    : nTasks(nTasks),
      nProcessors(nProcessors),
      dependencies(nTasks),
      successors(nTasks),
      dependencyData(nTasks, nTasks),
      computationTimes(nTasks, nProcessors),
      bandwidths(nProcessors, nProcessors),
      latencies(nProcessors, latency) {
  assignDependencies(dependencies);
  assignUniformComputationTimes(std::move(computationTimes));
  assignUniformBandwidth(bandwidth);
}
//----------------------------------------------------------------------------------------------------------------------
SchedulingProblem::SchedulingProblem(uint64_t nTasks, uint64_t nProcessors,
                                     const std::vector<TaskDependency>& dependencies, Matrix<double> computationTimes,
                                     double bandwidth, double latency)
    : nTasks(nTasks),
      nProcessors(nProcessors),
      dependencies(nTasks),
      successors(nTasks),
      dependencyData(nTasks, nTasks),
      computationTimes(std::move(computationTimes)),
      bandwidths(nProcessors, nProcessors),
      latencies(nProcessors, latency) {
  assignDependencies(dependencies);
  assignUniformBandwidth(bandwidth);
}
//----------------------------------------------------------------------------------------------------------------------
void SchedulingProblem::assignDependencies(const std::vector<TaskDependency>& dependencyList) {
  for (const auto& d : dependencyList) {
    // We assume that the first task is the starting task and the last task is the exit task
    // Further tasks have to be in a valid order, meaning that each task comes after all it's dependencies
    assert(d.fromTask < d.toTask);
    dependencies[d.toTask].push_back(d.fromTask);
    successors[d.fromTask].push_back(d.toTask);
    dependencyData(d.fromTask, d.toTask) = d.data;
  }
}
//----------------------------------------------------------------------------------------------------------------------
void SchedulingProblem::assignUniformComputationTimes(std::vector<double> computationTimeList) {
  for (size_t i = 0; i < nTasks; ++i) {
    for (size_t j = 0; j < nProcessors; ++j) {
      computationTimes(i, j) = computationTimeList[i];
    }
  }
}
//----------------------------------------------------------------------------------------------------------------------
void SchedulingProblem::assignUniformBandwidth(double bandwidth) {
  for (size_t i = 0; i < nProcessors; ++i) {
    for (size_t j = 0; j < nProcessors; ++j) {
      bandwidths(i, j) = bandwidth;
    }
  }
}
//----------------------------------------------------------------------------------------------------------------------
std::vector<double> SchedulingProblem::getUpRank() const {
  std::vector<double> avgTaskTime(nTasks);
  for (size_t i = 0; i < nTasks; ++i) {
    size_t nCounted = 0;
    double currentAvg = 0;
    for (size_t j = 0; j < nProcessors; ++j) {
      double time = computationTimes(i, j);
      if (time < std::numeric_limits<double>::max()) {
        currentAvg += computationTimes(i, j);
        ++nCounted;
      }
    }
    avgTaskTime[i] = currentAvg / nCounted;
  }

  double avgBandwidth = 0;
  for (size_t i = 0; i < nProcessors; ++i) {
    for (size_t j = 0; j < nProcessors; ++j) {
      if (i != j) avgBandwidth += bandwidths(i, j);
    }
  }
  avgBandwidth = avgBandwidth / (nProcessors * (nProcessors - 1));

  double avgLatency = std::accumulate(latencies.begin(), latencies.end(), 0.0) / latencies.size();

  Matrix<double> avgCommunicationCost(nTasks, nTasks);
  for (size_t i = 0; i < nTasks; ++i) {
    for (size_t j = 0; j < nTasks; ++j) {
      avgCommunicationCost(i, j) = avgLatency + dependencyData(i, j) / avgBandwidth;
    }
  }

  std::vector<double> upRank(nTasks, -1);
  for (size_t i = 0; i < nTasks; ++i) {
    // We use reverse order to make sure that all values are present
    size_t current = nTasks - i - 1;
    double maxSuccTime = 0;
    for (auto succ : successors[current]) {
      assert(upRank[succ] >= 0);
      double currentSuccTime = avgCommunicationCost(current, succ) + upRank[succ];
      maxSuccTime = std::max(currentSuccTime, maxSuccTime);
    }
    upRank[current] = avgTaskTime[current] + maxSuccTime;
  }
  return upRank;
}
//----------------------------------------------------------------------------------------------------------------------
std::vector<SchedulingProblem::ScheduledTask> SchedulingProblem::runHEFT() const {
  auto upRank = getUpRank();

  std::vector<size_t> priority = std::ranges::to<std::vector>(std::views::iota(0uz, nTasks));
  std::ranges::sort(priority, [&](size_t a, size_t b) { return upRank[a] > upRank[b]; });

  // Same order as original tasks
  std::vector<ScheduledTask> scheduledTasks(nTasks, {nTasks, -1, -1});
  // Has indices of original tasks for each processor
  std::vector<std::vector<size_t>> processorTasks(nProcessors);

  for (size_t i = 0; i < nTasks; ++i) {
    size_t currentTask = priority[i];
    double earliestFinishTime = std::numeric_limits<double>::max();
    double actualStartTime = std::numeric_limits<double>::max();
    size_t bestProcessor = nProcessors;
    size_t procSlot = -1;

    // find best processor
    for (size_t proc = 0; proc < nProcessors; ++proc) {
      double earliestStartTime = 0;
      for (size_t dep : dependencies[currentTask]) {
        assert(scheduledTasks[dep].startTime >= 0);
        uint64_t sourceProc = scheduledTasks[dep].processor;
        assert(sourceProc < nProcessors);
        double commTime = latencies[sourceProc] + dependencyData(dep, currentTask) / bandwidths(sourceProc, proc);
        commTime = sourceProc == proc ? 0 : commTime;
        double depStartTime = commTime + scheduledTasks[dep].finishTime;
        earliestStartTime = std::max(earliestStartTime, depStartTime);
      }

      auto [slot, startTime] = findFirstAvailableSlot(scheduledTasks, processorTasks[proc], earliestStartTime,
                                                      computationTimes(currentTask, proc));
      assert(startTime >= earliestStartTime);
      double finishTime = startTime + computationTimes(currentTask, proc);
      if (finishTime < earliestFinishTime) {
        earliestFinishTime = finishTime;
        bestProcessor = proc;
        procSlot = slot;
        actualStartTime = startTime;
      }
    }
    assert(bestProcessor < nProcessors);

    // schedule to processor
    scheduledTasks[currentTask] = ScheduledTask{bestProcessor, actualStartTime, earliestFinishTime};
    // insert into proc slot
    auto& currentProcTasks = processorTasks[bestProcessor];
    currentProcTasks.insert(currentProcTasks.begin() + procSlot, currentTask);
  }
  return scheduledTasks;
}
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

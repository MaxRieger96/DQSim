#include "src/planning/ComponentPlanner.hpp"

#include <algorithm>
#include <cassert>
#include <concepts>
#include <cstdint>
#include <deque>
#include <iterator>
#include <limits>
#include <print>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "src/CustomTypes.hpp"
#include "src/distributedplan/ProblemWriter.hpp"
#include "src/planning/AssignmentPlannerState.hpp"
#include "src/planning/NetHEFTScheduler.hpp"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2025 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
namespace {
//----------------------------------------------------------------------------------------------------------------------
/// A connected component in the query plan where there are no shuffles
struct PlanComponent {
  /// All DUs in the component
  std::vector<DataUnitId> DUs;
  /// All pipelines in the component in a dependency order
  std::vector<PipelineId> pipelines;
};
//----------------------------------------------------------------------------------------------------------------------
/// All transitive successors of a all pipelines
using PlanIndex = std::unordered_map<PipelineId, std::unordered_set<PipelineId>>;
/// All pipelines that are related to each DU
using PipelineIndex = std::vector<std::unordered_set<PipelineId>>;
//----------------------------------------------------------------------------------------------------------------------
struct PlanIndexStruct {
  PipelineIndex pipelineIndex;
  PlanIndex planIndex;
};
//----------------------------------------------------------------------------------------------------------------------
/// A component with additional information and functionality
struct ComponentManager {
  /// The component
  PlanComponent comp;
  /// The tasks of the component separated by partition index
  std::vector<std::vector<Task>> tasks;
  /// Get the number of different partitions
  uint64_t getPartitionCount() const { return tasks.size(); }
  /// Constructor
  ComponentManager(const PlanComponent& comp, const AssignmentProblem& problem);
};
//----------------------------------------------------------------------------------------------------------------------
ComponentManager::ComponentManager(const PlanComponent& comp, const AssignmentProblem& problem) : comp(comp) {
  assert(!comp.pipelines.empty());
  assert(problem.tasks.contains(comp.pipelines.front()));
  uint64_t nPartitions = problem.tasks.find(comp.pipelines.front())->second.size();
  tasks.resize(nPartitions);
  for (uint64_t i = 0; i < nPartitions; ++i) {
    tasks[i].reserve(comp.pipelines.size());
  }
  for (const auto& p : comp.pipelines) {
    assert(problem.tasks.contains(p));
    const auto& currentTasks = problem.tasks.find(p)->second;
    if (nPartitions != currentTasks.size()) {
      std::println("Problem {}", problem.name);
      std::println("Pipeline {}", p.t);
      std::println("nPartitions: {} currentTasks: {}", nPartitions, currentTasks.size());
      problem.plan.toDOT(std::cout);
    }
    assert(nPartitions == currentTasks.size());
    for (uint64_t iTask = 0; iTask < nPartitions; ++iTask) {
      tasks[iTask].push_back(currentTasks[iTask]);
      assert(currentTasks[iTask].scannedPartition.partitionIndex == iTask ||
             currentTasks[iTask].scannedPartition.partitionIndex == 0);
      assert(currentTasks[iTask].outputPartition.partitionIndex == iTask);
    }
  }
}
//----------------------------------------------------------------------------------------------------------------------
PipelineIndex getPipelineIndex(const DistributedQueryPlan& plan)
/// Create a lookup table to find all pipelines that are related to each DU
{
  PipelineIndex result;
  result.resize(plan.dataUnits.size());
  for (const auto& p : plan.pipelines) {
    result[p.scan].insert(p.id);
    result[p.output].insert(p.id);
    for (auto r : p.requiredData) {
      result[r].insert(p.id);
    }
  }
  return result;
}
//----------------------------------------------------------------------------------------------------------------------
std::vector<PipelineId> pipelineSuccessors(PipelineId pipeline, const DistributedQueryPlan& plan,
                                           const PipelineIndex& pipelineIndex)
// Get the direct successors of a pipeline (including pipelines that read the output shuffled)
{
  std::vector<PipelineId> result;
  DataUnitId outDU = plan.getPipeline(pipeline).output;

  auto addPipeline = [&plan, &result](PipelineId p, DataUnitId scannedDU) {
    {
      assert(plan.getPipeline(p).output != scannedDU);
      [[maybe_unused]] bool foundInput = false;
      if (plan.getPipeline(p).scan == scannedDU) foundInput = true;
      for (const auto& input : plan.getPipeline(p).requiredData) {
        if (input == scannedDU) foundInput = true;
      }
      assert(foundInput);
    }
    result.push_back(p);
  };

  for (const auto& p : pipelineIndex[outDU]) {
    if (p == pipeline) continue;
    addPipeline(p, outDU);
  }

  if (!plan.isShuffleInput(outDU)) return result;

  assert(plan.getShuffleOutput(outDU).has_value());
  DataUnitId outShuffled = *plan.getShuffleOutput(outDU);
  for (const auto& p : pipelineIndex[outShuffled]) {
    addPipeline(p, outShuffled);
  }

  return result;
}
//----------------------------------------------------------------------------------------------------------------------
PlanIndex computePlanIndex(const DistributedQueryPlan& plan, const PipelineIndex& pipelineIndex)
// Find all transitive successors of all pipelines
{
  PlanIndex result;
  for (auto it = plan.dependencyOrder.rbegin(); it != plan.dependencyOrder.rend(); ++it) {
    const PipelineId& p = *it;
    result[p];
    for (const auto& successor : pipelineSuccessors(p, plan, pipelineIndex)) {
      assert(result.contains(successor));
      result[p].insert(successor);
      result[p].insert(result[successor].begin(), result[successor].end());
    }
  }
  return result;
}
//----------------------------------------------------------------------------------------------------------------------
PlanIndexStruct getPlanIndexes(const DistributedQueryPlan& plan) {
  PlanIndexStruct result;
  result.pipelineIndex = getPipelineIndex(plan);
  result.planIndex = computePlanIndex(plan, result.pipelineIndex);
  return result;
}
//----------------------------------------------------------------------------------------------------------------------
std::vector<uint64_t> dependencyOrderIndexes(const DistributedQueryPlan& plan) {
  std::vector<uint64_t> result(plan.pipelines.size());
  for (uint64_t orderPosition = 0; orderPosition < plan.pipelines.size(); ++orderPosition) {
    result[plan.dependencyOrder[orderPosition]] = orderPosition;
  }
  return result;
}
//----------------------------------------------------------------------------------------------------------------------
PlanComponent expandComponent(PipelineId startPipeline, const DistributedQueryPlan& plan,
                              const std::vector<uint64_t>& dependencyOrderIndex,
                              const std::unordered_map<PipelineId, std::vector<Task>>& tasks,
                              const PlanIndexStruct& planIndexes,
                              const std::unordered_set<PipelineId>& prohibitedPipelines) {
  std::unordered_set<PipelineId> exploredPipelines;
  std::unordered_set<DataUnitId> exploredDUs;
  std::unordered_set<DataUnitId> todo;
  for (const auto& du : plan.getPipeline(startPipeline).getDUs()) {
    todo.insert(du);
  }
  assert(!todo.empty());
  while (!todo.empty()) {
    auto current = *todo.begin();
    todo.erase(current);
    for (const auto& p : planIndexes.pipelineIndex[current]) {
      if (!tasks.contains(p)) continue;
      if (prohibitedPipelines.contains(p)) continue;
      exploredPipelines.insert(p);
      for (auto newDU : plan.getPipeline(p).getDUs()) {
        if (!exploredDUs.contains(newDU)) {
          exploredDUs.insert(newDU);
          todo.insert(newDU);
        }
      }
    }
  }
  // sort sortedPipelines by dependency order
  std::vector<PipelineId> sortedPipelines(exploredPipelines.begin(), exploredPipelines.end());
  std::sort(sortedPipelines.begin(), sortedPipelines.end(),
            [&](PipelineId a, PipelineId b) { return dependencyOrderIndex[a] < dependencyOrderIndex[b]; });
  return PlanComponent{
      {exploredDUs.begin(), exploredDUs.end()},
      {sortedPipelines.begin(), sortedPipelines.end()},
  };
}
//----------------------------------------------------------------------------------------------------------------------
std::vector<DataUnitId> shuffledDUsAfterComponent(const PlanComponent& comp, const DistributedQueryPlan& plan)
// Compute all DUs that are created by shuffling any DU that is part of the component
{
  std::vector<DataUnitId> result;
  for (const auto& du : comp.DUs) {
    if (plan.isShuffleInput(du)) result.push_back(du);
  }
  return result;
}
//----------------------------------------------------------------------------------------------------------------------
std::vector<PipelineId> shuffleScanningPipelines(const std::vector<DataUnitId>& shuffledDUs,
                                                 [[maybe_unused]] const DistributedQueryPlan& plan,
                                                 const PipelineIndex& pipelineIndex)
// Compute all Pipelines that depend on any of the shuffled data in the component directly
{
  std::vector<PipelineId> result;
  for (const auto& du : shuffledDUs) {
    DataUnitId shuffledDU = *plan.getShuffleOutput(du);
    for (const auto& p : pipelineIndex[shuffledDU]) {
      assert(plan.getPipeline(p).output != shuffledDU);
      [[maybe_unused]] bool foundInput;
      if (plan.getPipeline(p).scan == shuffledDU) foundInput = true;
      for (const auto& input : plan.getPipeline(p).requiredData) {
        if (input == shuffledDU) foundInput = true;
      }
      assert(foundInput);
      result.push_back(p);
    }
  }
  return result;
}
//----------------------------------------------------------------------------------------------------------------------
std::unordered_set<PipelineId> getProhibitedPipelines(const std::vector<PipelineId>& initialProhibitedPipelines,
                                                      const PlanIndex& planIndex)
// Compute all pipelines that depend on any o the shuffled data in the component
{
  std::unordered_set<PipelineId> result;
  for (const auto& p : initialProhibitedPipelines) {
    assert(planIndex.contains(p));
    result.insert(p);
    for (const auto& dp : planIndex.find(p)->second) {
      result.insert(dp);
    }
  }
  return result;
}
//----------------------------------------------------------------------------------------------------------------------
template <typename R>
concept PipelineIterable = requires(R& r) {
  { std::begin(r) } -> std::input_iterator;  // requires a readable iterator
  { std::end(r) } -> std::sentinel_for<decltype(std::begin(r))>;
  // and that dereferencing yields PipelineId
  requires std::same_as<std::remove_cvref_t<decltype(*std::begin(r))>, PipelineId>;
};
//----------------------------------------------------------------------------------------------------------------------
template <PipelineIterable T>
std::vector<DataUnitId> getInvolvedDUs(const DistributedQueryPlan& plan, const T& pipelines) {
  std::unordered_set<DataUnitId> result;
  for (const auto& p : pipelines) {
    for (const auto& du : plan.getPipeline(p).getDUs()) {
      result.insert(du);
    }
  }
  return {result.begin(), result.end()};
}
//----------------------------------------------------------------------------------------------------------------------
void removeProhibitedPipelines(PlanComponent& component, const DistributedQueryPlan& plan,
                               const PlanIndexStruct& planIndexes, std::unordered_set<DataUnitId>& splitDUs) {
  auto shuffled = shuffledDUsAfterComponent(component, plan);
  auto initialPipelines = shuffleScanningPipelines(shuffled, plan, planIndexes.pipelineIndex);
  auto prohibited = getProhibitedPipelines(initialPipelines, planIndexes.planIndex);
  auto prohibitedDUs = getInvolvedDUs(plan, prohibited);

  std::vector<PipelineId> newPipelines;
  for (const auto& p : component.pipelines) {
    if (!prohibited.contains(p)) {
      newPipelines.push_back(p);
    }
  }
  component.DUs = getInvolvedDUs(plan, newPipelines);
  component.pipelines = std::move(newPipelines);

  // fill splitDUs
  std::unordered_set<DataUnitId> prohibitedDUset(prohibitedDUs.begin(), prohibitedDUs.end());
  for (const auto& du : component.DUs) {
    if (prohibitedDUset.contains(du)) {
      splitDUs.insert(du);
    }
  }
}
//----------------------------------------------------------------------------------------------------------------------
bool pipelineAvailable(PipelineId p, const PlanIndexStruct& planIndexes,
                       const std::vector<PipelineId>& ignoredPipelines,
                       const std::unordered_set<PipelineId>& availablePipelines)
/// Check whether all dependent pipelines are already available, so this pipeline is also available
{
  assert(planIndexes.planIndex.contains(p));
  for (const auto& dp : planIndexes.planIndex.find(p)->second) {
    bool found = false;
    for (const auto& p2 : ignoredPipelines) {
      if (p2 == dp) found = true;
    }
    if (!found && !availablePipelines.contains(dp)) {
      return false;
    }
  }
  return true;
}
//----------------------------------------------------------------------------------------------------------------------
std::pair<std::vector<PipelineId>, std::unordered_map<PipelineId, PlanComponent>> getPipelineOrder(
    const std::vector<PlanComponent>& components, const PlanIndexStruct& planIndexes,
    const std::vector<PipelineId>& broadcastPipelines) {
  std::vector<PipelineId> pipelineOrder;
  std::unordered_map<PipelineId, PlanComponent> componentIndex;

  std::deque<uint64_t> iCompQueue;
  for (uint64_t iComp = 0; iComp < components.size(); ++iComp) {
    iCompQueue.push_front(iComp);
  }
  std::deque<PipelineId> broadcastPipelineQueue;
  for (const auto& p : broadcastPipelines) {
    broadcastPipelineQueue.push_front(p);
  }

  std::unordered_set<PipelineId> placedPipelines;

  // Choose components from the back on which only placed pipelines depend
  while (!iCompQueue.empty() || !broadcastPipelineQueue.empty()) {
    if (!iCompQueue.empty()) {
      uint64_t iComp = iCompQueue.back();
      iCompQueue.pop_back();
      const PlanComponent& comp = components[iComp];
      bool available = true;
      for (const auto& p : comp.pipelines) {
        available &= pipelineAvailable(p, planIndexes, comp.pipelines, placedPipelines);
      }
      if (available) {
        for (auto it = comp.pipelines.rbegin(); it != comp.pipelines.rend(); ++it) {
          placedPipelines.insert(*it);
        }
        componentIndex[comp.pipelines.front()] = comp;
        pipelineOrder.push_back(comp.pipelines.front());
      } else {
        iCompQueue.push_front(iComp);
      }
    }
    if (!broadcastPipelineQueue.empty()) {
      PipelineId p = broadcastPipelineQueue.back();
      broadcastPipelineQueue.pop_back();
      if (pipelineAvailable(p, planIndexes, {}, placedPipelines)) {
        placedPipelines.insert(p);
        pipelineOrder.push_back(p);
      } else {
        broadcastPipelineQueue.push_front(p);
      }
    }
  }
  // std::cout << pipelineOrder.size() << " = " << components.size() << " + " << broadcastPipelines.size() << std::endl;
  assert(pipelineOrder.size() == components.size() + broadcastPipelines.size());
  std::reverse(pipelineOrder.begin(), pipelineOrder.end());
  return {pipelineOrder, componentIndex};
}
//----------------------------------------------------------------------------------------------------------------------
std::pair<std::vector<PlanComponent>, std::vector<PipelineId>> findComponents(
    const DistributedQueryPlan& plan, const std::unordered_map<PipelineId, std::vector<Task>>& tasks,
    const std::vector<uint64_t>& dependencyOrderIndex, const PlanIndexStruct& planIndexes) {
  std::vector<PlanComponent> result;
  std::unordered_set<PipelineId> assignedPipelines;
  std::unordered_set<DataUnitId> splitDUs;
  // pipelines we do not need to assign
  std::vector<PipelineId> skippedPipelines;
  // We intentionally return components in a dependency order
  for (const auto& p : plan.dependencyOrder) {
    if (!tasks.contains(p)) {
      // std::cout << "pipeline " << p << " has no tasks" << std::endl;
      skippedPipelines.push_back(p);
      continue;
    }
    if (assignedPipelines.contains(p)) {
      // const DataUnit& du = plan.getDataUnit(plan.getPipeline(p).scan);
      // std::cout << "pipeline " << p << " skipped, because DU " << du.id << " seems to be assigned already" <<
      // std::endl;
      continue;
    }
    auto current = expandComponent(p, plan, dependencyOrderIndex, tasks, planIndexes, assignedPipelines);
    removeProhibitedPipelines(current, plan, planIndexes, splitDUs);
    assignedPipelines.insert(current.pipelines.begin(), current.pipelines.end());
    result.push_back(current);
  }
  // We intentionally return components in a dependency order
  return {result, skippedPipelines};
}
//----------------------------------------------------------------------------------------------------------------------
/// The time all tasks of a component with taskIndex will be finished on a node
double taskComponentEft(const NetHEFTScheduler& scheduler, const ComponentManager& component, uint64_t iTask,
                        NodeId node, NetHEFTScheduler::Delta& delta) {
  assert(iTask < component.tasks.size());
  delta.clear();
  return scheduler.finishTime({component.tasks[iTask]}, node, delta);
}
//----------------------------------------------------------------------------------------------------------------------
}  // namespace
//----------------------------------------------------------------------------------------------------------------------
std::string ComponentPlanner::getName() const { return "Component"; }
//----------------------------------------------------------------------------------------------------------------------
TaskAssignment ComponentPlanner::assignTasks(const AssignmentProblem& problem) {
  // find components
  PlanIndexStruct planIndexes = getPlanIndexes(problem.plan);
  auto dependencyOrderIndex = dependencyOrderIndexes(problem.plan);
  auto [components, broadcastPipelines] =
      findComponents(problem.plan, problem.tasks, dependencyOrderIndex, planIndexes);
  auto [pipelineOrder, componentIndex] = getPipelineOrder(components, planIndexes, broadcastPipelines);

  // place all components in an EFT like manner
  std::vector<double> pipelineLatestFinishTimes(problem.plan.pipelines.size(), 0);
  NetHEFTScheduler scheduler(problem);
  NetHEFTScheduler::Delta delta(scheduler.problem.cluster.getComputeNodes().size());
  // Iterate over pipelines in some dependency order, then skip if pipeline was part of previous component
  // This is equivalent to iterating components in dependency order.
  for (const auto& p : pipelineOrder) {
    // std::cout << " Looking at pipeline " << p << std::endl;
    if (problem.tasks.contains(p)) {
      if (!componentIndex.contains(p)) continue;
      ComponentManager comp(componentIndex[p], problem);
      for (uint64_t iTask = 0; iTask < comp.getPartitionCount(); ++iTask) {
        double bestTime = std::numeric_limits<double>::max();
        NodeId bestNode;
        for (NodeId iNode{0}; iNode < problem.cluster.getComputeNodes().size(); ++iNode) {
          // Compute the finish time for the whole component
          double currentEft = taskComponentEft(scheduler, comp, iTask, iNode, delta);
          if (currentEft < bestTime) {
            bestTime = currentEft;
            bestNode = iNode;
          }
        }
        for (const auto& t : comp.tasks[iTask]) {
          double start = scheduler.addLoads(bestNode, t);
          double finishTime = scheduler.addExecution(bestNode, t, start);
          pipelineLatestFinishTimes[t.pipeline] = std::max(pipelineLatestFinishTimes[t.pipeline], finishTime);
          scheduler.pipelineAssignments[t.pipeline][iTask] = bestNode;
          // std::cout << " Placed Task " << t.pipeline << "." << t.scannedPartition.partitionIndex << std::endl;
        }
      }
      for (const auto& compPipeline : comp.comp.pipelines) {
        const auto& pipeline = problem.plan.getPipeline(compPipeline);
        // perform shuffle if output is shuffled
        if (problem.plan.isShuffleInput(pipeline.output))
          scheduler.doShuffle(compPipeline, pipelineLatestFinishTimes[compPipeline]);
      }
    } else {
      scheduler.runBroadcastPipeline(p);
      // std::cout << " Ran broadcast pipeline " << p << std::endl;
    }
  }
  // final shuffle
  scheduler.placeResultPartition();
  return NetHEFTScheduler::convertToAssignment(std::move(scheduler));
}
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

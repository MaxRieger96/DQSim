#include "src/planning/HEFTPlanner.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "src/CustomTypes.hpp"
#include "src/Util.hpp"
#include "src/distributedplan/DataUnit.hpp"
#include "src/distributedplan/ProblemWriter.hpp"
#include "src/partitioning/Partition.hpp"
#include "src/planning/AssignmentPlannerState.hpp"
#include "src/planning/HEFTScheduler.hpp"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2025 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace {
//----------------------------------------------------------------------------------------------------------------------
using namespace dqsim;
//----------------------------------------------------------------------------------------------------------------------
struct HeftPlannerImpl {
  /// Number of tasks created for heft
  size_t nTasks = 0;
  /// The assignment Problem
  const AssignmentProblem& problem;
  /// Indices of all tasks in each pipeline
  std::unordered_map<PipelineId, std::vector<uint64_t>> taskIndices;
  /// Mapping from task ptrs to tasks
  std::unordered_map<const Task*, uint64_t> taskIndexMap;
  /// Dependencies between HEFT tasks
  std::vector<SchedulingProblem::TaskDependency> dependencies;
  /// Task index of each partition
  std::unordered_map<Partition, uint64_t> partitionTaskSources;
  /// For each data unit that is created by a shuffle or a broadcast task, stores pipeline that creates data unit
  std::unordered_map<DataUnitId, PipelineId> shuffleOrBroadcastSources;
  /// Cache tasks ensure that data is available for free at the nodes that have partitions cached
  std::unordered_map<uint64_t, std::vector<NodeId>> cachingTaskNodes;

  /// Constructor
  HeftPlannerImpl(const AssignmentProblem& problem);
  /// Create scheduling problem
  SchedulingProblem createSchedulingProblem();
  /// Compute task assignment
  TaskAssignment computeTaskAssignment(const SchedulingProblem& sched);

  /// Add a dependency
  bool addDependency(Partition p, size_t iTask);
  /// Add a dependency for a broadcast pipeline
  bool addBroadcastDependency(Partition sourcePartition, PipelineId pipeline);
  /// Convenience function to push a dependency using db task indexes
  void pushDependency(uint64_t from, uint64_t to, double data);
};
//----------------------------------------------------------------------------------------------------------------------
HeftPlannerImpl::HeftPlannerImpl(const AssignmentProblem& problem) : problem(problem) {}
//----------------------------------------------------------------------------------------------------------------------
SchedulingProblem HeftPlannerImpl::createSchedulingProblem() {
  // add an auxiliary start task that does not have a corresponding db task
  nTasks = 1;
  // Enumerate tasks and save a mapping in both directions
  {
    assert(problem.cluster.nodes.size() == problem.cluster.getComputeNodes().size() ||
           problem.cluster.nodes.size() == problem.cluster.getComputeNodes().size() + 1);
    // If there is no storage node, this index will never be used
    NodeId iStorageNode{problem.cluster.getComputeNodes().size()};
    auto partitioning = PartitionManager::loadPartitioning(problem.partitioning, &problem.cluster);
    for (const auto& [p, n] : partitioning.getOwningNode()) {
      if (n == iStorageNode) {
        continue;
      }
      auto nodes = partitioning.owningAndCachingNodes(p);
      nodes.erase(std::remove(nodes.begin(), nodes.end(), iStorageNode), nodes.end());
      cachingTaskNodes[nTasks] = nodes;
      partitionTaskSources[p] = nTasks;
      ++nTasks;
    }
    for (PipelineId p : problem.plan.dependencyOrder) {
      if (problem.tasks.contains(p)) {
        const auto& ts = problem.tasks.find(p)->second;
        taskIndices[p] = {};
        for (const auto& t : ts) {
          taskIndices[p].push_back(nTasks);
          taskIndexMap[&t] = nTasks;
          assert(!partitionTaskSources.contains(t.outputPartition));
          partitionTaskSources[t.outputPartition] = nTasks;
          ++nTasks;
        }
      } else {
        assert(problem.broadcastTasks.contains(p));
        const auto& bt = problem.broadcastTasks.find(p)->second;
        shuffleOrBroadcastSources[bt.outputPartition.dataUnitId] = bt.pipeline;
        taskIndices[p] = {};
        for (uint64_t nodeId = 0; nodeId < problem.cluster.getComputeNodes().size(); ++nodeId) {
          taskIndices[p].push_back(nTasks);
          ++nTasks;
        }
      }
    }
  }

  // add a finish task
  ++nTasks;

  // find pipelines for all DUs that are created by a shuffle
  for (const auto& s : problem.plan.shuffleStages) {
    const DataUnit& targetDU = problem.plan.getDataUnit(s.to);
    // find the pipeline that produces the sourceDU
    PipelineId producingPipeline;
    bool foundPipeline = false;
    for (const auto& p : problem.plan.pipelines) {
      if (p.output == s.from) {
        producingPipeline = p.id;
        foundPipeline = true;
      }
    }
    assert(foundPipeline || !problem.plan.getDataUnit(s.from).isIntermediate);
    if (foundPipeline) shuffleOrBroadcastSources[targetDU.id] = producingPipeline;
  }

  // find dependencies for all tasks
  {
    for (const auto& [p, ts] : problem.tasks) {
      for (size_t i = 0; i < ts.size(); ++i) {
        const auto& t = ts[i];
        bool foundDep = false;
        uint64_t iTask = taskIndices[p][i];
        foundDep |= addDependency(t.scannedPartition, iTask);
        for (const auto& req : t.requiredPartitions) {
          foundDep |= addDependency(req, iTask);
        }
        // tasks without predecessor depend on auxiliary start task
        if (!foundDep) {
          dependencies.push_back({0, iTask, 0});
        }
      }
    }
  }
  // dependencies of broadcast tasks
  {
    for (const auto& [p, bt] : problem.broadcastTasks) {
      bool foundDep = false;
      foundDep |= addBroadcastDependency(bt.scannedPartition, bt.pipeline);
      for (const auto& req : bt.requiredPartitions) {
        foundDep |= addBroadcastDependency(req, bt.pipeline);
      }
      // tasks without predecessor depend on auxiliary start task
      if (!foundDep) {
        for (uint64_t iTargetTask : taskIndices[p]) {
          dependencies.push_back({0, iTargetTask, 0});
        }
      }
    }
  }
  // dependency to final task
  {
    const Pipeline& p = problem.plan.getPipeline(problem.plan.dependencyOrder.back());
    DataUnitId shuffledOutput = p.output;
    double dataSize = 0;
    if (problem.plan.isShuffleInput(shuffledOutput)) {
      [[maybe_unused]] bool found = false;
      for (const auto& s : problem.plan.shuffleStages) {
        if (s.from == shuffledOutput) {
          shuffledOutput = s.to;
          found = true;
          break;
        }
      }
      assert(found);
    }
    assert(shuffledOutput == problem.plan.getResultDataUnit());
    for (const auto& iTask : taskIndices[p.id]) {
      dependencies.push_back({iTask, nTasks - 1, dataSize});
    }
  }

  // find computation times for all task / node combinations
  {
    uint64_t nNodes = problem.cluster.getComputeNodes().size();
    Matrix<double> computationTimes(nTasks, nNodes);
    for (const auto& [p, ts] : taskIndices) {
      const auto& pipeline = problem.plan.getPipeline(p);
      bool isBroadcast = problem.plan.isBroadcast(pipeline.id);
      for (size_t iNode = 0; iNode < nNodes; ++iNode) {
        auto nInPartitions = problem.plan.getDataUnit(pipeline.scan).partitionLayout->nPartitions;
        assert(nInPartitions);
        double load = pipeline.estimatedLoad / nInPartitions / (problem.cluster.getComputeNodes()[iNode].nCores / 16.0);
        //  make sure that each broadcast task will be executed exactly on each node using computation time
        for (size_t i = 0; i < ts.size(); ++i) {
          const auto& t = ts[i];
          computationTimes(t, iNode) = (!isBroadcast || (i == iNode)) ? load : std::numeric_limits<double>::max();
          assert(computationTimes(t, iNode) >= 0);
          static_assert(std::numeric_limits<double>::max() >= 0);
          static_assert(std::numeric_limits<double>::max() + 10 >= 0);
        }
      }
    }
    // add computation time for cache tasks
    for (const auto& [t, ns] : cachingTaskNodes) {
      for (uint64_t iNode = 0; iNode < nNodes; ++iNode) {
        computationTimes(t, iNode) = std::numeric_limits<double>::max();
      }
      for (auto n : ns) {
        computationTimes(t, n) = 0;
      }
    }
    // create a HEFT scheduling problem instance
    Matrix<double> bandwidths(nNodes, nNodes);
    std::vector<double> latencies(nNodes, 0);
    for (uint64_t iNode = 0; iNode < nNodes; ++iNode) {
      latencies[iNode] = problem.cluster.getComputeNodes()[iNode].networkLatency;
      for (uint64_t jNode = 0; jNode < nNodes; ++jNode) {
        // translate to bytes per second
        bandwidths(iNode, jNode) = std::min(problem.cluster.getComputeNodes()[iNode].byteBandwidth(),
                                            problem.cluster.getComputeNodes()[jNode].byteBandwidth());
      }
    }
    return SchedulingProblem(nTasks, nNodes, dependencies, std::move(computationTimes), std::move(bandwidths),
                             std::move(latencies));
  }
}
//----------------------------------------------------------------------------------------------------------------------
bool HeftPlannerImpl::addDependency(Partition p, size_t iTask) {
  bool foundDep = false;
  double size = problem.plan.getDataUnit(p.dataUnitId).partitionSize({});

  // direct source tasks
  if (partitionTaskSources.contains(p)) {
    pushDependency(partitionTaskSources[p], iTask, size);
    foundDep = true;
  }

  // shuffle or broadcast sources
  if (shuffleOrBroadcastSources.contains(p.dataUnitId)) {
    assert(!foundDep);
    // This will depend on all tasks that are in the pipeline
    assert(taskIndices.contains(shuffleOrBroadcastSources[p.dataUnitId]));
    // uint64_t nSources = taskIndices[shuffleOrBroadcastSources[p.dataUnitId]].size();
    for (uint64_t iSourceTask : taskIndices[shuffleOrBroadcastSources[p.dataUnitId]]) {
      pushDependency(iSourceTask, iTask, size);  // over-estimate for congestion
    }
  }
  return foundDep;
}
//----------------------------------------------------------------------------------------------------------------------
bool HeftPlannerImpl::addBroadcastDependency(Partition sourcePartition, PipelineId pipeline) {
  bool foundDep = false;
  DataUnitId sourceDU = sourcePartition.dataUnitId;

  if (shuffleOrBroadcastSources.contains(sourceDU)) {
    PipelineId sourcePipeline = shuffleOrBroadcastSources[sourceDU];
    // Option 1: input is shuffle or broadcast
    double dataSize = 0;
    // If there is a shuffle we have to wait for the network
    if (problem.plan.isShuffleOutput(sourceDU)) dataSize = problem.plan.getDataUnit(sourceDU).partitionSize({});
    for (uint64_t iSourceTask : taskIndices[sourcePipeline]) {
      assert(taskIndices.contains(pipeline));
      // In case of shuffle, we have some network activity
      for (uint64_t iTargetTask : taskIndices[pipeline]) {
        pushDependency(iSourceTask, iTargetTask, dataSize);
        foundDep = true;
      }
    }
  } else {
    // Option 2: input is regular pipeline
    double dataSize = problem.plan.getDataUnit(sourceDU).partitionSize({});
    if (partitionTaskSources.contains(sourcePartition)) {
      for (uint64_t iTargetTask : taskIndices[pipeline]) {
        foundDep = true;
        pushDependency(partitionTaskSources[sourcePartition], iTargetTask, dataSize);
      }
    }
  }
  return foundDep;
}
//----------------------------------------------------------------------------------------------------------------------
void HeftPlannerImpl::pushDependency(uint64_t from, uint64_t to, double data) {
  assert(from < to);
  dependencies.push_back({from, to, data});
}
//----------------------------------------------------------------------------------------------------------------------
TaskAssignment HeftPlannerImpl::computeTaskAssignment(const SchedulingProblem& sched) {
  auto locatedTasks = sched.runHEFT();
  // std::cout << "makespan is " << locatedTasks.back().finishTime << std::endl;
  AssignmentPlannerState state;
  state.loadProblem(&problem);
  std::vector<size_t> flatAssignment;
  flatAssignment.reserve(state.flatTasks.size());
  for (const auto* t : state.flatTasks) {
    assert(taskIndexMap.contains(t));
    auto iTask = taskIndexMap[t];
    auto st = locatedTasks[iTask];
    flatAssignment.push_back(st.processor);
  }
  state.liftFlatAssignment(flatAssignment);
  return *state.result;
}
//----------------------------------------------------------------------------------------------------------------------
}  // namespace
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
TaskAssignment HEFTPlanner::assignTasks(const AssignmentProblem& newProblem) {
  HeftPlannerImpl planner(newProblem);
  auto sched = planner.createSchedulingProblem();
  return planner.computeTaskAssignment(sched);
}
//----------------------------------------------------------------------------------------------------------------------
std::string HEFTPlanner::getName() const { return "HEFT"; }
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

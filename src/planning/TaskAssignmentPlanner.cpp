#include "src/planning/TaskAssignmentPlanner.hpp"

#include <cassert>
#include <cstddef>
#include <string>
#include <vector>

#include "src/CustomTypes.hpp"
#include "src/distributedplan/ProblemWriter.hpp"
#include "src/partitioning/PartitionLayout.hpp"
#include "src/planning/AssignmentPlannerState.hpp"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2022 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
TaskAssignment RoundRobinPlanner::assignTasks(const AssignmentProblem& newProblem) {
  problem = &newProblem;
  AssignmentPlannerState state;
  NodeId currentNode{0};
  state.loadProblem(&newProblem);
  std::vector<size_t> assignment;
  size_t nNodes = problem->cluster.getComputeNodes().size();
  for (size_t i = 0; i < state.flatTasks.size(); ++i) {
    assignment.push_back(currentNode);
    currentNode = (currentNode + 1) % nNodes;
  }
  state.liftFlatAssignment(assignment);
  return *state.result;
}
//----------------------------------------------------------------------------------------------------------------------
std::string RoundRobinPlanner::getName() const { return "RoundRobin"; }
//----------------------------------------------------------------------------------------------------------------------
TaskAssignmentPlanner::~TaskAssignmentPlanner() {}
//----------------------------------------------------------------------------------------------------------------------
std::string AlignedRoundRobinPlanner::getName() const { return "AlignedRR"; }
//----------------------------------------------------------------------------------------------------------------------
TaskAssignment AlignedRoundRobinPlanner::assignTasks(const AssignmentProblem& newProblem) {
  problem = &newProblem;
  AssignmentPlannerState state;
  state.loadProblem(&newProblem);
  std::vector<size_t> assignment;
  size_t nNodes = problem->cluster.getComputeNodes().size();
  for (size_t i = 0; i < state.flatTasks.size(); ++i) {
    auto p = state.flatTasks[i]->pipeline;
    [[maybe_unused]] auto outDu = state.problem->plan.getPipeline(p).output;
    assert(!(state.problem->plan.getDataUnit(outDu).partitionLayout->type == PartitionLayoutType::Broadcast));
    assignment.push_back(state.flatTasks[i]->outputPartition.partitionIndex % nNodes);
  }
  state.liftFlatAssignment(assignment);
  return *state.result;
}
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

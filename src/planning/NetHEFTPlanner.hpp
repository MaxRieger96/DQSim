#pragma once
//----------------------------------------------------------------------------------------------------------------------
#include "src/distributedplan/ProblemWriter.hpp"
#include "src/planning/TaskAssignmentPlanner.hpp"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2025 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
/// This planner will have the problem that it will place some pipeline first and can never consider future pipelines in
/// this step
class NetHEFTPlanner : public TaskAssignmentPlanner {
 public:
  /// Assign tasks to nodes
  TaskAssignment assignTasks(const AssignmentProblem& problem) override;
  /// Get the name of the planner
  std::string getName() const override;
};
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

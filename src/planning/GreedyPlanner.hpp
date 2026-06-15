#pragma once
//----------------------------------------------------------------------------------------------------------------------
#include "src/planning/TaskAssignmentPlanner.hpp"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2024 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
/// Simple greedy task assignment that assigns tasks to nodes which need to load the least data and fulfills
/// distribution requirements Falls back to simple round robin when no node is best
class GreedyPlanner1 : public TaskAssignmentPlanner {
 public:
  /// Assign tasks to nodes
  TaskAssignment assignTasks(const AssignmentProblem& problem) override;
  /// Get the name of the planner
  std::string getName() const override;
};
//----------------------------------------------------------------------------------------------------------------------
/// Simple greedy task assignment that assigns tasks to nodes which need to load the least data and fulfills
/// distribution requirements Falls back to aligned round robin when no node is best
class GreedyPlanner2 : public TaskAssignmentPlanner {
 public:
  /// Assign tasks to nodes
  TaskAssignment assignTasks(const AssignmentProblem& problem) override;
  /// Get the name of the planner
  std::string getName() const override;
};
//----------------------------------------------------------------------------------------------------------------------
/// Simple greedy task assignment that assigns tasks to nodes which need to load the least data and fulfills
/// distribution requirements Falls back to EST-based load-balanced aligned assignment when no node is best
class GreedyPlanner3 : public TaskAssignmentPlanner {
 public:
  /// Assign tasks to nodes
  TaskAssignment assignTasks(const AssignmentProblem& problem) override;
  /// Get the name of the planner
  std::string getName() const override;
};
//----------------------------------------------------------------------------------------------------------------------
/// Simple greedy task assignment that assigns tasks to nodes which need to load the least data and fulfills
/// distribution requirements Falls back to EFT-based load-balanced aligned assignment when no node is best
class GreedyPlanner4 : public TaskAssignmentPlanner {
 public:
  /// Assign tasks to nodes
  TaskAssignment assignTasks(const AssignmentProblem& problem) override;
  /// Get the name of the planner
  std::string getName() const override;
};
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

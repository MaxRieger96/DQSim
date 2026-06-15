#pragma once
//----------------------------------------------------------------------------------------------------------------------
#include <string>
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2022 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
struct AssignmentProblem;
struct TaskAssignment;
//----------------------------------------------------------------------------------------------------------------------
/// Base class for task assignment solvers
class TaskAssignmentPlanner {
 protected:
  /// The problem that is currently solved
  const AssignmentProblem* problem;

 public:
  /// Assign tasks to nodes
  virtual TaskAssignment assignTasks(const AssignmentProblem& problem) = 0;
  /// Get the name of the planner
  virtual std::string getName() const = 0;
  /// Virtual destructor
  virtual ~TaskAssignmentPlanner();
};
//----------------------------------------------------------------------------------------------------------------------
/// Simple round robin task assignment that fulfills distribution requirements
class RoundRobinPlanner : public TaskAssignmentPlanner {
 public:
  /// Assign tasks to nodes
  TaskAssignment assignTasks(const AssignmentProblem& problem) override;
  /// Get the name of the planner
  std::string getName() const override;
};
//----------------------------------------------------------------------------------------------------------------------
/// Round robin task assignment will always assign the same partitionIndex to the same node
class AlignedRoundRobinPlanner : public TaskAssignmentPlanner {
 public:
  /// Assign tasks to nodes
  TaskAssignment assignTasks(const AssignmentProblem& problem) override;
  /// Get the name of the planner
  std::string getName() const override;
};
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

#pragma once
//----------------------------------------------------------------------------------------------------------------------
#include <boost/multiprecision/cpp_int.hpp>
#include <memory>

#include "src/planning/TaskAssignmentPlanner.hpp"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2024 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
struct AssignmentPlannerState;
//----------------------------------------------------------------------------------------------------------------------
/// Abstract class that allows to make use of enumeration of all assignments
class EnumerationPlanner : public TaskAssignmentPlanner {
 public:
  /// Large integers are necessary because of the exponential complexity here
  using LargeInt = boost::multiprecision::cpp_int;
  /// Number of bits used by largeInts in this class
  static constexpr size_t largeIntBits = sizeof(LargeInt) * 8;
  /// Cost of all sampled assignments
  std::vector<double> sampledCost{};

 protected:
  /// The best assignment found so far
  std::unique_ptr<TaskAssignment> best;
  /// The best score found so far
  double bestScore = std::numeric_limits<double>::infinity();
  /// Whether to use multiple threads
  bool multithreaded;

 public:
  /// Constructor
  explicit EnumerationPlanner(bool multithreaded);
  /// Destructor
  ~EnumerationPlanner();
  /// Get the number of possible assignments
  LargeInt possibleAssignments() const;

 protected:
  /// Get the TaskAssignment of a specific rank
  void unrankAssignment(LargeInt rank, AssignmentPlannerState& state) const;
  /// Sample and evaluate assignment of a specific rank
  void sampleAssignment(LargeInt rank);
  /// Sample assignments multi-threaded
  void sampleAssignments(bool random, size_t n);
  /// Reset for new problem
  void reset();
};
//----------------------------------------------------------------------------------------------------------------------
/// Brute force planner that tries out every possible combination and returns the cheapest one
class BruteForcePlanner : public EnumerationPlanner {
 public:
  /// Constructor
  explicit BruteForcePlanner(bool multithreaded) : EnumerationPlanner(multithreaded) {}
  /// Assign tasks to nodes
  TaskAssignment assignTasks(const AssignmentProblem& problem) override;
  /// Get the name of the planner
  std::string getName() const override;
};
//----------------------------------------------------------------------------------------------------------------------
class RandomizedPlanner : public EnumerationPlanner {
 public:
  /// Number of assignments to sample
  size_t nAssignments;
  /// Assign tasks to nodes
  TaskAssignment assignTasks(const AssignmentProblem& problem) override;
  /// Constructor
  RandomizedPlanner(size_t nAssignments, bool multithreaded)
      : EnumerationPlanner(multithreaded), nAssignments(nAssignments) {}
  /// Get the name of the planner
  std::string getName() const override;
};
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

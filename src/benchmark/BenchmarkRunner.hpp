#pragma once
//----------------------------------------------------------------------------------------------------------------------
#include <vector>

#include "src/benchmark/BenchmarkDefinition.hpp"
#include "src/distributedplan/ProblemWriter.hpp"
#include "src/planning/TaskAssignmentPlanner.hpp"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2025 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
struct BenchmarkRunner {
  /// Create a planner object from the corresponding type
  static std::unique_ptr<TaskAssignmentPlanner> getPlanner(PlannerType type);
  static void run(std::vector<AssignmentProblem>& problems, const BenchmarkDefinition& bench);
};
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim

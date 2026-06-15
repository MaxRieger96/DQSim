#pragma once
//----------------------------------------------------------------------------------------------------------------------
#include "src/distributedplan/ProblemWriter.hpp"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2023 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
/// Collection of methods to build commonly used clusters
class Handcrafted {
 public:
  /// Get all handcrafted problems
  static std::vector<AssignmentProblem> getHandcraftedProblems();
  /// Get problem 1
  static AssignmentProblem getH1();
  /// Get problem 2
  static AssignmentProblem getH2();
  /// Get problem 3
  static AssignmentProblem getH3();
  /// Get the example assignment for problem 3
  static TaskAssignment getH3Assignment(const AssignmentProblem& h3);
};
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

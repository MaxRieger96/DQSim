#include <memory>
#include <string>
#include <vector>

#include "src/Simulator.hpp"
#include "src/SimulatorSettings.hpp"
#include "src/benchmark/Handcrafted.hpp"
#include "src/distributedplan/ProblemWriter.hpp"
#include "src/planning/EnumerationPlanner.hpp"
#include "src/planning/GreedyPlanner.hpp"
#include "src/planning/HEFTPlanner.hpp"
#include "src/planning/NetHEFTPlanner.hpp"
#include "src/planning/PolarisPlanner.hpp"
#include "src/planning/TaskAssignmentPlanner.hpp"
#include "thirdparty/doctest.h"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2025 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
using namespace dqsim;

/// Plan, simulate, and assert that the simulation completed without abort.
/// INFO annotations ensure planner and problem names appear in failure output.
static void planAndSimulate(TaskAssignmentPlanner& planner, const AssignmentProblem& problem) {
  INFO("planner: " << planner.getName());
  INFO("problem: " << problem.name);

  TaskAssignment assignment = planner.assignTasks(problem);

  Simulator simulator(&problem);

  auto runWithSettings = [&](SimulatorSettings settings) {
    INFO("allowing streaming: " << settings.allowStreamingInput);
    Simulator::Result result = simulator.simulate(assignment, settings);

    INFO("abort message: " << result.abortMessage);
    CHECK(result.done);
    CHECK(result.abortMessage.empty());
    CHECK(result.executionTime > 0.0);
    simulator.recordSimulation(assignment, settings);
  };

  auto settings = SimulatorSettings::getDefault();
  settings.allowStreamingInput = false;
  runWithSettings(settings);
  settings.allowStreamingInput = true;
  runWithSettings(settings);
}

//----------------------------------------------------------------------------------------------------------------------
// Standard planners — run on all handcrafted problems.
// New problems added to Handcrafted::getHandcraftedProblems() will automatically be covered.
//----------------------------------------------------------------------------------------------------------------------
TEST_CASE("Simulation_Handcrafted_RoundRobin") {
  RoundRobinPlanner planner;
  for (const auto& problem : Handcrafted::getHandcraftedProblems()) planAndSimulate(planner, problem);
}
//----------------------------------------------------------------------------------------------------------------------
TEST_CASE("Simulation_Handcrafted_AlignedRoundRobin") {
  AlignedRoundRobinPlanner planner;
  for (const auto& problem : Handcrafted::getHandcraftedProblems()) planAndSimulate(planner, problem);
}
//----------------------------------------------------------------------------------------------------------------------
TEST_CASE("Simulation_Handcrafted_HEFT") {
  HEFTPlanner planner;
  for (const auto& problem : Handcrafted::getHandcraftedProblems()) planAndSimulate(planner, problem);
}
//----------------------------------------------------------------------------------------------------------------------
TEST_CASE("Simulation_Handcrafted_NetHEFT") {
  NetHEFTPlanner planner;
  for (const auto& problem : Handcrafted::getHandcraftedProblems()) planAndSimulate(planner, problem);
}
//----------------------------------------------------------------------------------------------------------------------
TEST_CASE("Simulation_Handcrafted_Greedy1") {
  GreedyPlanner1 planner;
  for (const auto& problem : Handcrafted::getHandcraftedProblems()) planAndSimulate(planner, problem);
}
//----------------------------------------------------------------------------------------------------------------------
TEST_CASE("Simulation_Handcrafted_Greedy2") {
  GreedyPlanner2 planner;
  for (const auto& problem : Handcrafted::getHandcraftedProblems()) planAndSimulate(planner, problem);
}
//----------------------------------------------------------------------------------------------------------------------
TEST_CASE("Simulation_Handcrafted_Greedy3") {
  GreedyPlanner3 planner;
  for (const auto& problem : Handcrafted::getHandcraftedProblems()) planAndSimulate(planner, problem);
}
//----------------------------------------------------------------------------------------------------------------------
TEST_CASE("Simulation_Handcrafted_Greedy4") {
  GreedyPlanner4 planner;
  for (const auto& problem : Handcrafted::getHandcraftedProblems()) planAndSimulate(planner, problem);
}
//----------------------------------------------------------------------------------------------------------------------
TEST_CASE("Simulation_Handcrafted_Polaris") {
  // Exercise the three independent boolean axes of PolarisPlanner.
  SUBCASE("P2P_NoForceUpload_NoBroadcast") {
    PolarisPlanner planner{/*allowP2P=*/true, /*forceUpload=*/false, /*allowBroadcastPipelines=*/false, false};
    for (const auto& problem : Handcrafted::getHandcraftedProblems()) planAndSimulate(planner, problem);
  }
  SUBCASE("NoP2P_ForceUpload_NoBroadcast") {
    PolarisPlanner planner{/*allowP2P=*/false, /*forceUpload=*/true, /*allowBroadcastPipelines=*/false, false};
    for (const auto& problem : Handcrafted::getHandcraftedProblems()) planAndSimulate(planner, problem);
  }
  SUBCASE("P2P_NoForceUpload_Broadcast") {
    PolarisPlanner planner{/*allowP2P=*/true, /*forceUpload=*/false, /*allowBroadcastPipelines=*/true, false};
    for (const auto& problem : Handcrafted::getHandcraftedProblems()) planAndSimulate(planner, problem);
  }
}
//----------------------------------------------------------------------------------------------------------------------
// Enumeration planners — intentionally scoped to only H1 and H2 by name.
// Do NOT switch this to Handcrafted::getHandcraftedProblems(): enumeration is exponential and
// must not silently run on larger problems if the handcrafted set is extended in the future.
//----------------------------------------------------------------------------------------------------------------------
TEST_CASE("Simulation_Handcrafted_EnumerationPlanners") {
  // BruteForcePlanner exhaustively evaluates every assignment; feasible only for small problems.
  SUBCASE("H1_BruteForce") {
    BruteForcePlanner planner{/*multithreaded=*/false};
    planAndSimulate(planner, Handcrafted::getH1());
  }
  SUBCASE("H2_BruteForce") {
    BruteForcePlanner planner{/*multithreaded=*/false};
    planAndSimulate(planner, Handcrafted::getH2());
  }
  // RandomizedPlanner with a very low sample count so the test stays fast.
  SUBCASE("H1_Randomized") {
    RandomizedPlanner planner{/*nAssignments=*/5, /*multithreaded=*/false};
    planAndSimulate(planner, Handcrafted::getH1());
  }
  SUBCASE("H2_Randomized") {
    RandomizedPlanner planner{/*nAssignments=*/5, /*multithreaded=*/false};
    planAndSimulate(planner, Handcrafted::getH2());
  }
}
//----------------------------------------------------------------------------------------------------------------------

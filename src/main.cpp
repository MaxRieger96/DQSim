#include <fstream>
#include <print>

#include "src/CustomTypes.hpp"
#include "src/Simulator.hpp"
#include "src/SimulatorSettings.hpp"
#include "src/benchmark/Handcrafted.hpp"
#include "src/planning/TaskAssignmentPlanner.hpp"

int main() {
  using namespace dqsim;
  auto h2 = Handcrafted::getH1();
  // auto assignment = Handcrafted::getH3Assignment(h3);
  AlignedRoundRobinPlanner planner;
  auto assignment = planner.assignTasks(h2);
  Simulator sim(&h2);
  auto recording = sim.recordSimulation(assignment, SimulatorSettings::getDefault());
  std::ofstream fileStream("visualization/example_recording.json");
  fileStream << recording;
  auto res = sim.simulate(assignment, SimulatorSettings::getDefault());
  std::println("{}", res.executionTime);
  return 0;
}

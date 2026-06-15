//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2025 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
#include <CLI11.hpp>
#include <exception>
#include <fstream>
#include <iostream>
#include <json.hpp>  // IWYU pragma: keep
#include <json_fwd.hpp>
#include <memory>
#include <string>

#include "src/Simulator.hpp"
#include "src/SimulatorSettings.hpp"
#include "src/distributedplan/ProblemWriter.hpp"
#include "src/planning/EnumerationPlanner.hpp"
#include "src/planning/TaskAssignmentPlanner.hpp"
//----------------------------------------------------------------------------------------------------------------------
using namespace dqsim;
//----------------------------------------------------------------------------------------------------------------------
int main(int argc, char** argv) {
  CLI::App app{"Partition assignment problem writer command line interface"};

  std::string problemFile;
  // bool execute = false;
  bool printPlan = false;
  std::string assignmentFile;
  std::string recordingFile;
  std::string settingsFile;

  app.add_option("problemfile", problemFile, "Path to the problem file")->required();
  // app.add_flag("--execute", execute, "Execute the simulator on the result");
  app.add_option("-a,--assignment-file", assignmentFile,
                 "Load an existing task assignment from this file instead of planning it");
  app.add_option("-o,--recording-file", recordingFile, "Record the execution and store it to the output file");
  app.add_option("-s,--settings-file", settingsFile, "Load custom settings from a json settings file");
  app.add_flag("-p,--print-plan", printPlan, "Print the plan in DOT format");

  CLI11_PARSE(app, argc, argv);

  AssignmentProblem problem;
  TaskAssignment assignment;
  try {
    std::ifstream problemStream(problemFile);
    problem = AssignmentProblem(nlohmann::json::parse(problemStream));
  } catch (const std::exception& e) {
    std::cerr << "Error reading problem file: " << e.what() << std::endl;
    return 1;
  }

  if (assignmentFile.empty()) {
    std::unique_ptr<TaskAssignmentPlanner> planner;
    planner = std::make_unique<RandomizedPlanner>(1000, true);

    assignment = planner->assignTasks(problem);
  } else {
    try {
      std::ifstream fileStream(assignmentFile);
      assignment = TaskAssignment(nlohmann::json::parse(fileStream));
    } catch (const std::exception& e) {
      std::cerr << "Error reading assignment file: " << e.what() << std::endl;
      return 1;
    }
  }

  SimulatorSettings settings = SimulatorSettings::getDefault();
  if (!settingsFile.empty()) {
    std::ifstream settingsStream(settingsFile);
    settings = SimulatorSettings(nlohmann::json::parse(settingsStream));
    std::cout << settings.toJson() << std::endl;
  }

  if (printPlan) problem.plan.toDOT(std::cout);
  Simulator sim(&problem);
  auto result = sim.simulate(assignment, settings);
  if (result.done)
    std::cout << result.executionTime << std::endl;
  else
    std::cerr << result.abortMessage << std::endl;
  if (!recordingFile.empty()) {
    auto recording = sim.recordSimulation(assignment, settings);
    std::ofstream fileStream(recordingFile);
    fileStream << recording;
  }
  return 0;
}
//----------------------------------------------------------------------------------------------------------------------

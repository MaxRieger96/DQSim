//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2022 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
#include <CLI11.hpp>
#include <fstream>
#include <json.hpp>  // IWYU pragma: keep
#include <json_fwd.hpp>
#include <memory>
#include <string>

#include "src/distributedplan/ProblemWriter.hpp"
#include "src/planning/TaskAssignmentPlanner.hpp"
//----------------------------------------------------------------------------------------------------------------------
using namespace dqsim;

//----------------------------------------------------------------------------------------------------------------------
int main(int argc, char** argv) {
  CLI::App app{"Partition assignment problem writer command line interface"};

  std::string problemFile;
  std::string outFile;
  // bool execute = false;
  std::string plannerName;

  app.add_option("problemfile", problemFile, "Path to the problem file")->required();
  app.add_option("outfile", outFile, "Path to the output file")->required();
  // app.add_flag("--execute", execute, "Execute the simulator on the result");
  app.add_option("--planner", plannerName, "Which planner to use (defaults to round robin)");

  CLI11_PARSE(app, argc, argv);

  AssignmentProblem problem;
  {
    std::ifstream problemStream(problemFile);
    problem = AssignmentProblem(nlohmann::json::parse(problemStream));
  }

  std::unique_ptr<TaskAssignmentPlanner> planner;
  if (plannerName.empty()) {
    planner = std::make_unique<AlignedRoundRobinPlanner>();
  }

  // problem.plan.toDOT(std::cout);
  TaskAssignment assignment = planner->assignTasks(problem);

  // Output
  {
    std::ofstream output(outFile);
    output << assignment.toJson().dump(2);
  }
}
//----------------------------------------------------------------------------------------------------------------------

#include <CLI11.hpp>
#include <filesystem>
#include <format>
#include <fstream>
#include <json.hpp>  // IWYU pragma: keep
#include <json_fwd.hpp>
#include <stdexcept>
#include <vector>

#include "src/benchmark/BenchmarkDefinition.hpp"
#include "src/benchmark/BenchmarkRunner.hpp"
#include "src/distributedplan/ProblemWriter.hpp"
//----------------------------------------------------------------------------------------------------------------------
int main(int argc, char** argv) {
  CLI::App app{"Benchmark runner command line interface"};

  std::string assignmentProblemFile;
  app.add_option("assignmentProblemFile", assignmentProblemFile, "Path to the assignment problem file")->required();

  CLI11_PARSE(app, argc, argv);

  std::ifstream f(assignmentProblemFile);
  const nlohmann::json json = nlohmann::json::parse(f);
  auto problem = dqsim::AssignmentProblem(json);

  assert(problem.broadcastTasks.empty());
  assert(problem.tasks.empty());
  problem.plan.consistencyCheck();

  dqsim::ProblemWriter writer(problem.plan, problem.cluster, std::move(problem.name), problem.resultNode);
  writer.loadBaseRelationPartitioning(problem.partitioning);
  writer.buildTasks();
  auto newProblem = writer.getProblem();
  assert(newProblem.tasks.size() + newProblem.broadcastTasks.size() > 0);
  {
    std::ofstream out(assignmentProblemFile);
    out << newProblem.toJson().dump() << '\n';
  }
  return 0;
}
//----------------------------------------------------------------------------------------------------------------------

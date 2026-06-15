#include <CLI11.hpp>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <json.hpp>  // IWYU pragma: keep
#include <json_fwd.hpp>
#include <mutex>
#include <print>
#include <stdexcept>
#include <vector>

#include "src/benchmark/BenchmarkDefinition.hpp"
#include "src/benchmark/BenchmarkRunner.hpp"
#include "src/distributedplan/ProblemWriter.hpp"
#include "src/infra/ThreadPool.hpp"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2023 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace {
//----------------------------------------------------------------------------------------------------------------------
using namespace dqsim;
//----------------------------------------------------------------------------------------------------------------------
std::vector<AssignmentProblem> loadProblemsFromDirectory(const std::filesystem::path& dir) {
  if (!std::filesystem::exists(dir)) {
    throw std::runtime_error("Directory does not exist: " + dir.string());
  }
  if (!std::filesystem::is_directory(dir)) {
    throw std::runtime_error("Path is not a directory: " + dir.string());
  }

  std::vector<AssignmentProblem> result;
  std::mutex resultMutex;
  {
    ThreadPool pool(std::thread::hardware_concurrency());

    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
      if (!entry.is_regular_file()) continue;
      const auto& path = entry.path();
      if (path.extension() != ".json") continue;

      pool.pushTask([path, &result, &resultMutex]() {
        std::ifstream input(path);
        if (!input.is_open()) {
          throw std::runtime_error("Failed to open problem file: " + path.string());
        }
        auto currentResult = AssignmentProblem(nlohmann::json::parse(input));
        {
          std::unique_lock lock(resultMutex);
          result.push_back(std::move(currentResult));
        }
      });
    }
  }

  return result;
}
//----------------------------------------------------------------------------------------------------------------------
}  // namespace
//----------------------------------------------------------------------------------------------------------------------
int main(int argc, char** argv) {
  CLI::App app{"Benchmark runner command line interface"};

  std::string benchmarkDefinitionFile;
  app.add_option("benchmarkdefinitionfile", benchmarkDefinitionFile, "Path to the benchmark definition file")
      ->required();

  CLI11_PARSE(app, argc, argv);

  auto bench = BenchmarkDefinition::loadBenchmarkFromFile(benchmarkDefinitionFile);
  std::string benchmarkDirectory = std::format("problems/{}", bench.name);
  std::println("Loading problems from {}", benchmarkDirectory);
  std::fflush(stdout);
  std::vector<AssignmentProblem> problems = loadProblemsFromDirectory(benchmarkDirectory);
  // Run the benchmarks
  std::println("Loaded {} problems", problems.size());
  std::fflush(stdout);
  BenchmarkRunner::run(problems, bench);
  return 0;
}
//----------------------------------------------------------------------------------------------------------------------

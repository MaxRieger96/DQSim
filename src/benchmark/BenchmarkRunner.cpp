#include "src/benchmark/BenchmarkRunner.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <json.hpp>  // IWYU pragma: keep
#include <limits>
#include <memory>
#include <numeric>
#include <ostream>
#include <random>
#include <ratio>
#include <string>
#include <utility>
#include <vector>

#include "src/Simulator.hpp"
#include "src/SimulatorSettings.hpp"
#include "src/benchmark/BenchmarkDefinition.hpp"
#include "src/distributedplan/DistributedQueryPlan.hpp"
#include "src/infra/ThreadPool.hpp"
#include "src/planning/ComponentPlanner.hpp"
#include "src/planning/EnumerationPlanner.hpp"
#include "src/planning/GreedyPlanner.hpp"
#include "src/planning/HEFTPlanner.hpp"
#include "src/planning/NetHEFTPlanner.hpp"
#include "src/planning/PolarisPlanner.hpp"
#include "src/planning/TaskAssignmentPlanner.hpp"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2025 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
namespace {
//----------------------------------------------------------------------------------------------------------------------
static thread_local std::mt19937 gen(42);
//----------------------------------------------------------------------------------------------------------------------
std::vector<double> partition_sizes(std::size_t nPartitions, double skew) {
  // Use Zipf distribution to create partition sizes
  std::vector<double> result(nPartitions);
  for (std::size_t i = 0; i < nPartitions; ++i) result[i] = 1.0 / std::pow(i + 1, skew);

  auto sum = std::reduce(result.begin(), result.end());
  for (auto& size : result) size = size * static_cast<double>(nPartitions) / sum;

  std::shuffle(result.begin(), result.end(), gen);
  return result;
}
//----------------------------------------------------------------------------------------------------------------------
std::vector<std::vector<double>> getPartitionSkews(const DistributedQueryPlan& plan, double skewFactor) {
  std::vector<std::vector<double>> result;
  for (const auto& du : plan.dataUnits) {
    assert(du.partitionLayout.has_value());
    if (du.partitionLayout->nPartitions > 1) {
      result.push_back(partition_sizes(du.partitionLayout->nPartitions, skewFactor));
    } else {
      result.push_back({1});
    }
  }
  return result;
}
//----------------------------------------------------------------------------------------------------------------------
BenchmarkResult computeScore(std::string assignmentAlgorithm, const std::vector<AssignmentProblem>& problems,
                             const std::vector<TaskAssignment>& assignments, const std::vector<double>& schedulingTimes,
                             double totalSchedulingTime, std::optional<double> skewFactor,
                             const BenchmarkDefinition& bench) {
  BenchmarkResult result;
  result.totalExecutionTime = 0;
  result.totalSchedulingTime = totalSchedulingTime;
  result.executionTimes.resize(problems.size());
  result.schedulingTimes.resize(problems.size());
  std::vector<std::string> names;

  {
    ThreadPool pool(std::thread::hardware_concurrency());
    for (size_t i = 0; i < problems.size(); ++i) {
      pool.pushTask([&problems, &assignments, &schedulingTimes, &result, &skewFactor, &bench, i]() {
        Simulator simulator(&problems[i]);
        SimulatorSettings settings = SimulatorSettings::getDefault();
        if (skewFactor) {
          settings.partitionSkewFactors = getPartitionSkews(problems[i].plan, *skewFactor);
        }
        assert(bench.nLinearScaling.has_value() == bench.discountFactor.has_value());
        if (bench.nLinearScaling) {
          settings.nLinearScaling = *bench.nLinearScaling;
          settings.discountFactor = *bench.discountFactor;
        }
        auto simulationResult = simulator.simulate(assignments[i], std::move(settings));
        double currentResult = std::numeric_limits<double>::infinity();
        if (simulationResult.done)
          currentResult = simulationResult.executionTime;
        else
          std::cout << problems[i].name << ": " << simulationResult.abortMessage << std::endl;
        result.executionTimes[i] = currentResult;
        result.schedulingTimes[i] = schedulingTimes[i];
      });
      names.push_back(problems[i].name);
    }
  }

  result.totalExecutionTime = std::accumulate(result.executionTimes.begin(), result.executionTimes.end(), 0.);
  result.benchmarkNames = names;
  auto now = std::chrono::system_clock::now();
  result.date = std::format("{:%FT%TZ}", now);
  result.assignmentAlgorithm = std::move(assignmentAlgorithm);
  result.skewFactor = skewFactor;
  return result;
}
//----------------------------------------------------------------------------------------------------------------------
std::pair<std::vector<TaskAssignment>, std::vector<double>> computeAssignment(
    const std::vector<AssignmentProblem>& problems, TaskAssignmentPlanner& planner, std::optional<double> cardErr) {
  std::vector<TaskAssignment> result;
  std::vector<double> schedulingTimes;
  auto t1 = std::chrono::steady_clock::now();
  for (const AssignmentProblem& problem : problems) {
    // std::println("{}", problem.name);
    if (cardErr) {
      auto problemCopy = problem;
      problemCopy.plan.distortCardinalities(*cardErr, true);
      result.push_back(planner.assignTasks(problemCopy));
    } else {
      result.push_back(planner.assignTasks(problem));
    }
    auto t2 = std::chrono::steady_clock::now();
    std::chrono::duration<double, std::micro> duration = t2 - t1;
    schedulingTimes.push_back(duration.count() / 1e6);
    t1 = t2;
  }
  return {result, schedulingTimes};
}
//----------------------------------------------------------------------------------------------------------------------
[[maybe_unused]] void recordSimulations(const std::vector<AssignmentProblem>& problems,
                                        const std::vector<std::string>& names,
                                        const std::vector<TaskAssignment>& assignments) {
  assert(problems.size() == names.size());

  {
    ThreadPool pool(std::thread::hardware_concurrency());
    for (size_t i = 0; i < problems.size(); ++i) {
      pool.pushTask([&problems, &assignments, &names, i]() {
        Simulator simulator(&problems[i]);
        auto recording = simulator.recordSimulation(assignments[i], SimulatorSettings::getDefault());
        std::ofstream stream(std::format("recordings/{}.json", names[i]));
        stream.write(recording.data(), recording.size());
      });
    }
  }
}
//----------------------------------------------------------------------------------------------------------------------
void runAndWrite(const TaskAssignmentPlanner& planner, std::chrono::duration<double, std::micro> duration,
                 const std::vector<AssignmentProblem>& problems, const std::vector<TaskAssignment>& assignments,
                 const std::vector<double>& schedulingTimes, const BenchmarkDefinition& bench,
                 std::optional<double> skewFactor, std::optional<double> cardError) {
  BenchmarkResult score = computeScore(planner.getName(), problems, assignments, schedulingTimes,
                                       duration.count() / 1e6, skewFactor, bench);
  score.cardinalityError = cardError;
  std::cout << " Scheduling Optimization time: " << duration.count() / 1000
            << "ms Simulated Execution Time: " << score.totalExecutionTime << "s" << std::endl;
  std::cout << (score.cardinalityError ? (*score.cardinalityError) : 0) << std::endl;

  auto now = std::chrono::system_clock::now();
  std::string path = std::format("{}/{}/", "benchmark_results", bench.name);
  // Create directory
  std::filesystem::create_directories(path);

  std::string additionalInformation;
  if (skewFactor) additionalInformation += std::format("-{}", *skewFactor);
  if (cardError) additionalInformation += std::format("-{}", *cardError);

  std::string outFile = std::format("{}/{}{}-{:%FT%TZ}.json", path, planner.getName(), additionalInformation, now);
  std::ofstream stream(outFile);
  stream << score.toJson().dump(2);
};
//----------------------------------------------------------------------------------------------------------------------
void scaleProblems(std::vector<AssignmentProblem>& problems, double scaleFactor) {
  for (auto& p : problems) {
    p.plan.distortCardinalities(scaleFactor, false);
  }
}
//----------------------------------------------------------------------------------------------------------------------
}  // namespace
//----------------------------------------------------------------------------------------------------------------------
std::unique_ptr<TaskAssignmentPlanner> BenchmarkRunner::getPlanner(PlannerType type) {
  switch (type) {
    case PlannerType::RR:
      return std::make_unique<RoundRobinPlanner>();
    case PlannerType::ARR:
      return std::make_unique<AlignedRoundRobinPlanner>();
    case PlannerType::HEFT:
      return std::make_unique<HEFTPlanner>();
    case PlannerType::NetHEFT:
      return std::make_unique<NetHEFTPlanner>();
    case PlannerType::Greedy1:
      return std::make_unique<GreedyPlanner1>();
    case PlannerType::Greedy2:
      return std::make_unique<GreedyPlanner2>();
    case PlannerType::Greedy3:
      return std::make_unique<GreedyPlanner3>();
    case PlannerType::Greedy4:
      return std::make_unique<GreedyPlanner4>();
    case PlannerType::CEFT:
      return std::make_unique<ComponentPlanner>();
    case PlannerType::BruteForce:
      return std::make_unique<BruteForcePlanner>(true);
    case PlannerType::Rand1k:
      return std::make_unique<RandomizedPlanner>(1000, true);
    case PlannerType::Rand10k:
      return std::make_unique<RandomizedPlanner>(10000, true);
    case PlannerType::Rand100k:
      return std::make_unique<RandomizedPlanner>(100000, true);
    case PlannerType::StateSep:
      return std::make_unique<PolarisPlanner>(false, true, false, false);
    case PlannerType::StateSepB:
      return std::make_unique<PolarisPlanner>(false, true, true, false);
    case PlannerType::StateSepP2P:
      return std::make_unique<PolarisPlanner>(true, true, false, false);
    case PlannerType::StateSepP2PB:
      return std::make_unique<PolarisPlanner>(true, true, true, false);
    case PlannerType::StateSepNoUp:
      return std::make_unique<PolarisPlanner>(true, false, false, false);
    case PlannerType::StateSepNoUpB:
      return std::make_unique<PolarisPlanner>(true, false, true, false);
    case PlannerType::StateSepNoUpD:
      return std::make_unique<PolarisPlanner>(true, false, false, true);
    case PlannerType::StateSepNoUpDB:
      return std::make_unique<PolarisPlanner>(true, false, true, true);
  }
  assert(false);  // unreachable
  return nullptr;
}
//----------------------------------------------------------------------------------------------------------------------
void BenchmarkRunner::run(std::vector<AssignmentProblem>& problems, const BenchmarkDefinition& bench) {
  if (bench.scaleFactor) {
    scaleProblems(problems, *bench.scaleFactor);
  }
  for (const auto& plannerType : bench.planners) {
    auto planner = getPlanner(plannerType);
    std::cout << "Running planner " << planner->getName() << std::endl;

    std::vector<std::optional<double>> cardErrs;
    if (bench.cardinalityErrors) {
      cardErrs.insert(cardErrs.end(), bench.cardinalityErrors->begin(), bench.cardinalityErrors->end());
    } else {
      cardErrs.push_back(std::nullopt);
    }

    /// The first index of the vector is for the cardinality estimation errors
    std::vector<std::vector<TaskAssignment>> assignments;
    std::vector<std::vector<double>> schedulingTimes;

    auto t1 = std::chrono::steady_clock::now();
    for (const auto& cardErr : cardErrs) {
      if (cardErr) std::cout << "Cardinality error: " << *cardErr << std::endl;
      auto [currentAssignments, currentSchedulingTimes] = computeAssignment(problems, *planner, cardErr);
      assignments.push_back(std::move(currentAssignments));
      schedulingTimes.push_back(std::move(currentSchedulingTimes));
    }
    auto t2 = std::chrono::steady_clock::now();
    std::chrono::duration<double, std::micro> duration = t2 - t1;

    // writeAssignments("assignments", assignments, names);
    std::cout << " Evaluating " << problems.size() << " assignments" << std::endl;

    std::vector<std::optional<double>> skewFactors;
    if (bench.skewFactors) {
      skewFactors.insert(skewFactors.end(), (*bench.skewFactors).begin(), (*bench.skewFactors).end());
    } else {
      skewFactors.push_back(std::nullopt);
    }

    for (const auto& skewFactor : skewFactors) {
      for (std::size_t iCardErr = 0; iCardErr < cardErrs.size(); ++iCardErr) {
        runAndWrite(*planner, duration, problems, assignments[iCardErr], schedulingTimes[iCardErr], bench, skewFactor,
                    cardErrs[iCardErr]);
      }
    }
  }
}
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

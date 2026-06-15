#include "src/planning/EnumerationPlanner.hpp"

#include <atomic>
#include <boost/random/mersenne_twister.hpp>
#include <boost/random/uniform_int_distribution.hpp>
#include <cassert>
#include <cstddef>
#include <format>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "src/Simulator.hpp"
#include "src/SimulatorSettings.hpp"
#include "src/distributedplan/ProblemWriter.hpp"
#include "src/planning/AssignmentPlannerState.hpp"
#include "src/planning/TaskAssignmentPlanner.hpp"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2024 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace {
//----------------------------------------------------------------------------------------------------------------------
std::mutex globalMutex;
//----------------------------------------------------------------------------------------------------------------------
using namespace dqsim;
//----------------------------------------------------------------------------------------------------------------------
EnumerationPlanner::LargeInt pow(EnumerationPlanner::LargeInt x, const EnumerationPlanner::LargeInt& p) {
  if (p == 0) return 1;
  if (p == 1) return x;
  EnumerationPlanner::LargeInt tmp = pow(x, p / 2);
  if ((p % 2) == 0)
    return tmp * tmp;
  else
    return x * tmp * tmp;
}
//----------------------------------------------------------------------------------------------------------------------
}  // namespace
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
EnumerationPlanner::EnumerationPlanner(bool multithreaded) : multithreaded(multithreaded) {}
//----------------------------------------------------------------------------------------------------------------------
EnumerationPlanner::~EnumerationPlanner() {};
//----------------------------------------------------------------------------------------------------------------------
EnumerationPlanner::LargeInt EnumerationPlanner::possibleAssignments() const {
  size_t nTasks = 0;
  for (const auto& [_, tasks] : problem->tasks) nTasks += tasks.size();
  return pow(LargeInt(problem->cluster.getComputeNodes().size()), LargeInt(nTasks));
}
//----------------------------------------------------------------------------------------------------------------------
void EnumerationPlanner::unrankAssignment(LargeInt rank, AssignmentPlannerState& state) const {
  std::vector<size_t> assignment;
  size_t nNodes = problem->cluster.getComputeNodes().size();
  for (size_t i = 0; i < state.flatTasks.size(); ++i) {
    assignment.push_back(static_cast<size_t>(rank % nNodes));
    rank /= nNodes;
  }
  assert(assignment.size() == state.flatTasks.size());
  state.liftFlatAssignment(assignment);
}
//----------------------------------------------------------------------------------------------------------------------
void EnumerationPlanner::sampleAssignment(EnumerationPlanner::LargeInt rank) {
  AssignmentPlannerState state;
  state.loadProblem(problem);
  unrankAssignment(std::move(rank), state);
  Simulator simulator(problem);
  Simulator::Result currentResult = simulator.simulate(*state.result, SimulatorSettings::getDefault());
  if (currentResult.done) {
    std::unique_lock lock(globalMutex);
    sampledCost.push_back(currentResult.executionTime);
    if (currentResult.executionTime < bestScore) {
      best = std::move(state.result);
      bestScore = currentResult.executionTime;
    }
  }
}
//----------------------------------------------------------------------------------------------------------------------
void EnumerationPlanner::sampleAssignments(bool random, size_t n) {
  size_t nThreads = std::thread::hardware_concurrency();
  std::atomic<size_t> nDone = 0;
  auto worker = [random, n, &nDone, this](size_t threadId) {
    boost::random::mt19937 mt(threadId);
    boost::random::uniform_int_distribution<LargeInt> ui(0, possibleAssignments() - 1);

    while (true) {
      size_t iCurrent = nDone++;
      if (iCurrent < n) {
        if (random)
          sampleAssignment(ui(mt));
        else
          sampleAssignment(iCurrent);
      } else
        return;
    }
  };
  if (multithreaded) {
    std::vector<std::thread> threads;
    threads.reserve(nThreads);
    for (size_t i = 0; i < nThreads; ++i) {
      threads.push_back(std::thread{worker, i});
    }
    for (auto& t : threads) {
      t.join();
    }
  } else {
    worker(0);
  }
}
//----------------------------------------------------------------------------------------------------------------------
void EnumerationPlanner::reset() {
  sampledCost.clear();
  best = nullptr;
  bestScore = std::numeric_limits<double>::infinity();
}
//----------------------------------------------------------------------------------------------------------------------
TaskAssignment RandomizedPlanner::assignTasks(const AssignmentProblem& newProblem) {
  reset();
  problem = &newProblem;
  assert(sampledCost.empty());
  sampledCost.reserve(nAssignments);
  LargeInt n = possibleAssignments();

  std::vector<LargeInt> ranks;
  ranks.reserve(nAssignments);
  if (n <= nAssignments) {
    sampleAssignments(false, size_t(n));
  } else {
    sampleAssignments(true, nAssignments);
  }
  return *best;
}
//----------------------------------------------------------------------------------------------------------------------
std::string RandomizedPlanner::getName() const { return std::format("Rand_x{}", nAssignments); }
//----------------------------------------------------------------------------------------------------------------------
TaskAssignment BruteForcePlanner::assignTasks(const AssignmentProblem& newProblem) {
  reset();
  problem = &newProblem;
  LargeInt n = possibleAssignments();
  if (n > std::numeric_limits<size_t>::max()) {
    throw std::runtime_error("Brute force on over 2 ** 64 possibilities will likely never return!");
  }
  sampleAssignments(false, size_t(n));
  return *best;
}
//----------------------------------------------------------------------------------------------------------------------
std::string BruteForcePlanner::getName() const { return "BruteForce"; }
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

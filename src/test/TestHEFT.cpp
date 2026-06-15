#include <cmath>
#include <vector>

#include "src/planning/HEFTScheduler.hpp"
#include "thirdparty/doctest.h"
//---------------------------------------------------------------------------
// DQSim
// (c) 2025 Maximilian Rieger
//---------------------------------------------------------------------------
TEST_CASE("TestHEFTPaperTrivial") {
  uint64_t nTasks = 4;
  uint64_t nProcessors = 2;
  std::vector<double> computationTimes{2, 1, 1.5, 0};
  double bandwidth = 10;
  double latency = 0.1;
  std::vector<dqsim::SchedulingProblem::TaskDependency> dependencies = {
      {0, 1, 1},
      {0, 2, 1},
      {1, 3, 0},
      {2, 3, 0},
  };

  dqsim::SchedulingProblem problem(nTasks, nProcessors, dependencies, computationTimes, bandwidth, latency);

  auto upRanks = problem.getUpRank();
  std::vector<double> expectedUpRanks = {3.8, 1.1, 1.6, 0};
  REQUIRE(upRanks.size() == expectedUpRanks.size());
  for (size_t i = 0; i < upRanks.size(); ++i) {
    CHECK(std::abs(upRanks[i] - expectedUpRanks[i]) < 0.001);
  }

  auto result = problem.runHEFT();

  CHECK(std::abs(result[1].startTime - 2.2) < 0.0001);
  CHECK(std::abs(result[1].finishTime - 3.2) < 0.0001);
  CHECK(std::abs(result[2].startTime - 2.0) < 0.0001);
  CHECK(std::abs(result[2].finishTime - 3.5) < 0.0001);
  CHECK(std::abs(result.back().finishTime - 3.5) < 0.0001);

  CHECK(result[0].processor == result[2].processor);
  CHECK(result[0].processor != result[1].processor);
}
//---------------------------------------------------------------------------
TEST_CASE("TestHEFTPaperExample") {
  uint64_t nTasks = 10;
  uint64_t nProcessors = 3;
  std::vector<std::vector<int>> computationTimes{{14, 16, 9}, {13, 19, 18}, {11, 13, 19}, {13, 8, 17},  {12, 13, 10},
                                                 {13, 16, 9}, {7, 15, 11},  {5, 11, 14},  {18, 12, 20}, {21, 7, 16}};
  dqsim::Matrix<double> computationTimesM(nTasks, nProcessors);
  for (size_t i = 0; i < computationTimes.size(); ++i) {
    const std::vector<int>& cur = computationTimes[i];
    for (size_t j = 0; j < cur.size(); j++) {
      computationTimesM(i, j) = cur[j];
    }
  }
  double bandwidth = 1;
  double latency = 0;
  std::vector<dqsim::SchedulingProblem::TaskDependency> dependencies = {
      {0, 1, 18}, {0, 2, 12}, {0, 3, 9},  {0, 4, 11}, {0, 5, 14}, {1, 7, 19}, {1, 8, 16}, {2, 6, 23},
      {3, 7, 27}, {3, 8, 23}, {4, 8, 13}, {5, 7, 15}, {6, 9, 17}, {7, 9, 11}, {8, 9, 13},
  };
  dqsim::SchedulingProblem problem(nTasks, nProcessors, dependencies, computationTimesM, bandwidth, latency);

  auto upRanks = problem.getUpRank();
  std::vector<double> expectedUpRanks = {108, 77, 80, 80, 69, 63.333, 42.666, 35.666, 44.333, 14.666};
  REQUIRE(upRanks.size() == expectedUpRanks.size());
  for (size_t i = 0; i < upRanks.size(); ++i) {
    CHECK(std::abs(upRanks[i] - expectedUpRanks[i]) < 0.001);
  }

  auto result = problem.runHEFT();
  CHECK(std::abs(result.back().finishTime - 80.0) < 0.0001);
  CHECK(result.back().processor == 1);
  CHECK(result.front().processor == 2);
  CHECK(result[1].processor == 0);
  CHECK(std::abs(result[1].startTime - 27.0) < 0.0001);
}
//---------------------------------------------------------------------------
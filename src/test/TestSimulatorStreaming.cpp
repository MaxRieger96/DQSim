#include <limits>
#include <vector>

#include "src/SimulatorImpl.hpp"
#include "src/SimulatorSettings.hpp"
#include "src/benchmark/Handcrafted.hpp"
#include "src/planning/GreedyPlanner.hpp"
#include "thirdparty/doctest.h"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2022 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
using namespace dqsim;
//----------------------------------------------------------------------------------------------------------------------
namespace {
//----------------------------------------------------------------------------------------------------------------------
/// Construct an impl using a real handcrafted problem and a real assignment.
/// The tests then clear and directly seed the runtime state they want to inspect.
static SimulatorImpl makeImpl(const AssignmentProblem& problem, const TaskAssignment& assignment,
                              SimulatorSettings settings) {
  // Adjust these field names if AssignmentProblem uses different names in your repository.
  return SimulatorImpl{problem.cluster, &assignment, problem.partitioning, &problem.plan, problem.resultNode, settings};
}
//----------------------------------------------------------------------------------------------------------------------
static TaskAssignment makeAssignment(const AssignmentProblem& problem) {
  GreedyPlanner1 planner;
  return planner.assignTasks(problem);
}
//----------------------------------------------------------------------------------------------------------------------
static TaskAssignment::Transfer makeSyntheticTransfer(const AssignmentProblem& problem) {
  // Pick any initially owned partition and transfer it between two compute nodes.
  // transferRuntimes() only needs partition/from/to and the plan for partition size.
  REQUIRE(problem.partitioning.owningNode.size() >= 1);
  REQUIRE(problem.cluster.nodes.size() >= 2);

  const auto [partition, owner] = *problem.partitioning.owningNode.begin();

  NodeId from = owner;
  NodeId to = NodeId{0};

  if (to == from) {
    to = NodeId{1};
  }

  REQUIRE(from != to);

  // If this aggregate initialization does not compile, swap the field order according
  // to TaskAssignment::Transfer in ProblemWriter.hpp / CustomTypes.hpp.
  return TaskAssignment::Transfer{
      partition,
      from,
      to,
  };
}
//----------------------------------------------------------------------------------------------------------------------
}  // namespace
//----------------------------------------------------------------------------------------------------------------------
TEST_CASE("Streaming_WaitingTransferDoesNotThrottleActiveTransfer") {
  AssignmentProblem problem = Handcrafted::getH2();
  TaskAssignment assignment = makeAssignment(problem);

  SimulatorSettings settings = SimulatorSettings::getDefault();
  settings.allowStreamingInput = true;

  SimulatorImpl impl = makeImpl(problem, assignment, settings);

  impl.pendingTasks.clear();
  impl.pendingTransfers.clear();
  impl.pendingShuffles.clear();
  impl.runningTasks.clear();
  impl.runningTransfers.clear();
  impl.runningShuffles.clear();

  const auto transfer = makeSyntheticTransfer(problem);

  impl.runningTransfers.emplace_back(&transfer, 0.0);

  auto [baselineTransferTimes, baselineShuffleTimes] = impl.transferRuntimes();

  REQUIRE(baselineShuffleTimes.empty());
  REQUIRE(baselineTransferTimes.size() == 1);
  REQUIRE(baselineTransferTimes[0] != std::numeric_limits<double>::infinity());
  REQUIRE(baselineTransferTimes[0] > 0.0);

  const double baseline = baselineTransferTimes[0];

  impl.runningTransfers.clear();

  impl.runningTransfers.emplace_back(&transfer, 0.0);

  impl.runningTransfers.emplace_back(&transfer, 0.0, 0.5);
  impl.runningTransfers.back().waitingForInput = true;

  auto [transferTimes, shuffleTimes] = impl.transferRuntimes();

  REQUIRE(shuffleTimes.empty());
  REQUIRE(transferTimes.size() == 2);

  CHECK(transferTimes[0] == doctest::Approx(baseline));
  CHECK(transferTimes[1] == std::numeric_limits<double>::infinity());
}
//----------------------------------------------------------------------------------------------------------------------
TEST_CASE("Streaming_WaitingTransferWithLatencyIsNotRunnable") {
  AssignmentProblem problem = Handcrafted::getH2();
  TaskAssignment assignment = makeAssignment(problem);

  SimulatorSettings settings = SimulatorSettings::getDefault();
  settings.allowStreamingInput = true;

  SimulatorImpl impl = makeImpl(problem, assignment, settings);

  impl.pendingTasks.clear();
  impl.pendingTransfers.clear();
  impl.pendingShuffles.clear();
  impl.runningTasks.clear();
  impl.runningTransfers.clear();
  impl.runningShuffles.clear();

  const auto transfer = makeSyntheticTransfer(problem);

  impl.runningTransfers.emplace_back(&transfer, 10.0, 0.5);
  impl.runningTransfers.back().waitingForInput = true;

  auto [transferTimes, shuffleTimes] = impl.transferRuntimes();

  REQUIRE(shuffleTimes.empty());
  REQUIRE(transferTimes.size() == 1);

  CHECK(transferTimes[0] == std::numeric_limits<double>::infinity());
}
//----------------------------------------------------------------------------------------------------------------------
// TEST_CASE("Streaming_WaitingShuffleDoesNotThrottleActiveShuffle") {
//   AssignmentProblem problem = Handcrafted::getH2();
//   TaskAssignment assignment = makeAssignment(problem);

//   REQUIRE(problem.plan.shuffleStages.size() >= 1);

//   SimulatorSettings settings = SimulatorSettings::getDefault();
//   settings.allowStreamingInput = true;

//   SimulatorImpl impl = makeImpl(problem, assignment, settings);

//   impl.pendingTasks.clear();
//   impl.pendingTransfers.clear();
//   impl.pendingShuffles.clear();
//   impl.runningTasks.clear();
//   impl.runningTransfers.clear();
//   impl.runningShuffles.clear();

//   const ShuffleStage& stage = problem.plan.shuffleStages.front();
//   const DataUnit& sourceDU = problem.plan.getDataUnit(stage.from);
//   const DataUnit& targetDU = problem.plan.getDataUnit(stage.to);

//   REQUIRE(sourceDU.partitionLayout->nPartitions >= 1);
//   REQUIRE(targetDU.partitionLayout->nPartitions >= 1);
//   REQUIRE(problem.cluster.nodes.size() >= 2);

//   const Partition sourcePartition{sourceDU.id, 0};
//   const Partition targetPartition{targetDU.id, 0};

//   const NodeId from{0};
//   const NodeId to{1};

//   SimulatorImpl::RunningShuffle activeShuffle{
//       targetPartition, sourcePartition, from, to, 1.0, 0.0, 0.0, 1.0, false,
//   };

//   SimulatorImpl::RunningShuffle waitingShuffle{
//       targetPartition, sourcePartition, from, to, 1.0, 0.0, 0.0, 0.5, true,
//   };

//   impl.runningShuffles.push_back(activeShuffle);

//   auto [baselineTransferTimes, baselineShuffleTimes] = impl.transferRuntimes();
//   REQUIRE(baselineTransferTimes.empty());
//   REQUIRE(baselineShuffleTimes.size() == 1);
//   REQUIRE(finite(baselineShuffleTimes[0]));
//   const double baseline = baselineShuffleTimes[0];

//   impl.runningShuffles.clear();
//   impl.runningShuffles.push_back(activeShuffle);
//   impl.runningShuffles.push_back(waitingShuffle);

//   auto [transferTimes, shuffleTimes] = impl.transferRuntimes();

//   REQUIRE(transferTimes.empty());
//   REQUIRE(shuffleTimes.size() == 2);

//   CHECK(shuffleTimes[0] == doctest::Approx(baseline));
//   CHECK(shuffleTimes[1] == std::numeric_limits<double>::infinity());
// }
//----------------------------------------------------------------------------------------------------------------------
// TEST_CASE("Streaming_WaitingShuffleWithLatencyIsNotRunnable") {
//   AssignmentProblem problem = Handcrafted::getH2();
//   TaskAssignment assignment = makeAssignment(problem);

//   REQUIRE(problem.plan.shuffleStages.size() >= 1);

//   SimulatorSettings settings = SimulatorSettings::getDefault();
//   settings.allowStreamingInput = true;

//   SimulatorImpl impl = makeImpl(problem, assignment, settings);

//   impl.pendingTasks.clear();
//   impl.pendingTransfers.clear();
//   impl.pendingShuffles.clear();
//   impl.runningTasks.clear();
//   impl.runningTransfers.clear();
//   impl.runningShuffles.clear();

//   const ShuffleStage& stage = problem.plan.shuffleStages.front();
//   const DataUnit& sourceDU = problem.plan.getDataUnit(stage.from);
//   const DataUnit& targetDU = problem.plan.getDataUnit(stage.to);

//   const Partition sourcePartition{sourceDU.id, 0};
//   const Partition targetPartition{targetDU.id, 0};

//   SimulatorImpl::RunningShuffle waitingShuffle{
//       targetPartition, sourcePartition, NodeId{0}, NodeId{1}, 1.0, 10.0, 0.0, 0.5, true,
//   };

//   impl.runningShuffles.push_back(waitingShuffle);

//   auto [transferTimes, shuffleTimes] = impl.transferRuntimes();

//   REQUIRE(transferTimes.empty());
//   REQUIRE(shuffleTimes.size() == 1);
//   CHECK(shuffleTimes[0] == std::numeric_limits<double>::infinity());
// }
//----------------------------------------------------------------------------------------------------------------------
// TEST_CASE("Streaming_LocalPartialShuffleCreatesPartialTargetData") {
//   AssignmentProblem problem = Handcrafted::getH2();
//   TaskAssignment assignment = makeAssignment(problem);

//   REQUIRE(problem.plan.shuffleStages.size() >= 1);

//   SimulatorSettings settings = SimulatorSettings::getDefault();
//   settings.allowStreamingInput = true;

//   SimulatorImpl impl = makeImpl(problem, assignment, settings);

//   impl.pendingTasks.clear();
//   impl.pendingTransfers.clear();
//   impl.pendingShuffles.clear();
//   impl.runningTasks.clear();
//   impl.runningTransfers.clear();
//   impl.runningShuffles.clear();
//   impl.collectedShuffles.clear();
//   impl.partialPartitions.clear();
//   impl.firstPartialPartitionNodes.clear();

//   for (NodeId n{0}; n < problem.cluster.nodes.size(); ++n) {
//     impl.partialPartitions[n];
//   }

//   const ShuffleStage& stage = problem.plan.shuffleStages.front();
//   const DataUnit& sourceDU = problem.plan.getDataUnit(stage.from);
//   const DataUnit& targetDU = problem.plan.getDataUnit(stage.to);

//   REQUIRE(sourceDU.partitionLayout->nPartitions >= 1);
//   REQUIRE(targetDU.partitionLayout->nPartitions >= 1);

//   const NodeId localNode{0};
//   const Partition sourcePartition{sourceDU.id, 0};

//   impl.shuffleTargets.clear();
//   for (size_t i = 0; i < targetDU.partitionLayout->nPartitions; ++i) {
//     impl.shuffleTargets[Partition{targetDU.id, i}].push_back(localNode);
//   }

//   constexpr double progressLimit = 0.5;

//   impl.startShuffle(sourceDU.id, targetDU.id, localNode, progressLimit, sourcePartition);

//   const double expectedFractionPerTarget =
//       progressLimit * (1.0 / static_cast<double>(sourceDU.partitionLayout->nPartitions));

//   for (size_t i = 0; i < targetDU.partitionLayout->nPartitions; ++i) {
//     Partition targetPartition{targetDU.id, i};

//     INFO("target partition index: " << i);
//     CHECK(impl.partiallyAvailableFraction(targetPartition, localNode) == doctest::Approx(expectedFractionPerTarget));
//   }

//   REQUIRE(impl.runningShuffles.size() == targetDU.partitionLayout->nPartitions);

//   for (const auto& shuffle : impl.runningShuffles) {
//     CHECK(shuffle.waitingForInput);
//     CHECK(shuffle.progress == doctest::Approx(progressLimit));
//     CHECK(shuffle.progressLimit == doctest::Approx(progressLimit));
//   }
// }
//----------------------------------------------------------------------------------------------------------------------

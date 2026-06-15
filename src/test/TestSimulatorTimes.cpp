#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "src/Simulator.hpp"
#include "src/SimulatorSettings.hpp"
#include "src/benchmark/ClusterBuilder.hpp"
#include "src/benchmark/DUBuilder.hpp"
#include "src/distributedplan/DistributedQueryPlan.hpp"
#include "src/distributedplan/Pipeline.hpp"
#include "src/distributedplan/ProblemWriter.hpp"
#include "src/partitioning/PartitionManager.hpp"
#include "src/planning/GreedyPlanner.hpp"
#include "thirdparty/doctest.h"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// Black-box simulator regression tests for simple hand-built assignments.
//----------------------------------------------------------------------------------------------------------------------
using namespace dqsim;
//----------------------------------------------------------------------------------------------------------------------
namespace {
//----------------------------------------------------------------------------------------------------------------------
static SimulatorSettings makeSettings(bool streaming, bool limitedSteps) {
  SimulatorSettings settings = SimulatorSettings::getDefault();
  settings.allowStreamingInput = streaming;
  settings.limitStepSize = limitedSteps;
  settings.stepLimit = 0.001;
  settings.minimumBlockSize = 1.0;
  return settings;
}
//----------------------------------------------------------------------------------------------------------------------
static std::vector<SimulatorSettings> representativeSettings(bool streaming) {
  return {
      makeSettings(streaming, false),
      makeSettings(streaming, true),
  };
}
//----------------------------------------------------------------------------------------------------------------------
static TaskAssignment::Transfer makeTransfer(Partition partition, NodeId from, NodeId to) {
  return TaskAssignment::Transfer{
      partition,
      from,
      to,
  };
}
//----------------------------------------------------------------------------------------------------------------------
static TaskAssignment::ShuffleTarget makeShuffleTarget(Partition partition, NodeId location) {
  return TaskAssignment::ShuffleTarget{
      partition,
      location,
  };
}
//----------------------------------------------------------------------------------------------------------------------
static TaskAssignment getSingleTaskAssignment(const AssignmentProblem& problem, NodeId node) {
  GreedyPlanner1 planner;
  TaskAssignment assignment = planner.assignTasks(problem);

  REQUIRE(assignment.locatedTasks.size() == 1);

  assignment.locatedTasks[0].node = node;
  assignment.transfers.clear();
  assignment.shuffleTargets.clear();

  return assignment;
}
//----------------------------------------------------------------------------------------------------------------------
static AssignmentProblem makeSingleLocalTaskProblem() {
  Cluster cluster = ClusterBuilder::fromDefinition(ClusterBuilderDefinition{2, true, {"c5n9xlarge", "c5n9xlarge"}});

  DUBuilder duBuilder;

  constexpr double bytes = 1e7;

  DataUnit r = duBuilder.buildDU("R", bytes, 1, false, {{{"x", 8}}}, 0, false, false);
  DataUnit result = duBuilder.buildDU("result", bytes, 1, true, {{{"x", 8}}}, 0, true, false);

  Pipeline p{PipelineId{0}, r.id, result.id, {}, 1e-9, "single local task"};

  DistributedQueryPlan plan{duBuilder.getDUs(), {p}, {}, {PipelineId{0}}};

  PartitionManager partitions{cluster};
  partitions.registerPartition(Partition{r.id, 0}, NodeId{0});

  ProblemWriter writer{plan, cluster, "BlackBoxSingleLocalTask", NodeId{0}};
  writer.loadBaseRelationPartitioning(partitions.getPartitioning());
  writer.buildTasks();

  return writer.getProblem();
}
//----------------------------------------------------------------------------------------------------------------------
static AssignmentProblem makeSingleTaskThenTransferProblem() {
  Cluster cluster = ClusterBuilder::fromDefinition(ClusterBuilderDefinition{2, true, {"c5n9xlarge", "c5n9xlarge"}});

  DUBuilder duBuilder;

  constexpr double bytes = 1e7;

  DataUnit r = duBuilder.buildDU("R", bytes, 1, false, {{{"x", 8}}}, 0, false, false);
  DataUnit result = duBuilder.buildDU("result", bytes, 1, true, {{{"x", 8}}}, 0, true, false);

  Pipeline p{PipelineId{0}, r.id, result.id, {}, 1e-9, "single task then transfer"};

  DistributedQueryPlan plan{duBuilder.getDUs(), {p}, {}, {PipelineId{0}}};

  PartitionManager partitions{cluster};
  partitions.registerPartition(Partition{r.id, 0}, NodeId{0});

  ProblemWriter writer{plan, cluster, "BlackBoxSingleTaskThenTransfer", NodeId{1}};
  writer.loadBaseRelationPartitioning(partitions.getPartitioning());
  writer.buildTasks();

  return writer.getProblem();
}
//----------------------------------------------------------------------------------------------------------------------
static TaskAssignment makeSingleTaskThenTransferAssignment(const AssignmentProblem& problem) {
  TaskAssignment assignment = getSingleTaskAssignment(problem, NodeId{0});

  const Partition output = assignment.locatedTasks[0].task.outputPartition;
  assignment.transfers.push_back(makeTransfer(output, NodeId{0}, NodeId{1}));

  return assignment;
}
//----------------------------------------------------------------------------------------------------------------------
static AssignmentProblem makeTwoIndependentTransfersProblem() {
  Cluster cluster = ClusterBuilder::fromDefinition(ClusterBuilderDefinition{2, true, {"c5n9xlarge", "c5n9xlarge"}});

  DUBuilder duBuilder;

  constexpr double transferBytes = 1e8;
  constexpr double intermediateBytes = 1e3;
  constexpr double resultBytes = 1e3;

  // These are the large base partitions whose explicit transfers we compare.
  DataUnit r1 = duBuilder.buildDU("R1", transferBytes, 1, false, {{{"x", 8}}}, 0, false, false);
  DataUnit r2 = duBuilder.buildDU("R2", transferBytes, 1, false, {{{"x", 8}}}, 0, false, false);

  // Tiny intermediates so task runtime does not dominate transfer runtime.
  DataUnit o1 = duBuilder.buildDU("O1", intermediateBytes, 1, true, {{{"x", 8}}}, 0, false, false);
  DataUnit o2 = duBuilder.buildDU("O2", intermediateBytes, 1, true, {{{"x", 8}}}, 0, false, false);

  // Single final result DU. This must be the only otherwise-unused output.
  DataUnit result = duBuilder.buildDU("result", resultBytes, 1, true, {{{"x", 8}}}, 0, true, false);

  Pipeline p1{PipelineId{0}, r1.id, o1.id, {}, 1e-12, "tiny task for R1"};
  Pipeline p2{PipelineId{1}, r2.id, o2.id, {}, 1e-12, "tiny task for R2"};

  // Consume both O1 and O2 so neither intermediate output is orphaned.
  Pipeline p3{PipelineId{2}, o1.id, result.id, {o2.id}, 1e-12, "consume intermediates"};

  DistributedQueryPlan plan{duBuilder.getDUs(), {p1, p2, p3}, {}, {PipelineId{0}, PipelineId{1}, PipelineId{2}}};

  PartitionManager partitions{cluster};
  partitions.registerPartition(Partition{r1.id, 0}, NodeId{0});
  partitions.registerPartition(Partition{r2.id, 0}, NodeId{0});

  ProblemWriter writer{plan, cluster, "BlackBoxTwoIndependentTransfers", NodeId{0}};
  writer.loadBaseRelationPartitioning(partitions.getPartitioning());
  writer.buildTasks();

  return writer.getProblem();
}
//----------------------------------------------------------------------------------------------------------------------
static TaskAssignment makeOneBaseTransferAssignment(const AssignmentProblem& problem) {
  GreedyPlanner1 planner;
  TaskAssignment assignment = planner.assignTasks(problem);

  REQUIRE(assignment.locatedTasks.size() == 3);

  for (auto& locatedTask : assignment.locatedTasks) {
    locatedTask.node = NodeId{0};
  }

  assignment.transfers.clear();
  assignment.shuffleTargets.clear();

  const DataUnit& r1 = problem.plan.dataUnits[0];

  assignment.transfers.push_back(makeTransfer(Partition{r1.id, 0}, NodeId{0}, NodeId{1}));

  return assignment;
}
//----------------------------------------------------------------------------------------------------------------------
static TaskAssignment makeTwoBaseTransfersAssignment(const AssignmentProblem& problem) {
  TaskAssignment assignment = makeOneBaseTransferAssignment(problem);

  const DataUnit& r2 = problem.plan.dataUnits[1];

  assignment.transfers.push_back(makeTransfer(Partition{r2.id, 0}, NodeId{0}, NodeId{1}));

  return assignment;
}
//----------------------------------------------------------------------------------------------------------------------
static AssignmentProblem makeSingleLocalShuffleProblem() {
  Cluster cluster = ClusterBuilder::fromDefinition(ClusterBuilderDefinition{2, true, {"c5n9xlarge", "c5n9xlarge"}});

  DUBuilder duBuilder;

  constexpr double bytes = 1e7;

  DataUnit r = duBuilder.buildDU("R", bytes, 1, false, {{{"x", 8}}}, 0, false, false);
  DataUnit intermediate = duBuilder.buildDU("intermediate", bytes, 1, true, {{{"x", 8}}}, 0, false, false);
  DataUnit result = duBuilder.buildDU("result", bytes, 1, true, {{{"x", 8}}}, 0, true, false);

  Pipeline p{PipelineId{0}, r.id, intermediate.id, {}, 1e-9, "producer before local shuffle"};

  ShuffleStage s{intermediate.id, result.id};

  DistributedQueryPlan plan{duBuilder.getDUs(), {p}, {s}, {PipelineId{0}}};

  PartitionManager partitions{cluster};
  partitions.registerPartition(Partition{r.id, 0}, NodeId{0});

  ProblemWriter writer{plan, cluster, "BlackBoxSingleLocalShuffle", NodeId{0}};
  writer.loadBaseRelationPartitioning(partitions.getPartitioning());
  writer.buildTasks();

  return writer.getProblem();
}
//----------------------------------------------------------------------------------------------------------------------
static TaskAssignment makeSingleLocalShuffleAssignment(const AssignmentProblem& problem) {
  TaskAssignment assignment = getSingleTaskAssignment(problem, NodeId{0});

  const DataUnit& resultDU = problem.plan.getDataUnit(problem.plan.getResultDataUnit());

  assignment.shuffleTargets.push_back(makeShuffleTarget(Partition{resultDU.id, 0}, NodeId{0}));

  return assignment;
}
//----------------------------------------------------------------------------------------------------------------------
static AssignmentProblem makeSlowProducerPlusIndependentTransferProblem() {
  Cluster cluster = ClusterBuilder::fromDefinition(ClusterBuilderDefinition{2, true, {"c5n9xlarge", "c5n9xlarge"}});

  DUBuilder duBuilder;

  constexpr double bytes = 1e8;
  constexpr double smallBytes = 1e3;

  DataUnit sourceA = duBuilder.buildDU("sourceA", bytes, 1, false, {{{"x", 8}}}, 0, false, false);
  DataUnit producedA = duBuilder.buildDU("producedA", bytes, 1, true, {{{"x", 8}}}, 0, false, false);

  DataUnit baseB = duBuilder.buildDU("baseB", bytes, 1, false, {{{"x", 8}}}, 0, false, false);
  DataUnit producedB = duBuilder.buildDU("producedB", smallBytes, 1, true, {{{"x", 8}}}, 0, false, false);

  DataUnit result = duBuilder.buildDU("result", smallBytes, 1, true, {{{"x", 8}}}, 0, true, false);

  // Slow producer A. Its output is the streamed transfer that can repeatedly wait for input.
  Pipeline pA{PipelineId{0}, sourceA.id, producedA.id, {}, 1e-7, "slow producer A"};

  // Tiny task consuming baseB, so baseB is valid plan input.
  Pipeline pB{PipelineId{1}, baseB.id, producedB.id, {}, 1e-12, "tiny consumer B"};

  // Consume both outputs so producedA and producedB are not orphaned.
  Pipeline pFinal{PipelineId{2}, producedB.id, result.id, {producedA.id}, 1e-12, "final consumer"};

  DistributedQueryPlan plan{duBuilder.getDUs(), {pA, pB, pFinal}, {}, {PipelineId{0}, PipelineId{1}, PipelineId{2}}};

  PartitionManager partitions{cluster};
  partitions.registerPartition(Partition{sourceA.id, 0}, NodeId{0});
  partitions.registerPartition(Partition{baseB.id, 0}, NodeId{0});

  ProblemWriter writer{plan, cluster, "BlackBoxSlowProducerPlusIndependentTransfer", NodeId{0}};
  writer.loadBaseRelationPartitioning(partitions.getPartitioning());
  writer.buildTasks();

  return writer.getProblem();
}
//----------------------------------------------------------------------------------------------------------------------
static TaskAssignment makeIndependentTransferOnlyAssignment(const AssignmentProblem& problem) {
  GreedyPlanner1 planner;
  TaskAssignment assignment = planner.assignTasks(problem);

  REQUIRE(assignment.locatedTasks.size() == 3);

  for (auto& locatedTask : assignment.locatedTasks) {
    locatedTask.node = NodeId{0};
  }

  assignment.transfers.clear();
  assignment.shuffleTargets.clear();

  const DataUnit& baseB = problem.plan.dataUnits[2];

  assignment.transfers.push_back(makeTransfer(Partition{baseB.id, 0}, NodeId{0}, NodeId{1}));

  return assignment;
}
//----------------------------------------------------------------------------------------------------------------------
static TaskAssignment makeIndependentTransferPlusStreamedProducerTransferAssignment(const AssignmentProblem& problem) {
  TaskAssignment assignment = makeIndependentTransferOnlyAssignment(problem);

  // producedA is dataUnits[1] in the fixture above.
  const DataUnit& producedA = problem.plan.dataUnits[1];

  assignment.transfers.push_back(makeTransfer(Partition{producedA.id, 0}, NodeId{0}, NodeId{1}));

  return assignment;
}
//----------------------------------------------------------------------------------------------------------------------
}  // namespace
//----------------------------------------------------------------------------------------------------------------------
TEST_CASE("BlackBox_SingleLocalTask_HasExpectedAbsoluteRuntime") {
  AssignmentProblem problem = makeSingleLocalTaskProblem();
  TaskAssignment assignment = getSingleTaskAssignment(problem, NodeId{0});

  const auto& locatedTask = assignment.locatedTasks[0];
  const double expected = problem.plan.estimateRuntime(problem.cluster.nodes[locatedTask.node].nCores,
                                                       locatedTask.task.pipeline, {}, {}, {});

  Simulator simulator(&problem);

  for (bool streaming : {false, true}) {
    for (const auto& settings : representativeSettings(streaming)) {
      INFO("streaming: " << settings.allowStreamingInput);
      INFO("limit step size: " << settings.limitStepSize);

      Simulator::Result result = simulator.simulate(assignment, settings);

      CHECK(result.done);
      CHECK(result.abortMessage.empty());
      CHECK(result.executionTime == doctest::Approx(expected));
    }
  }
}
//----------------------------------------------------------------------------------------------------------------------
TEST_CASE("BlackBox_StreamingAndNonStreamingAgreeWithoutTransfersOrShuffles") {
  AssignmentProblem problem = makeSingleLocalTaskProblem();
  TaskAssignment assignment = getSingleTaskAssignment(problem, NodeId{0});

  Simulator simulator(&problem);

  for (bool limitedSteps : {false, true}) {
    INFO("limit step size: " << limitedSteps);

    Simulator::Result nonStreaming = simulator.simulate(assignment, makeSettings(false, limitedSteps));
    Simulator::Result streaming = simulator.simulate(assignment, makeSettings(true, limitedSteps));

    CHECK(nonStreaming.done);
    CHECK(nonStreaming.abortMessage.empty());

    CHECK(streaming.done);
    CHECK(streaming.abortMessage.empty());

    CHECK(streaming.executionTime == doctest::Approx(nonStreaming.executionTime));
  }
}
//----------------------------------------------------------------------------------------------------------------------
TEST_CASE("BlackBox_SingleTaskThenTransfer_StreamingOverlapsComputeAndTransfer") {
  AssignmentProblem problem = makeSingleTaskThenTransferProblem();
  TaskAssignment assignment = makeSingleTaskThenTransferAssignment(problem);

  Simulator simulator(&problem);

  for (bool limitedSteps : {false, true}) {
    INFO("limit step size: " << limitedSteps);

    Simulator::Result nonStreaming = simulator.simulate(assignment, makeSettings(false, limitedSteps));
    Simulator::Result streaming = simulator.simulate(assignment, makeSettings(true, limitedSteps));

    CHECK(nonStreaming.done);
    CHECK(nonStreaming.abortMessage.empty());

    CHECK(streaming.done);
    CHECK(streaming.abortMessage.empty());

    CHECK(nonStreaming.executionTime > 0.0);
    CHECK(streaming.executionTime > 0.0);

    CHECK(streaming.executionTime <= nonStreaming.executionTime);
  }
}
//----------------------------------------------------------------------------------------------------------------------
TEST_CASE("BlackBox_TwoActiveTransfersOnSameLinkTakeLongerThanOne") {
  AssignmentProblem problem = makeTwoIndependentTransfersProblem();

  TaskAssignment oneTransfer = makeOneBaseTransferAssignment(problem);
  TaskAssignment twoTransfers = makeTwoBaseTransfersAssignment(problem);

  Simulator simulator(&problem);

  for (bool streaming : {false, true}) {
    for (const auto& settings : representativeSettings(streaming)) {
      INFO("streaming: " << settings.allowStreamingInput);
      INFO("limit step size: " << settings.limitStepSize);

      Simulator::Result one = simulator.simulate(oneTransfer, settings);
      Simulator::Result two = simulator.simulate(twoTransfers, settings);

      CHECK(one.done);
      CHECK(one.abortMessage.empty());

      CHECK(two.done);
      CHECK(two.abortMessage.empty());

      CHECK(one.executionTime > 0.0);
      CHECK(two.executionTime > one.executionTime);
    }
  }
}
//----------------------------------------------------------------------------------------------------------------------
TEST_CASE("BlackBox_LocalShuffleAddsNoNetworkRuntime") {
  AssignmentProblem problem = makeSingleLocalShuffleProblem();
  TaskAssignment assignment = makeSingleLocalShuffleAssignment(problem);

  const auto& locatedTask = assignment.locatedTasks[0];
  const double expectedTaskTime = problem.plan.estimateRuntime(problem.cluster.nodes[locatedTask.node].nCores,
                                                               locatedTask.task.pipeline, {}, {}, {});

  Simulator simulator(&problem);

  for (bool streaming : {false, true}) {
    for (const auto& settings : representativeSettings(streaming)) {
      INFO("streaming: " << settings.allowStreamingInput);
      INFO("limit step size: " << settings.limitStepSize);

      Simulator::Result result = simulator.simulate(assignment, settings);

      CHECK(result.done);
      CHECK(result.abortMessage.empty());

      if (!settings.limitStepSize) {
        CHECK(result.executionTime == doctest::Approx(expectedTaskTime));
      } else {
        CHECK(result.executionTime >= expectedTaskTime - 1e-12);
        CHECK(result.executionTime <= expectedTaskTime + settings.stepLimit + 1e-12);
      }
    }
  }
}
//----------------------------------------------------------------------------------------------------------------------
TEST_CASE("BlackBox_WaitingStreamedTransferDoesNotCauseLargeIndependentTransferSlowdown") {
  AssignmentProblem problem = makeSlowProducerPlusIndependentTransferProblem();

  TaskAssignment baseline = makeIndependentTransferOnlyAssignment(problem);
  TaskAssignment withStreamedProducerTransfer = makeIndependentTransferPlusStreamedProducerTransferAssignment(problem);

  Simulator simulator(&problem);

  for (bool limitedSteps : {false, true}) {
    INFO("limit step size: " << limitedSteps);

    auto settings = makeSettings(true, limitedSteps);

    Simulator::Result base = simulator.simulate(baseline, settings);
    Simulator::Result withExtra = simulator.simulate(withStreamedProducerTransfer, settings);

    CHECK(base.done);
    CHECK(base.abortMessage.empty());

    CHECK(withExtra.done);
    CHECK(withExtra.abortMessage.empty());

    CHECK(base.executionTime > 0.0);
    CHECK(withExtra.executionTime > 0.0);

    // The extra streamed transfer must finish, so it may increase total runtime.
    CHECK(withExtra.executionTime >= base.executionTime);

    // But it should not behave as if a waiting streamed transfer continuously consumes
    // bandwidth while blocked on producer input.
    CHECK(withExtra.executionTime < base.executionTime * 1.75);
  }
}
//----------------------------------------------------------------------------------------------------------------------

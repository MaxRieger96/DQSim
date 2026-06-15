#include <json.hpp>
#include <optional>
#include <vector>

#include "src/benchmark/ClusterBuilder.hpp"
#include "src/benchmark/DUBuilder.hpp"
#include "src/distributedplan/ProblemWriter.hpp"
#include "thirdparty/doctest.h"

TEST_CASE("TestProblemWriter") {
  using namespace dqsim;

  Cluster cluster = ClusterBuilder::get2Big();

  DUBuilder duBuilder;

  std::vector<DUBuilder::Attribute> attrs{{"x", 8}};

  DataUnit r1 = duBuilder.buildDU("R1", 1e7, 4, false, attrs, 0, false, false);
  DataUnit i1 = duBuilder.buildDU("result", 1e7, 4, true, attrs, 0, false, false);
  DataUnit result = duBuilder.buildDU("result", 1e7, 1, true, attrs, 0, true, false);

  Pipeline p1{PipelineId{0}, r1.id, i1.id, {}, 8, "build hashtable"};
  ShuffleStage s1{i1.id, result.id};

  DistributedQueryPlan plan{duBuilder.getDUs(), {p1}, {s1}, {PipelineId{0}}};
  ProblemWriter writer(plan, cluster, "", std::nullopt);
  writer.buildTasks();
  AssignmentProblem problem1 = writer.getProblem();

  nlohmann::json json = problem1.toJson();
  AssignmentProblem problem2(json);
  CHECK(problem1 == problem2);
}

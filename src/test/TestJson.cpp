#include <json.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "src/Cluster.hpp"
#include "src/SimulatorSettings.hpp"
#include "src/benchmark/BenchmarkDefinition.hpp"
#include "src/benchmark/ClusterBuilder.hpp"
#include "src/benchmark/DUBuilder.hpp"
#include "src/distributedplan/DataUnit.hpp"
#include "src/distributedplan/DistributedQueryPlan.hpp"
#include "src/distributedplan/Pipeline.hpp"
#include "src/distributedplan/ProblemWriter.hpp"
#include "src/partitioning/Attribute.hpp"
#include "src/partitioning/Partition.hpp"
#include "src/partitioning/PartitionLayout.hpp"
#include "src/partitioning/PartitionManager.hpp"
#include "src/tasks/Task.hpp"
#include "thirdparty/doctest.h"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2025 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
using namespace dqsim;

/// Serialize an object to JSON, deserialize it, and return the result.
template <typename T>
T roundTrip(const T& obj) {
  return T(obj.toJson());
}

/// For types without operator==: verify that two serialization passes produce identical JSON.
template <typename T>
bool stableJson(const T& obj) {
  nlohmann::json j1 = obj.toJson();
  T copy(j1);
  nlohmann::json j2 = copy.toJson();
  return j1 == j2;
}

/// Build a Node without relying on aggregate initialization (Node has a user-declared json constructor).
static Node makeNode(uint64_t nCores, double memorySize, double networkSpeed, double networkLatency) {
  Node n;
  n.nCores = nCores;
  n.memorySize = memorySize;
  n.networkSpeed = networkSpeed;
  n.networkLatency = networkLatency;
  return n;
}
//----------------------------------------------------------------------------------------------------------------------
TEST_CASE("JsonRoundTrip_Node") {
  Node n = makeNode(16, 256.5, 100.0, 0.0005);
  Node n2 = roundTrip(n);
  CHECK(n == n2);
}
//----------------------------------------------------------------------------------------------------------------------
TEST_CASE("JsonRoundTrip_Cluster") {
  SUBCASE("compute_nodes_only") {
    std::vector<Node> nodes{
        makeNode(8, 64, 25, 0.001),
        makeNode(4, 32, 10, 0.002),
        makeNode(16, 128, 100, 0.0005),
    };
    Cluster c{std::span<Node>{nodes}};
    Cluster c2 = roundTrip(c);
    CHECK(c == c2);
  }
  SUBCASE("with_storage_service") {
    std::vector<Node> nodes{makeNode(8, 64, 25, 0.001), makeNode(0, 0, 1000, 0)};
    Cluster c{std::span<Node>{nodes}, /*storageService=*/true};
    Cluster c2 = roundTrip(c);
    CHECK(c == c2);
  }
}
//----------------------------------------------------------------------------------------------------------------------
TEST_CASE("JsonRoundTrip_Attribute") {
  Attribute a{"customer_id", DataUnitId{3}, 2, 8.0};
  Attribute a2 = roundTrip(a);
  CHECK(a == a2);
}
//----------------------------------------------------------------------------------------------------------------------
TEST_CASE("JsonRoundTrip_Partition") {
  Partition p{DataUnitId{7}, 13};
  Partition p2 = roundTrip(p);
  CHECK(p == p2);
}
//----------------------------------------------------------------------------------------------------------------------
TEST_CASE("JsonRoundTrip_LocatedPartition") {
  LocatedPartition lp{Partition{DataUnitId{2}, 5}, NodeId{3}};
  LocatedPartition lp2 = roundTrip(lp);
  CHECK(lp == lp2);
}
//----------------------------------------------------------------------------------------------------------------------
TEST_CASE("JsonRoundTrip_PartitionLayout") {
  SUBCASE("scattered") {
    PartitionLayout pl{PartitionLayoutType::Scattered, 8, {}};
    PartitionLayout pl2 = roundTrip(pl);
    CHECK(pl == pl2);
  }
  SUBCASE("hash_partitioned") {
    Attribute a{"order_key", DataUnitId{1}, 0, 4.0};
    Attribute b{"line_number", DataUnitId{1}, 1, 4.0};
    PartitionLayout pl{PartitionLayoutType::HashPartitioned, 16, {a, b}};
    PartitionLayout pl2 = roundTrip(pl);
    CHECK(pl == pl2);
  }
  SUBCASE("single_node") {
    PartitionLayout pl{PartitionLayoutType::SingleNode, 1, {}};
    PartitionLayout pl2 = roundTrip(pl);
    CHECK(pl == pl2);
  }

  SUBCASE("broadcast") {
    PartitionLayout pl{PartitionLayoutType::Broadcast, 4, {}};
    PartitionLayout pl2 = roundTrip(pl);
    CHECK(pl == pl2);
  }
}
//----------------------------------------------------------------------------------------------------------------------
TEST_CASE("JsonRoundTrip_DataUnit") {
  Attribute attr{"price", DataUnitId{0}, 0, 8.0};
  SUBCASE("with_partition_layout") {
    PartitionLayout layout{PartitionLayoutType::Scattered, 4, {}};
    DataUnit du{"lineitem", {attr}, layout, DataUnitId{0}, 6e8, false};
    DataUnit du2 = roundTrip(du);
    CHECK(du == du2);
  }
  SUBCASE("single_node_result") {
    PartitionLayout layout{PartitionLayoutType::SingleNode, 1, {}};
    DataUnit du{"result", {attr}, layout, DataUnitId{2}, 500.0, true};
    DataUnit du2 = roundTrip(du);
    CHECK(du == du2);
  }
}
//----------------------------------------------------------------------------------------------------------------------
TEST_CASE("JsonRoundTrip_Pipeline") {
  SUBCASE("no_required_data") {
    Pipeline p{PipelineId{0}, DataUnitId{0}, DataUnitId{1}, {}, 0.75, "scan and project"};
    Pipeline p2 = roundTrip(p);
    CHECK(p == p2);
  }
  SUBCASE("with_required_data") {
    Pipeline p{PipelineId{2}, DataUnitId{1}, DataUnitId{3}, {DataUnitId{0}, DataUnitId{2}}, 1.5, "hash join"};
    Pipeline p2 = roundTrip(p);
    CHECK(p == p2);
  }
}
//----------------------------------------------------------------------------------------------------------------------
TEST_CASE("JsonRoundTrip_ShuffleStage") {
  ShuffleStage s{DataUnitId{1}, DataUnitId{2}};
  ShuffleStage s2 = roundTrip(s);
  CHECK(s == s2);
}
//----------------------------------------------------------------------------------------------------------------------
TEST_CASE("JsonRoundTrip_Task") {
  SUBCASE("without_required_partitions") {
    Task t{PipelineId{1}, Partition{DataUnitId{0}, 3}, {}, Partition{DataUnitId{1}, 3}, false};
    Task t2 = roundTrip(t);
    CHECK(t == t2);
  }
  SUBCASE("with_required_partitions_and_broadcast") {
    Task t{PipelineId{2},
           Partition{DataUnitId{0}, 0},
           {Partition{DataUnitId{1}, 0}, Partition{DataUnitId{2}, 0}},
           Partition{DataUnitId{3}, 0},
           true};
    Task t2 = roundTrip(t);
    CHECK(t == t2);
  }
}
//----------------------------------------------------------------------------------------------------------------------
TEST_CASE("JsonRoundTrip_DistributedQueryPlan") {
  DUBuilder builder;
  std::vector<DUBuilder::Attribute> attrs{{"key", 8}};
  DataUnit r1 = builder.buildDU("R1", 1e7, 4, false, attrs, 0, false, false);
  DataUnit i1 = builder.buildDU("i1", 1e7, 4, true, attrs, 0, false, false);
  DataUnit res = builder.buildDU("result", 1e7, 1, true, attrs, 0, true, false);

  Pipeline p1{PipelineId{0}, r1.id, i1.id, {}, 0.5, "scan R1"};
  ShuffleStage s1{i1.id, res.id};

  DistributedQueryPlan plan{builder.getDUs(), {p1}, {s1}, {PipelineId{0}}};
  DistributedQueryPlan plan2 = roundTrip(plan);
  CHECK(plan == plan2);
}
//----------------------------------------------------------------------------------------------------------------------
TEST_CASE("JsonRoundTrip_Partitioning") {
  SUBCASE("empty") {
    Partitioning pt{{}, {}, 3};
    Partitioning pt2 = roundTrip(pt);
    CHECK(pt == pt2);
  }
  SUBCASE("with_assignments_and_cache") {
    Partition p0{DataUnitId{0}, 0};
    Partition p1{DataUnitId{0}, 1};
    Partition p2{DataUnitId{1}, 0};

    std::unordered_map<Partition, NodeId> owning{
        {p0, NodeId{0}},
        {p1, NodeId{1}},
        {p2, NodeId{2}},
    };
    std::unordered_map<Partition, std::unordered_set<NodeId>> caching{
        {p0, {NodeId{1}, NodeId{2}}},
        {p1, {}},
    };
    Partitioning pt{std::move(owning), std::move(caching), 3};
    Partitioning pt2 = roundTrip(pt);
    CHECK(pt == pt2);
  }
}
//----------------------------------------------------------------------------------------------------------------------
TEST_CASE("JsonRoundTrip_AssignmentProblem") {
  Cluster cluster = ClusterBuilder::get2Big();

  DUBuilder builder;
  std::vector<DUBuilder::Attribute> attrs{{"x", 8}};
  DataUnit r1 = builder.buildDU("R1", 1e7, 4, false, attrs, 0, false, false);
  DataUnit i1 = builder.buildDU("i1", 1e7, 4, true, attrs, 0, false, false);
  DataUnit res = builder.buildDU("result", 1e7, 1, true, attrs, 0, true, false);

  Pipeline p1{PipelineId{0}, r1.id, i1.id, {}, 8.0, "scan R1"};
  ShuffleStage s1{i1.id, res.id};

  DistributedQueryPlan plan{builder.getDUs(), {p1}, {s1}, {PipelineId{0}}};
  ProblemWriter writer(plan, cluster, "test_problem", std::nullopt);
  writer.buildTasks();
  AssignmentProblem problem = writer.getProblem();

  AssignmentProblem problem2 = roundTrip(problem);
  CHECK(problem == problem2);
}
//----------------------------------------------------------------------------------------------------------------------
TEST_CASE("JsonRoundTrip_AssignmentProblem_WithResultNode") {
  Cluster cluster = ClusterBuilder::get2Big();

  DUBuilder builder;
  std::vector<DUBuilder::Attribute> attrs{{"x", 8}};
  DataUnit r1 = builder.buildDU("R1", 1e7, 4, false, attrs, 0, false, false);
  DataUnit i1 = builder.buildDU("i1", 1e7, 4, true, attrs, 0, false, false);
  DataUnit res = builder.buildDU("result", 1e7, 1, true, attrs, 0, true, false);

  Pipeline p1{PipelineId{0}, r1.id, i1.id, {}, 8.0, "scan R1"};
  ShuffleStage s1{i1.id, res.id};

  DistributedQueryPlan plan{builder.getDUs(), {p1}, {s1}, {PipelineId{0}}};
  ProblemWriter writer(plan, cluster, "test_with_result_node", NodeId{0});
  writer.buildTasks();
  AssignmentProblem problem = writer.getProblem();

  AssignmentProblem problem2 = roundTrip(problem);
  CHECK(problem == problem2);
  REQUIRE(problem2.resultNode.has_value());  // guards the .value() call below
  CHECK(problem2.resultNode.value() == NodeId{0});
}
//----------------------------------------------------------------------------------------------------------------------
TEST_CASE("JsonRoundTrip_TaskAssignment_LocatedTask") {
  Task t{PipelineId{3}, Partition{DataUnitId{1}, 2}, {}, Partition{DataUnitId{2}, 2}, false};
  TaskAssignment::LocatedTask lt{t, NodeId{1}};
  CHECK(stableJson(lt));
  // Also verify field values survive the round-trip
  TaskAssignment::LocatedTask lt2(lt.toJson());
  CHECK(lt2.node == NodeId{1});
  CHECK(lt2.task == t);
}
//----------------------------------------------------------------------------------------------------------------------
TEST_CASE("JsonRoundTrip_TaskAssignment_Transfer") {
  TaskAssignment::Transfer tr{Partition{DataUnitId{0}, 1}, NodeId{0}, NodeId{2}};
  CHECK(stableJson(tr));
  TaskAssignment::Transfer tr2(tr.toJson());
  CHECK(tr2.partition == Partition{DataUnitId{0}, 1});
  CHECK(tr2.from == NodeId{0});
  CHECK(tr2.to == NodeId{2});
}
//----------------------------------------------------------------------------------------------------------------------
TEST_CASE("JsonRoundTrip_TaskAssignment_ShuffleTarget") {
  TaskAssignment::ShuffleTarget st{Partition{DataUnitId{1}, 3}, NodeId{2}};
  CHECK(stableJson(st));
  TaskAssignment::ShuffleTarget st2(st.toJson());
  CHECK(st2.partition == Partition{DataUnitId{1}, 3});
  CHECK(st2.location == NodeId{2});
}
//----------------------------------------------------------------------------------------------------------------------
TEST_CASE("JsonRoundTrip_TaskAssignment") {
  Task t0{PipelineId{0}, Partition{DataUnitId{0}, 0}, {}, Partition{DataUnitId{1}, 0}, false};
  Task t1{PipelineId{0}, Partition{DataUnitId{0}, 1}, {}, Partition{DataUnitId{1}, 1}, false};

  TaskAssignment ta{
      /*locatedTasks=*/{
          TaskAssignment::LocatedTask{t0, NodeId{0}},
          TaskAssignment::LocatedTask{t1, NodeId{1}},
      },
      /*transfers=*/
      {
          TaskAssignment::Transfer{Partition{DataUnitId{2}, 0}, NodeId{0}, NodeId{1}},
      },
      /*shuffleTargets=*/
      {
          TaskAssignment::ShuffleTarget{Partition{DataUnitId{3}, 0}, NodeId{0}},
          TaskAssignment::ShuffleTarget{Partition{DataUnitId{3}, 1}, NodeId{1}},
      },
  };
  CHECK(stableJson(ta));
}
//----------------------------------------------------------------------------------------------------------------------
TEST_CASE("JsonRoundTrip_SimulatorSettings") {
  SUBCASE("default") {
    SimulatorSettings s = SimulatorSettings::getDefault();
    CHECK(stableJson(s));
  }

  SUBCASE("non_default") {
    SimulatorSettings s;
    s.nParallelTasks = 4;
    s.allowStreamingInput = true;
    s.minimumBlockSize = 512.0 * 1024.0;
    s.taskSlowdownFactors = std::vector<std::vector<double>>{{1.0, 1.0, 1.0}, {0.5, 1.5}};

    CHECK(stableJson(s));

    SimulatorSettings s2(s.toJson());
    CHECK(s2.nParallelTasks == 4);
    CHECK(s2.allowStreamingInput == true);
    CHECK(s2.minimumBlockSize == 512.0 * 1024.0);

    REQUIRE(s2.taskSlowdownFactors.has_value());
    CHECK(*s2.taskSlowdownFactors == *s.taskSlowdownFactors);
  }
}
//----------------------------------------------------------------------------------------------------------------------
TEST_CASE("JsonRoundTrip_ClusterBuilderDefinition") {
  SUBCASE("single_node_type") {
    ClusterBuilderDefinition def{8, false, {"18xlarge"}};
    CHECK(stableJson(def));
    ClusterBuilderDefinition def2(def.toJson());
    CHECK(def2.nNodes == 8);
    CHECK(def2.storageService == false);
    CHECK(def2.nodeTypes == std::vector<std::string>{"18xlarge"});
  }
  SUBCASE("mixed_node_types_with_storage") {
    ClusterBuilderDefinition def{4, true, {"18xlarge", "9xlarge"}};
    CHECK(stableJson(def));
    ClusterBuilderDefinition def2(def.toJson());
    CHECK(def2.nNodes == 4);
    CHECK(def2.storageService == true);
    CHECK(def2.nodeTypes == (std::vector<std::string>{"18xlarge", "9xlarge"}));
  }
}
//----------------------------------------------------------------------------------------------------------------------
TEST_CASE("JsonRoundTrip_BenchmarkDefinition") {
  BenchmarkDefinition bd;
  bd.name = "my_benchmark";
  bd.clusters = {
      ClusterBuilderDefinition{4, true, {"18xlarge"}},
      ClusterBuilderDefinition{8, true, {"18xlarge"}},
  };
  bd.partitionCounts = {4, 8, 16};
  bd.queryCategories = {QueryCategory::TPCH, QueryCategory::Handcrafted};
  bd.partitioners = {PartitionerType::RRAll, PartitionerType::Rand1};
  bd.planners = {PlannerType::HEFT, PlannerType::Greedy1};
  bd.nParallelTasks = 32;
  bd.taskLimit = std::optional<uint64_t>{500};

  CHECK(stableJson(bd));

  BenchmarkDefinition bd2(bd.toJson());
  CHECK(bd2.name == "my_benchmark");
  CHECK(bd2.partitionCounts == (std::vector<uint64_t>{4, 8, 16}));
  CHECK(bd2.nParallelTasks == 32);
  CHECK(bd2.taskLimit == std::optional<uint64_t>{500});
  CHECK(bd2.clusters.size() == 2);
}
//----------------------------------------------------------------------------------------------------------------------
TEST_CASE("JsonRoundTrip_BenchmarkDefinition_NoTaskLimit") {
  BenchmarkDefinition bd;
  bd.name = "no_limit";
  bd.clusters = {ClusterBuilderDefinition{2, false, {"xlarge"}}};
  bd.partitionCounts = {2};
  bd.queryCategories = {QueryCategory::Growing};
  bd.partitioners = {PartitionerType::One};
  bd.planners = {PlannerType::NetHEFT};
  bd.nParallelTasks = 1;
  bd.taskLimit = std::nullopt;

  CHECK(stableJson(bd));
  BenchmarkDefinition bd2(bd.toJson());
  CHECK(bd2.taskLimit == std::nullopt);
}
//----------------------------------------------------------------------------------------------------------------------
TEST_CASE("JsonRoundTrip_BaseRelationLayoutDefinition") {
  // BaseRelationLayoutDefinition::Layout has no default constructor,
  // so we build the object via its JSON constructor.
  nlohmann::json j;
  j["layouts"] = nlohmann::json::array();

  nlohmann::json l1;
  l1["name"] = "orders";
  l1["type"] = "hashpartitioned";
  l1["nPartitions"] = 8;
  l1["attributes"] = nlohmann::json::array({"o_orderkey"});
  j["layouts"].push_back(l1);

  nlohmann::json l2;
  l2["name"] = "lineitem";
  l2["type"] = "scattered";
  l2["nPartitions"] = 16;
  l2["attributes"] = nlohmann::json::array();
  j["layouts"].push_back(l2);

  nlohmann::json l3;
  l3["name"] = "customer";
  l3["type"] = "singlenode";
  l3["nPartitions"] = 1;
  l3["attributes"] = nlohmann::json::array();
  j["layouts"].push_back(l3);

  BaseRelationLayoutDefinition brl(j);
  CHECK(stableJson(brl));

  // Verify layout lookup works after round-trip
  BaseRelationLayoutDefinition brl2(brl.toJson());

  const auto* orders = brl2.getLayout("orders");
  REQUIRE(orders != nullptr);  // guards the dereferences below
  CHECK(orders->nPartitions == 8);
  CHECK(orders->type == PartitionLayoutType::HashPartitioned);
  CHECK(orders->attributes == std::vector<std::string>{"o_orderkey"});

  const auto* lineitem = brl2.getLayout("lineitem");
  REQUIRE(lineitem != nullptr);  // guards the dereferences below
  CHECK(lineitem->nPartitions == 16);
  CHECK(lineitem->type == PartitionLayoutType::Scattered);

  const auto* customer = brl2.getLayout("customer");
  REQUIRE(customer != nullptr);  // guards the dereferences below
  CHECK(customer->type == PartitionLayoutType::SingleNode);
}
//----------------------------------------------------------------------------------------------------------------------

#include <json.hpp>

#include "src/partitioning/PartitionManager.hpp"
#include "thirdparty/doctest.h"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2022 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
using namespace dqsim;

static Node makeNode(uint64_t nCores, double memorySize, double networkSpeed, double networkLatency) {
  Node n;
  n.nCores = nCores;
  n.memorySize = memorySize;
  n.networkSpeed = networkSpeed;
  n.networkLatency = networkLatency;
  return n;
}

TEST_CASE("TestPartitionManager") {
  std::vector<Node> nodes{
      makeNode(8, 64, 1e9, 10),
      makeNode(8, 64, 1e9, 10),
      makeNode(8, 64, 1e9, 10),
      makeNode(4, 64, 1e9, 1),
  };
  Cluster cluster{std::span<Node>{nodes}};
  PartitionManager partitionManager(cluster);
  Partition p1{DataUnitId{0}, 0};
  Partition p2{DataUnitId{0}, 1};

  // assign p1 to node 0
  partitionManager.registerPartition(p1, NodeId{0});
  REQUIRE(partitionManager.getOwningNode().find(p1)->second == NodeId{0});
  REQUIRE(partitionManager.getOwnedPartitions().find(NodeId{0})->second == std::unordered_set<Partition>{p1});

  // assign p2 to node 0
  partitionManager.registerPartition(p2, NodeId{0});
  REQUIRE(partitionManager.getOwningNode().find(p2)->second == NodeId{0});
  REQUIRE(partitionManager.getOwnedPartitions().find(NodeId{0})->second == std::unordered_set<Partition>{p1, p2});

  // move p1 to node 1
  partitionManager.registerPartition(p1, NodeId{1});
  REQUIRE(partitionManager.getOwningNode().find(p1)->second == NodeId{1});
  REQUIRE(partitionManager.getOwningNode().find(p2)->second == NodeId{0});
  REQUIRE(partitionManager.getOwnedPartitions().find(NodeId{0})->second == std::unordered_set<Partition>{p2});
  REQUIRE(partitionManager.getOwnedPartitions().find(NodeId{1})->second == std::unordered_set<Partition>{p1});

  // cache p1 on node 0
  partitionManager.cachePartition(p1, NodeId{0});
  REQUIRE(partitionManager.getCachingNodes().find(p1) != partitionManager.getCachingNodes().end());
  REQUIRE(partitionManager.getCachingNodes().find(p1)->second == std::unordered_set<NodeId>{NodeId{0}});
  REQUIRE(partitionManager.getCachedPartitions().find(NodeId{0})->second == std::unordered_set<Partition>{p1});

  {  // check serialization round-trip — independent of subsequent operations
    Partitioning partitioning = partitionManager.getPartitioning();
    nlohmann::json j = partitioning.toJson();
    Partitioning partitioning2(j);
    CHECK(partitioning == partitioning2);
    PartitionManager manager2 = PartitionManager::loadPartitioning(partitioning2);
    CHECK(partitionManager == manager2);
  }

  // evict p1 from cache on node 0
  partitionManager.evictPartitionCache(p1, NodeId{0});
  CHECK(partitionManager.getCachingNodes().find(p1)->second.empty());
  CHECK(partitionManager.getCachedPartitions().find(NodeId{0})->second.empty());

  // remove partitions — final state checks, all independent
  partitionManager.deregisterPartition(p1);
  partitionManager.deregisterDataUnit(p1.dataUnitId);
  CHECK(partitionManager.getOwningNode().empty());
  CHECK(partitionManager.getCachingNodes().empty());
  for (NodeId iNode{0}; iNode < nodes.size(); ++iNode) {
    CHECK(partitionManager.getOwnedPartitions().find(iNode)->second.empty());
    CHECK(partitionManager.getCachedPartitions().find(iNode)->second.empty());
  }
}
//----------------------------------------------------------------------------------------------------------------------

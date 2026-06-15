#include "Partitioner.hpp"

#include <algorithm>
#include <boost/random/mersenne_twister.hpp>
#include <boost/random/uniform_int_distribution.hpp>
#include <cassert>
#include <cstdint>
#include <random>
#include <vector>

#include "src/Cluster.hpp"
#include "src/CustomTypes.hpp"
#include "src/distributedplan/DataUnit.hpp"
#include "src/partitioning/PartitionManager.hpp"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2022 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
namespace {
//----------------------------------------------------------------------------------------------------------------------
struct RandomSource {
  boost::random::mt19937 mt;
  boost::random::uniform_int_distribution<uint64_t> ui;
  explicit RandomSource(const Cluster& cluster) : mt(), ui(0, cluster.getComputeNodes().size()) {}
  NodeId getRandomNode() { return NodeId{ui(mt)}; }
};
//----------------------------------------------------------------------------------------------------------------------
}  // namespace
//----------------------------------------------------------------------------------------------------------------------
void RoundRobinPartitioner::assignPartitions(PartitionManager& partitionManager, const Cluster& cluster,
                                             uint64_t nPartitions, const DataUnit& du) {
  std::vector<NodeId> nodeMapping;
  nodeMapping.reserve(cluster.getComputeNodes().size());
  for (NodeId n{0}; n < cluster.getComputeNodes().size(); ++n) {
    nodeMapping.push_back(n);
  }
  if (remapNodes) {
    std::mt19937 mt;
    std::shuffle(nodeMapping.begin(), nodeMapping.end(), mt);
  }
  uint64_t nNodes = 0;
  switch (cacheState) {
    case CacheState::All:
      nNodes = cluster.getComputeNodes().size();
      break;
    case CacheState::Half:
      nNodes = cluster.getComputeNodes().size() / 2;
      break;
    case CacheState::One:
      nNodes = 1;
      break;
    case CacheState::None:
      nNodes = 0;
  }
  if (nNodes == 0) {
    assert(cluster.getComputeNodes().size() < cluster.nodes.size());
    assert(cluster.storageService);
    NodeId iStorage{cluster.getComputeNodes().size()};
    if (du.partitionLayout->isPartitioned()) {
      for (uint64_t iPartition = 0; iPartition < nPartitions; ++iPartition) {
        partitionManager.registerPartition({du.id, iPartition}, iStorage);
      }
    } else {
      partitionManager.registerPartition({du.id, 0}, iStorage);
    }
    return;
  }
  assert(nNodes != 0);
  if (du.partitionLayout->isPartitioned()) {
    for (uint64_t iPartition = 0; iPartition < nPartitions; ++iPartition) {
      partitionManager.registerPartition({du.id, iPartition}, nodeMapping[iPartition % nNodes]);
    }
  } else {
    partitionManager.registerPartition({du.id, 0}, nodeMapping[0]);
  }
  // Always assign to storage node if it exists
  if (cluster.storageService) {
    NodeId iStorage{cluster.getComputeNodes().size()};
    if (du.partitionLayout->isPartitioned()) {
      for (uint64_t iPartition = 0; iPartition < nPartitions; ++iPartition) {
        partitionManager.cachePartition({du.id, iPartition}, iStorage);
      }
    } else {
      partitionManager.cachePartition({du.id, 0}, iStorage);
    }
  }
}
//----------------------------------------------------------------------------------------------------------------------
void FullPartitioner::assignPartitions(PartitionManager& partitionManager, const Cluster& cluster, uint64_t nPartitions,
                                       const DataUnit& du) {
  if (du.partitionLayout->isPartitioned()) {
    for (uint64_t iPartition = 0; iPartition < nPartitions; ++iPartition) {
      partitionManager.registerPartition({du.id, iPartition}, NodeId{0});
      for (NodeId n{1}; n < cluster.getComputeNodes().size(); ++n) {
        partitionManager.cachePartition({du.id, iPartition}, n);
      }
    }
  } else {
    partitionManager.registerPartition({du.id, 0}, NodeId{0});
  }
  // Always assign to storage node if it exists
  if (cluster.storageService) {
    NodeId iStorage{cluster.getComputeNodes().size()};
    if (du.partitionLayout->isPartitioned()) {
      for (uint64_t iPartition = 0; iPartition < nPartitions; ++iPartition) {
        partitionManager.cachePartition({du.id, iPartition}, iStorage);
      }
    } else {
      partitionManager.cachePartition({du.id, 0}, iStorage);
    }
  }
}
//----------------------------------------------------------------------------------------------------------------------
void RandomPartitioner::assignPartitions(PartitionManager& partitionManager, const Cluster& cluster,
                                         uint64_t nPartitions, const DataUnit& du) {
  RandomSource randomSource(cluster);
  if (du.partitionLayout->isPartitioned()) {
    for (uint64_t iPartition = 0; iPartition < nPartitions; ++iPartition) {
      partitionManager.registerPartition({du.id, iPartition}, randomSource.getRandomNode());
      for (uint64_t i = 0; i < nDuplicates; ++i) {
        auto node = randomSource.getRandomNode();
        if (!partitionManager.hasPartition(node, {du.id, iPartition}))
          partitionManager.cachePartition({du.id, iPartition}, node);
      }
    }
  } else {
    partitionManager.registerPartition({du.id, 0}, NodeId{0});
  }
  // Always assign to storage node if it exists
  if (cluster.storageService) {
    NodeId iStorage{cluster.getComputeNodes().size()};
    if (du.partitionLayout->isPartitioned()) {
      for (uint64_t iPartition = 0; iPartition < nPartitions; ++iPartition) {
        if (!partitionManager.hasPartition(iStorage, {du.id, iPartition}))
          partitionManager.cachePartition({du.id, iPartition}, iStorage);
      }
    } else {
      if (!partitionManager.hasPartition(iStorage, {du.id, 0})) partitionManager.cachePartition({du.id, 0}, iStorage);
    }
  }
}
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

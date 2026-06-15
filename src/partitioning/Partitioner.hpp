#pragma once
//----------------------------------------------------------------------------------------------------------------------
#include "src/Cluster.hpp"
#include "src/partitioning/PartitionManager.hpp"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2022 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
class DataUnit;
//----------------------------------------------------------------------------------------------------------------------
/// Assigns partitions to nodes
class Partitioner {
 public:
  /// Assign partitions to nodes
  virtual void assignPartitions(PartitionManager& partitionManager, const Cluster& cluster, uint64_t nPartitions,
                                const DataUnit& dataUnit) = 0;
  /// Destructor
  virtual ~Partitioner() = default;
};
//----------------------------------------------------------------------------------------------------------------------
class RoundRobinPartitioner : public Partitioner {
 public:
  enum class CacheState {
    All,   // All nodes have some data
    Half,  // Half the nodes have some data
    One,   // Only one node has cached data
    None,  // No node has data

  };

 private:
  CacheState cacheState;
  bool remapNodes;

 public:
  /// Default Constructor
  RoundRobinPartitioner() : cacheState(CacheState::All), remapNodes(false) {}
  /// Constructor
  explicit RoundRobinPartitioner(CacheState cacheState, bool remapNodes)
      : cacheState(cacheState), remapNodes(remapNodes) {}
  /// Assign partitions to nodes in a round robin manner
  void assignPartitions(PartitionManager& partitionManager, const Cluster& cluster, uint64_t nPartitions,
                        const DataUnit& dataUnit) override;
};
//----------------------------------------------------------------------------------------------------------------------
/// A partitioner that assigns every partition to every node
class FullPartitioner : public Partitioner {
  /// Assign partitions to nodes
  void assignPartitions(PartitionManager& partitionManager, const Cluster& cluster, uint64_t nPartitions,
                        const DataUnit& dataUnit) override;
};
//----------------------------------------------------------------------------------------------------------------------
/// A partitioner that assigns partitions randomly to the nodes (including the storage node)
class RandomPartitioner : public Partitioner {
 public:
  size_t nDuplicates = 0;
  /// Assign partitions to nodes
  void assignPartitions(PartitionManager& partitionManager, const Cluster& cluster, uint64_t nPartitions,
                        const DataUnit& dataUnit) override;
  /// Default Constructor
  RandomPartitioner() = default;
  /// Constructor
  explicit RandomPartitioner(size_t nDuplicates) : nDuplicates(nDuplicates) {}
};
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

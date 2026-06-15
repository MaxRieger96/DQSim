#pragma once
//----------------------------------------------------------------------------------------------------------------------
#include <optional>

#include "src/Cluster.hpp"
#include "src/distributedplan/DataUnit.hpp"
#include "src/partitioning/Partition.hpp"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2024 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
/// Simulates the memory usage of nodes
class MemorySimulator {
  /// The data units used in this simulation
  std::span<const DataUnit> dataUnits;
  /// The currently free memory on each node in bytes
  std::vector<double> freeMemory;
  /// Compute the size of a partition
  double getPartitionSize(Partition partition, std::optional<double> skewFactor);

 public:
  /// Constructor
  MemorySimulator(std::span<const DataUnit> dataUnits, const Cluster& cluster);
  /// Get the free memory of all nodes
  std::span<const double> getFreeMemory() const;
  /// Allocate size in bytes on node, return false if impossible
  bool allocate(NodeId node, double size);
  /// Free size in bytes on node
  void free(NodeId node, double size);
  /// Store a partition on node, return false if impossible
  bool storePartition(NodeId node, Partition partition, std::optional<double> skewFactor);
  /// Evict a partition from node
  bool evictPartition(NodeId node, Partition partition, std::optional<double> skewFactor);
};
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

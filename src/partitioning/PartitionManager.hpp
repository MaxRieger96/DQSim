#pragma once
//----------------------------------------------------------------------------------------------------------------------
#include <json_fwd.hpp>
#include <unordered_map>
#include <unordered_set>

#include "src/Cluster.hpp"
#include "src/CustomTypes.hpp"
#include "src/partitioning/Partition.hpp"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2022 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
/// A readable and writable container for partition assignment
struct Partitioning {
  /// The responsible node for the partition
  std::unordered_map<Partition, NodeId> owningNode;
  /// The complete set of nodes holding the partition in cache
  std::unordered_map<Partition, std::unordered_set<NodeId>> cachingNodes;
  /// The number of nodes
  uint64_t nNodes;
  /// Constructor
  Partitioning() = default;
  /// Constructor
  Partitioning(std::unordered_map<Partition, NodeId> owningNode,
               std::unordered_map<Partition, std::unordered_set<NodeId>> cachingNodes, std::size_t nNodes);
  /// Equality
  bool operator==(const Partitioning& other) const = default;

  nlohmann::json toJson() const;
  explicit Partitioning(const nlohmann::json& json);
};
//----------------------------------------------------------------------------------------------------------------------
/// Maintains assignments between partitions and nodes
/// Each partition has exactly one responsible node, but may be replicated
/// on several other nodes
class PartitionManager {
  /// The responsible node for the partition
  std::unordered_map<Partition, NodeId> owningNode;
  /// The complete set of nodes holding the partition in cache
  std::unordered_map<Partition, std::unordered_set<NodeId>> cachingNodes;
  /// All partitions a node is responsible for
  std::unordered_map<NodeId, std::unordered_set<Partition>> ownedPartitions;
  /// All partitions a node has cached
  std::unordered_map<NodeId, std::unordered_set<Partition>> cachedPartitions;
  /// All partitions of a data unit
  std::unordered_map<DataUnitId, std::unordered_set<Partition>> dataUnits;

  /// Remove partition from maps but not from data units
  void removePartitionFromMaps(Partition partition);

 public:
  /// Constructor
  explicit PartitionManager(const Cluster& cluster);
  /// Equality
  bool operator==(const PartitionManager& other) const = default;
  /// Assign a (new) responsible node for a partition
  void registerPartition(Partition partition, NodeId node);
  /// Assign a (new) responsible node for all partitions of a du
  void registerDU(DataUnitId du, size_t nPartitions, NodeId node);
  /// Cache a partition at node
  void cachePartition(Partition partition, NodeId node);
  /// Remove partition from cache at node
  void evictPartitionCache(Partition partition, NodeId node);
  /// Remove a partition
  void deregisterPartition(Partition partition);
  /// Remove all partitions of the dataUnit
  void deregisterDataUnit(DataUnitId dataUnitId);

  /// Get owning node
  const std::unordered_map<Partition, NodeId>& getOwningNode() const { return owningNode; };
  /// Get caching nodes
  const std::unordered_map<Partition, std::unordered_set<NodeId>>& getCachingNodes() const { return cachingNodes; };
  /// Get owned partitions
  const std::unordered_map<NodeId, std::unordered_set<Partition>>& getOwnedPartitions() const {
    return ownedPartitions;
  };
  /// Get cached partitions
  const std::unordered_map<NodeId, std::unordered_set<Partition>>& getCachedPartitions() const {
    return cachedPartitions;
  };
  /// Check whether nodes has partition
  bool hasPartition(NodeId node, Partition partition) const;
  /// Check whether a partition is stored on any node
  bool partitionStored(Partition partition) const;
  /// Get all nodes that store the partition (owning and caching)
  std::vector<NodeId> owningAndCachingNodes(Partition p) const;

  /// Get a partitioning object, which can be serialized
  Partitioning getPartitioning() const;
  /// Load partitioning
  static PartitionManager loadPartitioning(const Partitioning& partitioning, const Cluster* cluster = nullptr);

  /// Print the current partition assignment
  void print(std::ostream& stream) const;
};
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

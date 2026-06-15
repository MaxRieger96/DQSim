#pragma once
//----------------------------------------------------------------------------------------------------------------------

#include <json_fwd.hpp>
#include <tuple>

#include "src/CustomTypes.hpp"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2022 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
/// Partition of a data unit
struct Partition {
  /// Id of the data unit
  DataUnitId dataUnitId;
  /// Index of the partition within the data unit
  uint64_t partitionIndex;
  /// Constructor
  Partition() = default;
  /// Constructor
  Partition(DataUnitId dataUnitId, uint64_t partitionIndex) : dataUnitId(dataUnitId), partitionIndex(partitionIndex) {}
  /// Equality
  bool operator==(const Partition& other) const = default;
  auto operator<=>(const Partition& other) const {
    return std::tuple{static_cast<uint64_t>(dataUnitId), partitionIndex} <=>
           std::tuple{static_cast<uint64_t>(other.dataUnitId), other.partitionIndex};
  }

  nlohmann::json toJson() const;
  explicit Partition(const nlohmann::json& json);
};
//----------------------------------------------------------------------------------------------------------------------
/// Partition that is located at a node
struct LocatedPartition {
  /// The partition
  Partition partition;
  /// The location
  NodeId node;
  /// Constructor
  LocatedPartition() = default;
  /// Constructor
  LocatedPartition(Partition partition, NodeId node) : partition(partition), node(node) {}
  /// Equality
  bool operator==(const LocatedPartition& other) const = default;

  nlohmann::json toJson() const;
  explicit LocatedPartition(const nlohmann::json& json);
};
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------
namespace std {
//----------------------------------------------------------------------------------------------------------------------
template <>
struct hash<dqsim::Partition> {
  size_t operator()(dqsim::Partition const& partition) const noexcept {
    size_t seed = 0;
    size_t h1 = hash<dqsim::DataUnitId>{}(partition.dataUnitId);
    size_t h2 = hash<uint64_t>{}(partition.partitionIndex);
    boost::hash_combine(seed, h1);
    boost::hash_combine(seed, h2);
    return seed;
  }
};
//----------------------------------------------------------------------------------------------------------------------
template <>
struct hash<dqsim::LocatedPartition> {
  size_t operator()(dqsim::LocatedPartition const& partition) const noexcept {
    size_t seed = 0;
    size_t h1 = hash<dqsim::DataUnitId>{}(partition.partition.dataUnitId);
    size_t h2 = hash<uint64_t>{}(partition.partition.partitionIndex);
    size_t h3 = hash<uint64_t>{}(partition.node);
    boost::hash_combine(seed, h1);
    boost::hash_combine(seed, h2);
    boost::hash_combine(seed, h3);
    return seed;
  }
};
//----------------------------------------------------------------------------------------------------------------------
}  // namespace std
//----------------------------------------------------------------------------------------------------------------------

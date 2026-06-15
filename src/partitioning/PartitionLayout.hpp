#pragma once
//----------------------------------------------------------------------------------------------------------------------
#include <string_view>
#include <unordered_set>

#include "src/partitioning/Attribute.hpp"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2022 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
enum class PartitionLayoutType {
  /// Data is stored in an unordered way
  Scattered,
  /// Data is hash partitioned by a set of attributes
  HashPartitioned,
  /// Data is stored at all nodes
  Broadcast,
  /// Data is stored at a single node
  SingleNode
};
//----------------------------------------------------------------------------------------------------------------------
/// Struct to define how a data unit should be partitioned
struct PartitionLayout {
  /// The type of partitioning
  PartitionLayoutType type;
  /// The number of partitions
  uint64_t nPartitions;
  /// The attributes on which it is partitioned (only relevant for hash partitioned)
  std::unordered_set<Attribute> attributes;
  /// Constructor
  PartitionLayout();
  /// Constructor
  PartitionLayout(PartitionLayoutType type, std::size_t nPartitions, std::unordered_set<Attribute> attributes);
  /// Equality
  bool operator==(const PartitionLayout& other) const = default;
  /// Checks whether a layout satisfies another layout
  bool satisfies(const PartitionLayout& other) const;
  /// Write as string
  void prettyPrint(std::ostream& out) const;
  /// Checks whether the data unit uses multiple partitions
  bool isPartitioned() const {
    return type == PartitionLayoutType::Scattered || type == PartitionLayoutType::HashPartitioned;
  }
  /// load from json
  explicit PartitionLayout(const nlohmann::json& json);
  /// Convert to json
  nlohmann::json toJson() const;
};
//----------------------------------------------------------------------------------------------------------------------
/// Class to define base relation partitioning without knowing the DU and attribute index in advance
struct BaseRelationLayoutDefinition {
  /// Simplified class for relation names
  struct Layout {
    /// The name of the relation
    std::string name;
    /// The type of layout
    PartitionLayoutType type;
    /// The number of partitions, 1 if not partitioned
    uint64_t nPartitions;
    /// Attributes (for hash distributed)
    std::vector<std::string> attributes;

    /// Constructor
    Layout(std::string name, PartitionLayoutType type, uint64_t nPartitions, std::vector<std::string> attributes = {})
        : name(std::move(name)), type(type), nPartitions(nPartitions), attributes(std::move(attributes)) {}
    nlohmann::json toJson() const;
    explicit Layout(const nlohmann::json& json);
  };
  /// List of all layouts
  std::vector<Layout> layouts;
  const Layout* getLayout(std::string_view name) const;

  BaseRelationLayoutDefinition() = default;
  explicit BaseRelationLayoutDefinition(std::vector<Layout> layouts) : layouts(std::move(layouts)) {}
  nlohmann::json toJson() const;
  explicit BaseRelationLayoutDefinition(const nlohmann::json& json);
};
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

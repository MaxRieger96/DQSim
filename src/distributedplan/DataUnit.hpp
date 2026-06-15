#pragma once
//----------------------------------------------------------------------------------------------------------------------
#include <json_fwd.hpp>
#include <optional>
#include <vector>

#include "src/CustomTypes.hpp"
#include "src/partitioning/Attribute.hpp"
#include "src/partitioning/PartitionLayout.hpp"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2022 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
/// Represents either a relation or an intermediate result
class DataUnit {
 public:
  /// Name
  std::string name;
  /// Attributes
  std::vector<Attribute> attributes;
  /// The partition layout of the data unit
  std::optional<PartitionLayout> partitionLayout;
  /// The id of the data unit
  DataUnitId id;
  /// Estimated number of tuples in the data unit
  double estimatedCard;
  /// True if the data unit represents an intermediate result
  /// The result is also considered an intermediate result here
  bool isIntermediate;

  /// Constructor
  DataUnit(std::string name, std::vector<Attribute> attributes, std::optional<PartitionLayout> partitionLayout,
           DataUnitId id, double estimatedCard, bool isIntermediate)
      : name(std::move(name)),
        attributes(std::move(attributes)),
        partitionLayout(std::move(partitionLayout)),
        id(id),
        estimatedCard(estimatedCard),
        isIntermediate(isIntermediate) {
    assert(!this->partitionLayout || this->partitionLayout->nPartitions);
  }
  /// Constructor
  DataUnit() = default;

  /// Equality
  bool operator==(const DataUnit& other) const = default;
  /// Compute the tuple size in bytes
  double tupleSize() const;
  /// Compute the size of a partition in bytes
  double partitionSize(std::optional<double> skewFactor) const;

  nlohmann::json toJson() const;
  explicit DataUnit(const nlohmann::json& json);
};
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

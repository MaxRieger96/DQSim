#pragma once
//----------------------------------------------------------------------------------------------------------------------
#include <json_fwd.hpp>
#include <string>

#include "src/CustomTypes.hpp"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2022 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
/// An attribute of a data unit
struct Attribute {
  /// Name
  std::string name;
  /// Id of the data unit
  DataUnitId dataUnitId;
  /// Index of the attribute within the data unit
  uint64_t attributeIndex;
  /// The size of an element of this attribute in bytes
  double size;

  /// Default Constructor
  Attribute() = default;
  /// Constructor
  Attribute(std::string name, DataUnitId dataUnitId, uint64_t attributeIndex, double size)
      : name(std::move(name)), dataUnitId(dataUnitId), attributeIndex(attributeIndex), size(size) {}
  /// Equality
  bool operator==(const Attribute& other) const = default;

  // Export as json
  nlohmann::json toJson() const;
  // Load from json
  explicit Attribute(const nlohmann::json& json);
};
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------
namespace std {
//----------------------------------------------------------------------------------------------------------------------
template <>
struct hash<dqsim::Attribute> {
  size_t operator()(dqsim::Attribute const& attribute) const noexcept {
    size_t seed = 0;
    size_t h1 = hash<dqsim::DataUnitId>{}(attribute.dataUnitId);
    size_t h2 = hash<uint64_t>{}(attribute.attributeIndex);
    size_t h3 = hash<string>{}(attribute.name);
    boost::hash_combine(seed, h1);
    boost::hash_combine(seed, h2);
    boost::hash_combine(seed, h3);
    return seed;
  }
};
//----------------------------------------------------------------------------------------------------------------------
}  // namespace std
//----------------------------------------------------------------------------------------------------------------------

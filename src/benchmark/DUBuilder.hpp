#pragma once
//----------------------------------------------------------------------------------------------------------------------
#include <span>

#include "src/distributedplan/DataUnit.hpp"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2023 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
/// Class to easily build data units
class DUBuilder {
 public:
  /// An attribute definition
  struct Attribute {
    /// The name
    std::string name;
    /// The size of a value in bytes
    double size;
  };

 private:
  /// Previously created DUs
  std::vector<DataUnit> dus;

 public:
  /// Create a data unit
  DataUnit buildDU(std::string name, double size, std::size_t nPartition, bool intermediate,
                   std::span<Attribute const> attributes, std::optional<size_t> partitionAttributeIndex,
                   bool makeSingleNode, bool makeBroadcast);
  /// Get all data units
  const std::vector<DataUnit>& getDUs() const;
};
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

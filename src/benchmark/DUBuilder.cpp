#include "DUBuilder.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "src/CustomTypes.hpp"
#include "src/distributedplan/DataUnit.hpp"
#include "src/partitioning/Attribute.hpp"
#include "src/partitioning/PartitionLayout.hpp"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2023 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
DataUnit DUBuilder::buildDU(std::string name, double size, std::size_t nPartitions, bool intermediate,
                            std::span<const Attribute> attributeDefinitions,
                            std::optional<size_t> partitionAttributeIndex, bool makeSingleNode, bool makeBroadcast) {
  assert(!makeSingleNode || !makeBroadcast);
  DataUnitId duId{dus.size()};
  std::vector<dqsim::Attribute> attributes;
  size_t iAttr = 0;
  for (const Attribute& attr : attributeDefinitions) {
    attributes.push_back(dqsim::Attribute{attr.name, duId, iAttr++, attr.size});
  }
  PartitionLayout layout{PartitionLayoutType::Scattered, nPartitions, {}};
  if (partitionAttributeIndex.has_value()) {
    layout = PartitionLayout{PartitionLayoutType::HashPartitioned, nPartitions, {attributes[*partitionAttributeIndex]}};
  }
  if (makeSingleNode) {
    assert(nPartitions == 1);
    layout = PartitionLayout{PartitionLayoutType::SingleNode, nPartitions, {}};
  }
  if (makeBroadcast) {
    assert(nPartitions == 1);
    layout = PartitionLayout{PartitionLayoutType::Broadcast, nPartitions, {}};
  }
  DataUnit result{std::move(name), std::move(attributes), layout, duId, size, intermediate};
  dus.push_back(result);
  return result;
}
//----------------------------------------------------------------------------------------------------------------------
const std::vector<DataUnit>& DUBuilder::getDUs() const { return dus; }
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

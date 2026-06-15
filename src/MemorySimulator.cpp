#include "src/MemorySimulator.hpp"

#include <cassert>
#include <span>

#include "src/Cluster.hpp"
#include "src/CustomTypes.hpp"
#include "src/distributedplan/DataUnit.hpp"
#include "src/partitioning/Partition.hpp"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2024 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
MemorySimulator::MemorySimulator(std::span<const DataUnit> dataUnits, const Cluster& cluster) : dataUnits(dataUnits) {
  for (const Node& n : cluster.nodes) {
    freeMemory.push_back(n.memorySize * 1024 * 1024 * 1024);
  }
}
//----------------------------------------------------------------------------------------------------------------------
double MemorySimulator::getPartitionSize(Partition partition, std::optional<double> skewFactor) {
  const auto& dataUnit = dataUnits[partition.dataUnitId];
  assert(dataUnit.partitionLayout->nPartitions != 0);
  return dataUnit.partitionSize(skewFactor);
}
//----------------------------------------------------------------------------------------------------------------------
bool MemorySimulator::allocate(NodeId node, double size) {
  if (freeMemory[node] < size) {
    // std::cout << "Node " << node << " out of memory\n";
    return false;
  }
  freeMemory[node] -= size;
  return true;
}
//----------------------------------------------------------------------------------------------------------------------
void MemorySimulator::free(NodeId node, double size) { freeMemory[node] += size; }
//----------------------------------------------------------------------------------------------------------------------
bool MemorySimulator::storePartition(NodeId node, Partition partition, std::optional<double> skewFactor) {
  return allocate(node, getPartitionSize(partition, skewFactor));
}
//----------------------------------------------------------------------------------------------------------------------
std::span<const double> MemorySimulator::getFreeMemory() const {
  return std::span<const double>(freeMemory.begin(), freeMemory.end());
}
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

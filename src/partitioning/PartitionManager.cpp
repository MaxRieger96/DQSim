#include "src/partitioning/PartitionManager.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <json.hpp>
#include <json_fwd.hpp>
#include <ostream>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "src/Cluster.hpp"
#include "src/CustomTypes.hpp"
#include "src/partitioning/Partition.hpp"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2022 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
PartitionManager::PartitionManager(const Cluster& cluster) {
  /// register the nodes in the respective maps
  for (NodeId iNode{0}; iNode < cluster.nodes.size(); ++iNode) {
    ownedPartitions.emplace(iNode, std::unordered_set<Partition>{});
    cachedPartitions.emplace(iNode, std::unordered_set<Partition>{});
  }
}
//----------------------------------------------------------------------------------------------------------------------
void PartitionManager::registerPartition(Partition partition, NodeId node) {
  assert(ownedPartitions.find(node) != ownedPartitions.end());
  assert(cachedPartitions.find(node) != cachedPartitions.end());

  // add partition to data unit map
  if (dataUnits.find(partition.dataUnitId) == dataUnits.end()) {
    dataUnits[partition.dataUnitId] = std::unordered_set<Partition>{};
  }
  dataUnits[partition.dataUnitId].insert(partition);

  // if the partition was already assigned to a node, remove it from the node
  if (owningNode.find(partition) != owningNode.end()) {
    NodeId previousNode = owningNode[partition];
    assert(ownedPartitions[previousNode].find(partition) != ownedPartitions[previousNode].end());
    ownedPartitions[previousNode].erase(partition);
  }
  owningNode[partition] = node;
  ownedPartitions[node].insert(partition);
  if (!cachingNodes.contains(partition)) cachingNodes[partition] = std::unordered_set<NodeId>{};
}
//----------------------------------------------------------------------------------------------------------------------
void PartitionManager::registerDU(DataUnitId du, size_t nPartitions, NodeId node) {
  for (size_t i = 0; i < nPartitions; ++i) {
    registerPartition(Partition{du, i}, node);
  }
}
//----------------------------------------------------------------------------------------------------------------------
void PartitionManager::cachePartition(Partition partition, NodeId node) {
  assert(cachingNodes.find(partition) != cachingNodes.end());
  assert(cachedPartitions.find(node) != cachedPartitions.end());
  assert(owningNode.contains(partition));
  assert(owningNode.find(partition)->second != node);
  cachingNodes[partition].insert(node);
  cachedPartitions[node].insert(partition);
}
//----------------------------------------------------------------------------------------------------------------------
void PartitionManager::deregisterDataUnit(DataUnitId dataUnitId) {
  assert(dataUnits.find(dataUnitId) != dataUnits.end());
  for (const Partition& partition : dataUnits[dataUnitId]) {
    removePartitionFromMaps(partition);
  }
  dataUnits.erase(dataUnitId);
}
//----------------------------------------------------------------------------------------------------------------------
void PartitionManager::deregisterPartition(Partition partition) {
  removePartitionFromMaps(partition);
  assert(dataUnits.find(partition.dataUnitId) != dataUnits.end());
  dataUnits[partition.dataUnitId].erase(partition);
}
//----------------------------------------------------------------------------------------------------------------------
void PartitionManager::removePartitionFromMaps(Partition partition) {
  assert(owningNode.find(partition) != owningNode.end());
  assert(cachingNodes.find(partition) != cachingNodes.end());
  for (const NodeId& node : cachingNodes[partition]) {
    assert(cachedPartitions[node].find(partition) != cachedPartitions[node].end());
    cachedPartitions[node].erase(partition);
  }
  cachingNodes.erase(partition);

  assert(ownedPartitions[owningNode[partition]].find(partition) != ownedPartitions[owningNode[partition]].end());
  ownedPartitions[owningNode[partition]].erase(partition);
  owningNode.erase(partition);
}
//----------------------------------------------------------------------------------------------------------------------
void PartitionManager::evictPartitionCache(Partition partition, NodeId node) {
  assert(cachingNodes.find(partition) != cachingNodes.end());
  assert(cachingNodes[partition].find(node) != cachingNodes[partition].end());
  assert(cachedPartitions.find(node) != cachedPartitions.end());
  assert(cachedPartitions[node].find(partition) != cachedPartitions[node].end());
  cachingNodes[partition].erase(node);
  cachedPartitions[node].erase(partition);
}
//----------------------------------------------------------------------------------------------------------------------
Partitioning PartitionManager::getPartitioning() const {
  return Partitioning{owningNode, cachingNodes, ownedPartitions.size()};
}
//----------------------------------------------------------------------------------------------------------------------
PartitionManager PartitionManager::loadPartitioning(const Partitioning& partitioning, const Cluster* cluster) {
  PartitionManager result{{}};
  if (!cluster) {
    Cluster dummy;
    dummy.nodes.resize(partitioning.nNodes);
    result = PartitionManager{dummy};
  } else {
    result = PartitionManager{*cluster};
  }
  for (const auto& [partition, node] : partitioning.owningNode) {
    result.registerPartition(partition, node);
  }
  for (auto& [partition, cacheNodes] : partitioning.cachingNodes) {
    for (const auto& node : cacheNodes) {
      result.cachePartition(partition, node);
    }
  }
  return result;
}
//----------------------------------------------------------------------------------------------------------------------
bool PartitionManager::hasPartition(NodeId node, Partition partition) const {
  return (owningNode.contains(partition) && owningNode.find(partition)->second == node) ||
         (cachingNodes.contains(partition) && cachingNodes.find(partition)->second.contains(node));
}
//----------------------------------------------------------------------------------------------------------------------
void PartitionManager::print(std::ostream& out) const {
  for (const auto& [du, partitions] : dataUnits) {
    out << "DataUnit " << du << "\n";
    for (const auto& p : partitions) {
      out << " " << p.partitionIndex << ": " << owningNode.find(p)->second << " (";
      for (const auto& cache : cachingNodes.find(p)->second) {
        out << cache << ", ";
      }
      out << ")\n";
    }
    out << "\n";
  }
}
//----------------------------------------------------------------------------------------------------------------------
bool PartitionManager::partitionStored(Partition partition) const { return owningNode.contains(partition); }
//----------------------------------------------------------------------------------------------------------------------
std::vector<NodeId> PartitionManager::owningAndCachingNodes(Partition p) const {
  if (!partitionStored(p)) return std::vector<NodeId>();
  std::vector<NodeId> result;
  assert(owningNode.contains(p));
  result.push_back(owningNode.find(p)->second);
  assert(cachingNodes.contains(p));
  for (const auto& n : cachingNodes.find(p)->second) {
    result.push_back(n);
  }
  return result;
}
//----------------------------------------------------------------------------------------------------------------------
Partitioning::Partitioning(std::unordered_map<Partition, NodeId> owningNode,
                           std::unordered_map<Partition, std::unordered_set<NodeId>> cachingNodes, size_t nNodes)
    : owningNode(std::move(owningNode)), cachingNodes(std::move(cachingNodes)), nNodes(nNodes) {}
//----------------------------------------------------------------------------------------------------------------------
nlohmann::json Partitioning::toJson() const {
  nlohmann::json json;
  json["nNodes"] = nNodes;

  json["owningNode"] = nlohmann::json::array();
  for (const auto& [partition, node] : owningNode) {
    nlohmann::json entry;
    entry["k"] = partition.toJson();
    entry["v"] = node.t;
    json["owningNode"].push_back(entry);
  }

  json["cachingNodes"] = nlohmann::json::array();
  for (const auto& [partition, nodes] : cachingNodes) {
    nlohmann::json entry;
    entry["k"] = partition.toJson();
    entry["v"] = nlohmann::json::array();
    for (const auto& node : nodes) {
      entry["v"].push_back(node.t);
    }
    json["cachingNodes"].push_back(entry);
  }

  return json;
}
//----------------------------------------------------------------------------------------------------------------------
Partitioning::Partitioning(const nlohmann::json& json) : nNodes(json.at("nNodes").get<uint64_t>()) {
  for (const auto& entry : json.at("owningNode")) {
    owningNode[Partition(entry.at("k"))] = NodeId(entry.at("v").get<uint64_t>());
  }
  for (const auto& entry : json.at("cachingNodes")) {
    Partition partition(entry.at("k"));
    std::unordered_set<NodeId> nodes;
    for (const auto& node : entry.at("v")) {
      nodes.insert(NodeId(node.get<uint64_t>()));
    }
    cachingNodes[partition] = std::move(nodes);
  }
}
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

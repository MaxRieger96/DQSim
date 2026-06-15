#pragma once
//----------------------------------------------------------------------------------------------------------------------
#include <cstdint>
#include <json_fwd.hpp>
#include <span>
#include <vector>

#include "src/CustomTypes.hpp"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2022 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
/// Represents the necessary information of a node for the simulator
struct Node {
  /// The number of cores
  uint64_t nCores;
  /// The main memory size in GiB
  double memorySize;
  /// The network speed in g_bit per second
  double networkSpeed;
  /// The expected network latency in seconds
  double networkLatency;

  /// Equality
  bool operator==(const Node& other) const = default;

  /// Get the bandwidth in bytes/s
  double byteBandwidth() const;

  nlohmann::json toJson() const;
  explicit Node(const nlohmann::json& json);
  Node() = default;
};
//----------------------------------------------------------------------------------------------------------------------
/// Represents all available nodes
struct Cluster {
  /// Ordered list of nodes
  std::vector<Node> nodes;
  /// Storage service like S3 can be simulated like a virtual node with very large bandwidth
  /// The last element in nodes can be this storage service node
  bool storageService = false;

  /// Constructor
  explicit Cluster(std::span<Node> nodes, bool storageService = false)
      : nodes(nodes.begin(), nodes.end()), storageService(storageService) {}
  /// Constructor
  explicit Cluster(std::initializer_list<Node> nodes, bool storageService = false)
      : nodes(nodes.begin(), nodes.end()), storageService(storageService) {}
  /// Constructor
  Cluster() = default;
  /// Equality
  bool operator==(const Cluster& other) const = default;
  /// Get the compute nodes excluding storage service
  std::span<Node const> getComputeNodes() const;
  /// Check whether a node is a compute node
  bool isComputeNode(NodeId node) const;

  // Export as json
  nlohmann::json toJson() const;
  // Load from json
  explicit Cluster(const nlohmann::json& json);
};
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

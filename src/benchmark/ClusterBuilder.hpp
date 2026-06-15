#pragma once
//----------------------------------------------------------------------------------------------------------------------
#include <json_fwd.hpp>

#include "src/Cluster.hpp"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2023 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
/// A simple definition class to easily define clusters
struct ClusterBuilderDefinition {
  /// The number of compute nodes in the cluster. Does not include the storage service.
  uint64_t nNodes;
  /// Whether to add a storage service
  bool storageService;
  /// The types of nodes used in the cluster.
  /// Node types will follow the provided order.
  /// Node types are equally replicated to create nComputeNodes nodes.
  /// If equal distribution is not possible, the remainder will be filled with the last type.
  std::vector<std::string> nodeTypes;

  // Get a name for the cluster
  std::string getName() const;

  /// Member-wise constructor
  ClusterBuilderDefinition(uint64_t nNodes, bool storageService, std::vector<std::string> nodeTypes)
      : nNodes(nNodes), storageService(storageService), nodeTypes(std::move(nodeTypes)) {}

  nlohmann::json toJson() const;
  explicit ClusterBuilderDefinition(const nlohmann::json& json);
};
//----------------------------------------------------------------------------------------------------------------------
/// Collection of methods to build commonly used clusters
class ClusterBuilder {
 public:
  /// Get the ec2 instance c5n.18xlarge
  static Node get18xlarge();
  /// Get the ec2 instance c5n.9xlarge
  static Node get9xlarge();
  /// Get the ec2 instance c5n.xlarge
  static Node getxlarge();
  /// Get a fast machine with slow network
  static Node getBigSlowNetwork();
  /// Get a slow machine with fast network
  static Node getSmallFastNetwork();
  /// Get a slow machine
  static Node getSmall();
  /// Get a storage service node
  static Node getStorageNode();
  /// Get a node by its name
  static Node getNodeByName(std::string_view nodeName);

  /// Get a cluster with n equal nodes
  static Cluster getCluster(Node node, size_t n, bool storageService);

  /// Get a 2 machine cluster with storage service
  static Cluster get2Big() { return getCluster(get18xlarge(), 2, true); }
  /// Get a 4 machine cluster with storage service
  static Cluster get4Big() { return getCluster(get18xlarge(), 4, true); }
  /// Get a 16 machine cluster with storage service
  static Cluster get16Big() { return getCluster(get18xlarge(), 16, true); }
  /// Get a 128 machine cluster with storage service
  static Cluster get128Big() { return getCluster(get18xlarge(), 128, true); }
  /// Get a 1024 machine cluster with storage service
  static Cluster get1024Big() { return getCluster(get18xlarge(), 1024, true); }

  /// Build a cluster from a builder definition
  static Cluster fromDefinition(ClusterBuilderDefinition definition);
};
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

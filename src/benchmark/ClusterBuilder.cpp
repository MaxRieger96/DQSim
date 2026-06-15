#include "ClusterBuilder.hpp"

#include <cstddef>
#include <cstdint>
#include <format>
#include <fstream>
#include <json.hpp>
#include <json_fwd.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "src/Cluster.hpp"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2023 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace {
//----------------------------------------------------------------------------------------------------------------------
using namespace dqsim;
//----------------------------------------------------------------------------------------------------------------------
std::string c5n18xlarge = "{\"nCores\":72, \"memorySize\": 192, \"networkSpeed\":100, \"networkLatency\":2.5e-4}";
std::string c5n9xlarge = "{\"nCores\":36, \"memorySize\": 96, \"networkSpeed\":50, \"networkLatency\":2.5e-4}";
std::string c5nxlarge = "{\"nCores\":4, \"memorySize\": 10.5, \"networkSpeed\":20, \"networkLatency\":2.5e-4}";
std::string bigSlowNetwork = "{\"nCores\":72, \"memorySize\": 192, \"networkSpeed\":1, \"networkLatency\":2.5e-4}";
std::string smallFastNetwork = "{\"nCores\":4, \"memorySize\": 32, \"networkSpeed\":100, \"networkLatency\":2.5e-4}";
std::string small = "{\"nCores\":4, \"memorySize\": 32, \"networkSpeed\":1, \"networkLatency\":2.5e-4}";
// latency network?
std::string blobstore = "{\"nCores\":0, \"memorySize\": 1000000, \"networkSpeed\":1000000, \"networkLatency\":0.1}";
//----------------------------------------------------------------------------------------------------------------------
}  // namespace
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
Node ClusterBuilder::get18xlarge() { return Node(nlohmann::json::parse(c5n18xlarge)); }
//----------------------------------------------------------------------------------------------------------------------
Node ClusterBuilder::get9xlarge() { return Node(nlohmann::json::parse(c5n9xlarge)); }
//----------------------------------------------------------------------------------------------------------------------
Node ClusterBuilder::getxlarge() { return Node(nlohmann::json::parse(c5nxlarge)); }
//----------------------------------------------------------------------------------------------------------------------
Node ClusterBuilder::getBigSlowNetwork() { return Node(nlohmann::json::parse(bigSlowNetwork)); }
//----------------------------------------------------------------------------------------------------------------------
Node ClusterBuilder::getSmallFastNetwork() { return Node(nlohmann::json::parse(smallFastNetwork)); }
//----------------------------------------------------------------------------------------------------------------------
Node ClusterBuilder::getSmall() { return Node(nlohmann::json::parse(small)); }
//----------------------------------------------------------------------------------------------------------------------
Node ClusterBuilder::getStorageNode() { return Node(nlohmann::json::parse(blobstore)); }
//----------------------------------------------------------------------------------------------------------------------
Node ClusterBuilder::getNodeByName(std::string_view nodeName) {
  std::string path = "src/benchmark/nodeTypes.json";
  std::ifstream stream(path);
  if (!stream.is_open()) {
    throw std::runtime_error(std::format("Failed to open nodeTypes.json: {}", path));
  }

  const nlohmann::json json = nlohmann::json::parse(stream);
  const std::string key(nodeName);
  if (!json.contains(key)) {
    throw std::runtime_error(std::format("Unknown node type: {}", nodeName));
  }

  return Node(json.at(key));
}
//----------------------------------------------------------------------------------------------------------------------
Cluster ClusterBuilder::getCluster(Node node, size_t n, bool storageService) {
  std::vector<Node> nodes(n, node);
  if (storageService) {
    nodes.emplace_back(getStorageNode());
  }
  return Cluster{{nodes}, storageService};
}
//----------------------------------------------------------------------------------------------------------------------
Cluster ClusterBuilder::fromDefinition(ClusterBuilderDefinition definition) {
  std::vector<Node> nodes;
  nodes.reserve(definition.nNodes + (definition.storageService ? 1 : 0));

  size_t nodesPerType = definition.nNodes / definition.nodeTypes.size();

  for (const auto& type : definition.nodeTypes) {
    Node node = getNodeByName(type);
    for (size_t i = 0; i < nodesPerType; ++i) {
      nodes.push_back(node);
    }
  }

  // We fill up with the last node type
  Node node = getNodeByName(definition.nodeTypes.back());
  while (nodes.size() < definition.nNodes) {
    nodes.push_back(node);
  }

  if (definition.storageService) {
    nodes.push_back(getNodeByName("storage"));
  }

  return Cluster{{nodes}, definition.storageService};
}
//----------------------------------------------------------------------------------------------------------------------
std::string ClusterBuilderDefinition::getName() const {
  std::string typeName;
  for (const auto& type : nodeTypes) {
    typeName += type;
    typeName += "-";
  }
  typeName.pop_back();
  return std::format("{}x{}", typeName, nNodes);
}
//----------------------------------------------------------------------------------------------------------------------
nlohmann::json ClusterBuilderDefinition::toJson() const {
  nlohmann::json json;
  json["nNodes"] = nNodes;
  json["storageService"] = storageService;
  json["nodeTypes"] = nodeTypes;
  return json;
}
//----------------------------------------------------------------------------------------------------------------------
ClusterBuilderDefinition::ClusterBuilderDefinition(const nlohmann::json& json)
    : nNodes(json["nNodes"].get<uint64_t>()),
      storageService(json["storageService"].get<bool>()),
      nodeTypes(json["nodeTypes"].get<std::vector<std::string>>()) {}
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

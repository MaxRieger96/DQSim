//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2022 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
#include "src/Cluster.hpp"

#include <cstdint>
#include <json.hpp>
#include <json_fwd.hpp>
#include <span>

#include "src/CustomTypes.hpp"
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
double Node::byteBandwidth() const { return 1024 * 1024 * 1024 * networkSpeed / 8; }
//----------------------------------------------------------------------------------------------------------------------
nlohmann::json Node::toJson() const {
  nlohmann::json json;
  json["nCores"] = nCores;
  json["memorySize"] = memorySize;
  json["networkSpeed"] = networkSpeed;
  json["networkLatency"] = networkLatency;
  return json;
}
//----------------------------------------------------------------------------------------------------------------------
Node::Node(const nlohmann::json& json)
    : nCores(json["nCores"].get<uint64_t>()),
      memorySize(json["memorySize"].get<double>()),
      networkSpeed(json["networkSpeed"].get<double>()),
      networkLatency(json["networkLatency"].get<double>()) {}
//----------------------------------------------------------------------------------------------------------------------
nlohmann::json Cluster::toJson() const {
  nlohmann::json json;
  json["nodes"] = nlohmann::json::array();
  for (const auto& node : nodes) {
    json["nodes"].push_back(node.toJson());
  }
  json["storageService"] = storageService;
  return json;
}
//----------------------------------------------------------------------------------------------------------------------
Cluster::Cluster(const nlohmann::json& json) : nodes(), storageService(json["storageService"].get<bool>()) {
  for (const auto& nodeJson : json["nodes"]) {
    nodes.push_back(Node(nodeJson));
  }
}
//----------------------------------------------------------------------------------------------------------------------
std::span<Node const> Cluster::getComputeNodes() const {
  return std::span{nodes.begin(), storageService ? nodes.end() - 1 : nodes.end()};
}
//----------------------------------------------------------------------------------------------------------------------
bool Cluster::isComputeNode(NodeId node) const { return node < (getComputeNodes().size()); }
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

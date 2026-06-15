#include "PartitionLayout.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <json.hpp>
#include <json_fwd.hpp>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2022 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
const BaseRelationLayoutDefinition::Layout* BaseRelationLayoutDefinition::getLayout(std::string_view name) const {
  auto layout = std::find_if(layouts.begin(), layouts.end(), [name](const Layout& elem) { return elem.name == name; });
  if (layout == layouts.end()) return nullptr;
  return &*layout;
}
//----------------------------------------------------------------------------------------------------------------------
bool PartitionLayout::satisfies(const PartitionLayout& other) const {
  if (type == PartitionLayoutType::Broadcast || other.type == PartitionLayoutType::Scattered) return true;
  return *this == other;
}
//----------------------------------------------------------------------------------------------------------------------
void PartitionLayout::prettyPrint(std::ostream& out) const {
  switch (type) {
    case PartitionLayoutType::Scattered:
      out << "Scattered";
      break;
    case PartitionLayoutType::HashPartitioned:
      out << "HashPartitioned";
      break;
    case PartitionLayoutType::Broadcast:
      out << "Broadcast";
      break;
    case PartitionLayoutType::SingleNode:
      out << "SingleNode";
      break;
  }
  if (type == PartitionLayoutType::HashPartitioned) {
    for (const auto& attribute : attributes) {
      out << " " << attribute.name;
    }
    out << " (" << nPartitions << ")";
  }
}
//----------------------------------------------------------------------------------------------------------------------
PartitionLayout::PartitionLayout(PartitionLayoutType type, std::size_t nPartitions,
                                 std::unordered_set<Attribute> attributes)
    : type(type), nPartitions(nPartitions), attributes(std::move(attributes)) {}
//----------------------------------------------------------------------------------------------------------------------
PartitionLayout::PartitionLayout() : type(PartitionLayoutType::Scattered), nPartitions(0), attributes() {}
//----------------------------------------------------------------------------------------------------------------------
NLOHMANN_JSON_SERIALIZE_ENUM(PartitionLayoutType, {
                                                      {PartitionLayoutType::Scattered, "scattered"},
                                                      {PartitionLayoutType::HashPartitioned, "hashpartitioned"},
                                                      {PartitionLayoutType::Broadcast, "broadcast"},
                                                      {PartitionLayoutType::SingleNode, "singlenode"},
                                                  });
//----------------------------------------------------------------------------------------------------------------------
nlohmann::json PartitionLayout::toJson() const {
  nlohmann::json json;
  json["type"] = type;
  json["nPartitions"] = nPartitions;
  json["attributes"] = nlohmann::json::array();
  for (const auto& attribute : attributes) {
    json["attributes"].push_back(attribute.toJson());
  }
  return json;
}
//----------------------------------------------------------------------------------------------------------------------
PartitionLayout::PartitionLayout(const nlohmann::json& json)
    : type(json.at("type").get<PartitionLayoutType>()),
      nPartitions(json.at("nPartitions").get<uint64_t>()),
      attributes() {
  for (const auto& attr : json.at("attributes")) {
    attributes.insert(Attribute(attr));
  }
}
//----------------------------------------------------------------------------------------------------------------------
nlohmann::json BaseRelationLayoutDefinition::Layout::toJson() const {
  nlohmann::json json;
  json["name"] = name;
  json["type"] = type;
  json["nPartitions"] = nPartitions;
  json["attributes"] = nlohmann::json::array();
  for (const auto& attribute : attributes) {
    json["attributes"].push_back(attribute);
  }
  return json;
}
//----------------------------------------------------------------------------------------------------------------------
BaseRelationLayoutDefinition::Layout::Layout(const nlohmann::json& json)
    : name(json.at("name").get<std::string>()),
      type(json.at("type").get<PartitionLayoutType>()),
      nPartitions(json.at("nPartitions").get<uint64_t>()) {
  for (const auto& attribute : json.at("attributes")) {
    attributes.push_back(attribute.get<std::string>());
  }
}
//----------------------------------------------------------------------------------------------------------------------
BaseRelationLayoutDefinition::BaseRelationLayoutDefinition(const nlohmann::json& json) {
  for (const auto& layout : json.at("layouts")) {
    layouts.emplace_back(layout);
  }
}
//----------------------------------------------------------------------------------------------------------------------
nlohmann::json BaseRelationLayoutDefinition::toJson() const {
  nlohmann::json json;
  json["layouts"] = nlohmann::json::array();
  for (const auto& layout : layouts) {
    json["layouts"].push_back(layout.toJson());
  }
  return json;
}
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

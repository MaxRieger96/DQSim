#include "src/partitioning/Attribute.hpp"

#include <cstdint>
#include <json.hpp>
#include <json_fwd.hpp>
#include <string>
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2022 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
nlohmann::json Attribute::toJson() const {
  nlohmann::json json;
  json["name"] = name;
  json["dataUnitId"] = dataUnitId.t;
  json["attributeIndex"] = attributeIndex;
  json["size"] = size;
  return json;
}
//----------------------------------------------------------------------------------------------------------------------
Attribute::Attribute(const nlohmann::json& json)
    : name(json.at("name").get<std::string>()),
      dataUnitId(json.at("dataUnitId").get<uint64_t>()),
      attributeIndex(json.at("attributeIndex").get<uint64_t>()),
      size(json.at("size").get<double>()) {}
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

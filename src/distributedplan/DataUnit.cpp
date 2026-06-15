//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2022 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
#include "src/distributedplan/DataUnit.hpp"

#include <cassert>
#include <cstdint>
#include <json.hpp>
#include <json_fwd.hpp>
#include <string>

#include "src/partitioning/Attribute.hpp"
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
double DataUnit::tupleSize() const {
  double result = 0;
  for (const Attribute& attr : attributes) {
    result += attr.size;
  }
  return result;
}
//----------------------------------------------------------------------------------------------------------------------
double DataUnit::partitionSize(std::optional<double> skewFactor) const {
  assert(partitionLayout);
  assert(partitionLayout->nPartitions);
  double result = tupleSize() * estimatedCard / partitionLayout->nPartitions;
  if (skewFactor) return result * *skewFactor;
  return result;
}
//----------------------------------------------------------------------------------------------------------------------
nlohmann::json DataUnit::toJson() const {
  nlohmann::json json;
  json["name"] = name;
  json["attributes"] = nlohmann::json::array();
  for (const Attribute& attr : attributes) {
    json["attributes"].push_back(attr.toJson());
  }
  assert(partitionLayout);
  json["partitionLayout"] = partitionLayout->toJson();
  json["id"] = id.t;
  json["estimatedCard"] = estimatedCard;
  json["isIntermediate"] = isIntermediate;
  return json;
}
//----------------------------------------------------------------------------------------------------------------------
DataUnit::DataUnit(const nlohmann::json& json)
    : name(json.at("name").get<std::string>()),
      attributes(),
      partitionLayout(PartitionLayout(json.at("partitionLayout"))),
      id(DataUnitId(json.at("id").get<uint64_t>())),
      estimatedCard(json.at("estimatedCard").get<double>()),
      isIntermediate(json.at("isIntermediate").get<bool>()) {
  for (const auto& attr : json.at("attributes")) {
    attributes.push_back(Attribute(attr));
  }
  assert(!this->partitionLayout || this->partitionLayout->nPartitions);
}
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

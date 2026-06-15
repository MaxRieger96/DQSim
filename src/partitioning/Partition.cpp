#include "src/partitioning/Partition.hpp"

#include <cstdint>
#include <json.hpp>
#include <json_fwd.hpp>
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2022 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
nlohmann::json Partition::toJson() const {
  nlohmann::json json;
  json["dataUnitId"] = dataUnitId.t;
  json["partitionIndex"] = partitionIndex;
  return json;
}
//----------------------------------------------------------------------------------------------------------------------
Partition::Partition(const nlohmann::json& json)
    : dataUnitId(json.at("dataUnitId").get<uint64_t>()), partitionIndex(json.at("partitionIndex").get<uint64_t>()) {}
//----------------------------------------------------------------------------------------------------------------------
nlohmann::json LocatedPartition::toJson() const {
  nlohmann::json json;
  json["Partition"] = partition.toJson();
  json["Node"] = node.t;
  return json;
}
//----------------------------------------------------------------------------------------------------------------------
LocatedPartition::LocatedPartition(const nlohmann::json& json)
    : partition(json.at("Partition")), node(json.at("Node").get<uint64_t>()) {}
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

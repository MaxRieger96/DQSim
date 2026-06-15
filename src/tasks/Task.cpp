#include "src/tasks/Task.hpp"

#include <cstdint>
#include <json.hpp>
#include <json_fwd.hpp>
#include <vector>

#include "src/CustomTypes.hpp"
#include "src/partitioning/Partition.hpp"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2022 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
Task::Task(PipelineId pipelineId, Partition scannedPartition, const std::vector<Partition>& requiredPartitions,
           Partition outputPartition, bool broadcast)
    : pipeline(pipelineId),
      scannedPartition(scannedPartition),
      requiredPartitions(requiredPartitions.begin(), requiredPartitions.end()),
      outputPartition(outputPartition),
      broadcast(broadcast) {}
//----------------------------------------------------------------------------------------------------------------------
nlohmann::json Task::toJson() const {
  nlohmann::json json;
  json["pipelineId"] = pipeline.t;
  json["scannedPartition"] = scannedPartition.toJson();
  json["requiredPartitions"] = nlohmann::json::array();
  for (const auto& p : requiredPartitions) {
    json["requiredPartitions"].push_back(p.toJson());
  }
  json["outputPartition"] = outputPartition.toJson();
  json["broadcast"] = broadcast;
  return json;
}
//----------------------------------------------------------------------------------------------------------------------
Task::Task(const nlohmann::json& json)
    : pipeline(json.at("pipelineId").get<uint64_t>()),
      scannedPartition(json.at("scannedPartition")),
      requiredPartitions(),
      outputPartition(json.at("outputPartition")),
      broadcast(json.at("broadcast").get<bool>()) {
  for (const auto& p : json.at("requiredPartitions")) {
    requiredPartitions.push_back(Partition(p));
  }
}
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

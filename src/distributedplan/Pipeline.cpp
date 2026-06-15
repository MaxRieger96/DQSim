#include "src/distributedplan/Pipeline.hpp"

#include <cstddef>
#include <cstdint>
#include <json.hpp>
#include <json_fwd.hpp>
#include <string>
#include <vector>

//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2022 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace {
//----------------------------------------------------------------------------------------------------------------------
template <typename T>
bool vectorEquivalence(const std::vector<T>& a, const std::vector<T>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i] != b[i]) return false;
  }
  return true;
}
//----------------------------------------------------------------------------------------------------------------------
}  // namespace
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
bool Pipeline::operator==(const Pipeline& other) const {
  return id == other.id && output == other.output && scan == other.scan &&
         vectorEquivalence(requiredData, other.requiredData);
}
//----------------------------------------------------------------------------------------------------------------------
nlohmann::json Pipeline::toJson() const {
  nlohmann::json json;
  json["id"] = id.t;
  json["scan"] = scan.t;
  json["output"] = output.t;
  json["estimatedLoad"] = estimatedLoad;
  json["requiredData"] = nlohmann::json::array();
  for (const auto& du : requiredData) {
    json["requiredData"].push_back(du.t);
  }
  json["description"] = description;
  return json;
}
//----------------------------------------------------------------------------------------------------------------------
Pipeline::Pipeline(const nlohmann::json& json)
    : id(json.at("id").get<uint64_t>()),
      scan(json.at("scan").get<uint64_t>()),
      output(json.at("output").get<uint64_t>()),
      estimatedLoad(json.at("estimatedLoad").get<double>()),
      requiredData(),
      // description(json.at("description").get<std::string>()) {
      description(json.value("description", std::string{})) {
  // description(json.contains("description") ? json.at("description").get<std::string>() : std::string{}) {
  for (const auto& du : json.at("requiredData")) {
    requiredData.push_back(DataUnitId(du.get<uint64_t>()));
  }
}
//----------------------------------------------------------------------------------------------------------------------
nlohmann::json ShuffleStage::toJson() const {
  nlohmann::json json;
  json["from"] = from.t;
  json["to"] = to.t;
  return json;
}
//----------------------------------------------------------------------------------------------------------------------
ShuffleStage::ShuffleStage(const nlohmann::json& json)
    : from(json.at("from").get<uint64_t>()), to(json.at("to").get<uint64_t>()) {}
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------------------------
#include "src/SimulatorSettings.hpp"
//----------------------------------------------------------------------------------------------------------------------
#include <json.hpp>
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2026 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
nlohmann::json SimulatorSettings::toJson() const {
  nlohmann::json json;
  json["nParallelTasks"] = nParallelTasks;
  json["allowStreamingInput"] = allowStreamingInput;
  json["minimumBlockSize"] = minimumBlockSize;
  json["limitStepSize"] = limitStepSize;
  json["stepLimit"] = stepLimit;
  if (taskSlowdownFactors) {
    json["taskSlowdownFactors"] = *taskSlowdownFactors;
  }
  if (partitionSkewFactors) {
    json["partitionSkewFactors"] = *partitionSkewFactors;
  }
  if (nLinearScaling) {
    json["nLinearScaling"] = *nLinearScaling;
  }
  if (discountFactor) {
    json["discountFactor"] = *discountFactor;
  }
  return json;
}
//----------------------------------------------------------------------------------------------------------------------
SimulatorSettings::SimulatorSettings(const nlohmann::json& json)
    : nParallelTasks(json.at("nParallelTasks").get<uint64_t>()),
      allowStreamingInput(json.at("allowStreamingInput").get<bool>()),
      minimumBlockSize(json.at("minimumBlockSize").get<double>()),
      limitStepSize(json.at("limitStepSize").get<bool>()),
      stepLimit(json.at("stepLimit").get<double>()),
      taskSlowdownFactors(),
      partitionSkewFactors() {
  if (json.contains("taskSlowdownFactors") && !json.at("taskSlowdownFactors").is_null()) {
    taskSlowdownFactors = json.at("taskSlowdownFactors").get<std::vector<std::vector<double>>>();
  }
  if (json.contains("partitionSkewFactors") && !json.at("partitionSkewFactors").is_null()) {
    partitionSkewFactors = json.at("partitionSkewFactors").get<std::vector<std::vector<double>>>();
  }
  if (json.contains("nLinearScaling") && !json.at("nLinearScaling").is_null()) {
    nLinearScaling = json.at("nLinearScaling").get<uint64_t>();
  }
  if (json.contains("discountFactor") && !json.at("discountFactor").is_null()) {
    discountFactor = json.at("discountFactor").get<double>();
  }
}
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

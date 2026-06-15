#include "src/benchmark/BenchmarkDefinition.hpp"

#include <cstdint>
#include <format>
#include <fstream>
#include <json.hpp>
#include <json_fwd.hpp>
#include <stdexcept>
#include <string>
#include <vector>
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2025 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
NLOHMANN_JSON_SERIALIZE_ENUM(PlannerType, {
                                              {PlannerType::RR, "rr"},
                                              {PlannerType::ARR, "arr"},
                                              {PlannerType::HEFT, "heft"},
                                              {PlannerType::NetHEFT, "netheft"},
                                              {PlannerType::CEFT, "ceft"},
                                              {PlannerType::Greedy1, "greedy1"},
                                              {PlannerType::Greedy2, "greedy2"},
                                              {PlannerType::Greedy3, "greedy3"},
                                              {PlannerType::Greedy4, "greedy4"},
                                              {PlannerType::BruteForce, "bruteforce"},
                                              {PlannerType::Rand1k, "rand1k"},
                                              {PlannerType::Rand10k, "rand10k"},
                                              {PlannerType::Rand100k, "rand100k"},
                                              {PlannerType::StateSep, "statesep"},
                                              {PlannerType::StateSepB, "statesepb"},
                                              {PlannerType::StateSepP2P, "statesepp2p"},
                                              {PlannerType::StateSepP2PB, "statesepp2pb"},
                                              {PlannerType::StateSepNoUp, "statesepnoup"},
                                              {PlannerType::StateSepNoUpB, "statesepnoupb"},
                                              {PlannerType::StateSepNoUpD, "statesepnoupd"},
                                              {PlannerType::StateSepNoUpDB, "statesepnoupdb"},
                                          })
//----------------------------------------------------------------------------------------------------------------------
nlohmann::json BenchmarkResult::toJson() const {
  nlohmann::json json;
  json["assignmentAlgorithm"] = assignmentAlgorithm;
  json["date"] = date;
  json["totalExecutionTime"] = totalExecutionTime;
  json["totalSchedulingTime"] = totalSchedulingTime;
  json["benchmarkNames"] = benchmarkNames;
  json["executionTimes"] = executionTimes;
  json["schedulingTimes"] = schedulingTimes;
  if (skewFactor) {
    json["skewFactor"] = *skewFactor;
  } else {
    json["skewFactor"] = nullptr;
  }
  if (cardinalityError) {
    json["cardinalityError"] = *cardinalityError;
  } else {
    json["cardinalityError"] = nullptr;
  }
  return json;
}
//----------------------------------------------------------------------------------------------------------------------
nlohmann::json BenchmarkDefinition::toJson() const {
  nlohmann::json json;
  json["name"] = name;
  json["nParallelTasks"] = nParallelTasks;

  json["clusters"] = nlohmann::json::array();
  for (const auto& cluster : clusters) {
    json["clusters"].push_back(cluster.toJson());
  }

  json["partitionCounts"] = partitionCounts;

  json["queryCategories"] = nlohmann::json::array();
  for (const auto& qc : queryCategories) {
    json["queryCategories"].push_back(qc);
  }

  json["partitioners"] = nlohmann::json::array();
  for (const auto& p : partitioners) {
    json["partitioners"].push_back(p);
  }

  json["planners"] = nlohmann::json::array();
  for (const auto& p : planners) {
    json["planners"].push_back(p);
  }

  if (taskLimit) {
    json["taskLimit"] = *taskLimit;
  } else {
    json["taskLimit"] = nullptr;
  }

  if (skewFactors) {
    json["skewFactors"] = *skewFactors;
  } else {
    json["skewFactors"] = nullptr;
  }

  if (cardinalityErrors) {
    json["cardinalityErrors"] = *cardinalityErrors;
  } else {
    json["cardinalityErrors"] = nullptr;
  }

  if (scaleFactor) {
    json["scaleFactor"] = *scaleFactor;
  } else {
    json["scaleFactor"] = nullptr;
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
BenchmarkDefinition::BenchmarkDefinition(const nlohmann::json& json)
    : name(json.at("name").get<std::string>()), nParallelTasks(json.at("nParallelTasks").get<uint64_t>()) {
  for (const auto& c : json.at("clusters")) {
    clusters.push_back(ClusterBuilderDefinition(c));
  }
  for (const auto& pc : json.at("partitionCounts")) {
    partitionCounts.push_back(pc.get<uint64_t>());
  }
  for (const auto& qc : json.at("queryCategories")) {
    queryCategories.push_back(qc.get<QueryCategory>());
  }
  for (const auto& p : json.at("partitioners")) {
    partitioners.push_back(p.get<PartitionerType>());
  }
  for (const auto& p : json.at("planners")) {
    planners.push_back(p.get<PlannerType>());
  }
  if (json.contains("taskLimit") && !json["taskLimit"].is_null()) {
    taskLimit = json["taskLimit"].get<uint64_t>();
  }
  if (json.contains("skewFactors") && !json["skewFactors"].is_null()) {
    skewFactors = json["skewFactors"].get<std::vector<double>>();
  }
  if (json.contains("cardinalityErrors") && !json["cardinalityErrors"].is_null()) {
    cardinalityErrors = json["cardinalityErrors"].get<std::vector<double>>();
  }
  if (json.contains("scaleFactor") && !json["scaleFactor"].is_null()) {
    scaleFactor = json["scaleFactor"].get<double>();
  }
  if (json.contains("nLinearScaling") && !json["nLinearScaling"].is_null()) {
    nLinearScaling = json["nLinearScaling"].get<uint64_t>();
  }
  if (json.contains("discountFactor") && !json["discountFactor"].is_null()) {
    discountFactor = json["discountFactor"].get<double>();
  }
}
//----------------------------------------------------------------------------------------------------------------------
BenchmarkDefinition BenchmarkDefinition::loadBenchmarkFromFile(const std::string& path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    throw std::runtime_error(std::format("Failed to open benchmark file: {}", path));
  }
  const nlohmann::json json = nlohmann::json::parse(file);
  return BenchmarkDefinition(json);
}
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

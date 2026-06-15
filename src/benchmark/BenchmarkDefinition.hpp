#pragma once
//----------------------------------------------------------------------------------------------------------------------
#include <json.hpp>
#include <json_fwd.hpp>
#include <optional>
#include <string>
#include <vector>

#include "src/benchmark/ClusterBuilder.hpp"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2025 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
enum class PlannerType {
  // Round Robin planners
  RR,
  ARR,

  // EFT planners
  HEFT,
  NetHEFT,
  CEFT,

  // Greedy Planners
  // GMN
  Greedy1,
  // Fallback to aligned RR
  Greedy2,
  // Fallback to EST
  Greedy3,
  // Fallback to EFT
  Greedy4,

  // Enumeration planners
  BruteForce,
  Rand1k,
  Rand10k,
  Rand100k,

  // State Sep planners
  StateSep,
  StateSepB,
  StateSepP2P,
  StateSepP2PB,
  StateSepNoUp,
  StateSepNoUpB,
  StateSepNoUpD,
  StateSepNoUpDB,
};
//----------------------------------------------------------------------------------------------------------------------
enum class PartitionerType {
  /// Partitions are only on storage service
  None,
  /// All nodes have all partitions
  Copy,
  /// The first node gets all partitions
  One,
  /// One random node gets all partitions
  OneRand,
  /// Aligned round robin. Partition n of each DU will be mapped to node n % nNodes
  /// All nodes get partitions
  RRAll,
  /// Only the first half of nodes gets partitions
  RRHalf,
  /// Partitions are assigned round robin to half the nodes, but the list of nodes is randomized
  RRHalfRand,
  /// Partitions are randomly assigned to nodes
  /// One copy of each partition
  Rand1,
  /// Two copies of each partition
  Rand2,
  /// Four copies of each partition
  Rand4,
};
//----------------------------------------------------------------------------------------------------------------------
NLOHMANN_JSON_SERIALIZE_ENUM(PartitionerType, {
                                                  {PartitionerType::None, "none"},
                                                  {PartitionerType::Copy, "copy"},
                                                  {PartitionerType::One, "one"},
                                                  {PartitionerType::OneRand, "onerand"},
                                                  {PartitionerType::RRAll, "rrall"},
                                                  {PartitionerType::RRHalf, "rrhalf"},
                                                  {PartitionerType::RRHalfRand, "rrhalfrand"},
                                                  {PartitionerType::Rand1, "rand1"},
                                                  {PartitionerType::Rand2, "rand2"},
                                                  {PartitionerType::Rand4, "rand4"},
                                              })
//----------------------------------------------------------------------------------------------------------------------
enum class QueryCategory {
  // The TPC-H queries
  TPCH,
  // The growing query subset from SQLStorm
  Growing,
  // The handcrafted queries
  Handcrafted,
  JOB,
  TPCDS,
};
//----------------------------------------------------------------------------------------------------------------------
NLOHMANN_JSON_SERIALIZE_ENUM(QueryCategory, {
                                                {QueryCategory::TPCH, "tpch"},
                                                {QueryCategory::Growing, "growing"},
                                                {QueryCategory::Handcrafted, "handcrafted"},
                                                {QueryCategory::JOB, "job"},
                                                {QueryCategory::TPCDS, "tpcds"},
                                            })
//----------------------------------------------------------------------------------------------------------------------
struct BenchmarkResult {
  /// The name of the assignment algorithm
  std::string assignmentAlgorithm;
  /// The date of the benchmark
  std::string date;
  /// Time to execute all benchmarks in seconds
  double totalExecutionTime;
  /// Time to optimize assignments for all benchmarks in seconds
  double totalSchedulingTime;
  /// Names of all benchmarks
  std::vector<std::string> benchmarkNames;
  /// Execution times of all benchmarks
  std::vector<double> executionTimes;
  /// Scheduling times of all benchmarks
  std::vector<double> schedulingTimes;
  /// Partition skew factor if used
  std::optional<double> skewFactor;
  /// Cardinality estimation error factor if used
  std::optional<double> cardinalityError;

  nlohmann::json toJson() const;
};
//----------------------------------------------------------------------------------------------------------------------
struct BenchmarkDefinition {
  /// The name of the benchmark
  std::string name;
  /// The clusters to use for all queries
  std::vector<dqsim::ClusterBuilderDefinition> clusters;
  /// The partition counts to use for all queries
  std::vector<uint64_t> partitionCounts;
  /// All queries to benchmark
  std::vector<QueryCategory> queryCategories;
  /// The partitioners to use for base relations
  std::vector<PartitionerType> partitioners;
  /// The planners (scheduling algorithms) to use
  std::vector<PlannerType> planners;
  /// Number of tasks that can be executed in parallel on a node
  uint64_t nParallelTasks;
  /// The maximum number of tasks a problem may have. This can be used for full enumeration experiments.
  std::optional<uint64_t> taskLimit;
  /// The partition skew factors to use
  std::optional<std::vector<double>> skewFactors;
  /// The cardinality estimation errors to use for the planners
  std::optional<std::vector<double>> cardinalityErrors;
  /// A cardinality scale factor to use to simulate larger cardinalities
  std::optional<double> scaleFactor;
  // Number of cores until which algorithms scale linearly
  std::optional<uint64_t> nLinearScaling;
  // Discount factor applied to each additional core beyond nLinearScaling.
  std::optional<double> discountFactor;

  BenchmarkDefinition(std::string name, std::vector<dqsim::ClusterBuilderDefinition> clusters,
                      std::vector<uint64_t> partitionCounts, std::vector<QueryCategory> queryCategories,
                      std::vector<PartitionerType> partitioners, std::vector<PlannerType> planners,
                      uint64_t nParallelTasks, std::optional<uint64_t> taskLimit = std::nullopt,
                      std::optional<std::vector<double>> skewFactors = std::nullopt,
                      std::optional<std::vector<double>> cardinalityErrors = std::nullopt,
                      std::optional<double> scaleFactor = std::nullopt,
                      std::optional<uint64_t> nLinearScaling = std::nullopt,
                      std::optional<double> discountFactor = std::nullopt)
      : name(std::move(name)),
        clusters(std::move(clusters)),
        partitionCounts(std::move(partitionCounts)),
        queryCategories(std::move(queryCategories)),
        partitioners(std::move(partitioners)),
        planners(std::move(planners)),
        nParallelTasks(nParallelTasks),
        taskLimit(taskLimit),
        skewFactors(std::move(skewFactors)),
        cardinalityErrors(std::move(cardinalityErrors)),
        scaleFactor(scaleFactor),
        nLinearScaling(nLinearScaling),
        discountFactor(discountFactor) {}
  nlohmann::json toJson() const;
  explicit BenchmarkDefinition(const nlohmann::json& json);
  BenchmarkDefinition() = default;

  /// Load definition from file
  static BenchmarkDefinition loadBenchmarkFromFile(const std::string& path);
};
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

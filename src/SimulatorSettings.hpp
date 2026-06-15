#pragma once
//----------------------------------------------------------------------------------------------------------------------
#include <cstdint>
#include <json_fwd.hpp>
#include <optional>
#include <vector>
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2026 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
/// Settings used to define simulator behavior
struct SimulatorSettings {
  /// Number of tasks that can be executed in parallel on a node
  uint64_t nParallelTasks = 1'000'000;
  // Should tasks, transfers, and shuffles be allowed to start when their input is not fully present yet?
  // For tasks, this only allows the scanned input to be streamed.
  bool allowStreamingInput = false;
  // The minimum data size streaming ops are working on
  double minimumBlockSize = 1 * 1024 * 1024;
  // Whether to limit the amount of time that is simulated in a single step
  // This can make sense to make partials available for subsequent tasks when streaming
  bool limitStepSize = false;
  // Upper limit for step length in seconds
  double stepLimit = 1e-4;
  // Slowdown factors of individual tasks.
  // First index is the pipeline id, second the task
  // Should sum up to n_tasks for each pipeline.
  std::optional<std::vector<std::vector<double>>> taskSlowdownFactors;
  // Partition skew factors to be multiplied to the partition size without skew
  // First index is the DataUnitId, second the partition
  // Should sum up to n_partitions for each DataUnit
  std::optional<std::vector<std::vector<double>>> partitionSkewFactors;
  // Number of cores until which algorithms scale linearly
  // 16 might be a reasonable default
  std::optional<uint64_t> nLinearScaling;
  // Discount factor applied to each additional core beyond nLinearScaling.
  // Effective speedup is modeled as:
  //   nLinearScaling + sum(discountFactor^i) for i = 1 to nCores - nLinearScaling
  // 0.9 might be a reasonable default
  std::optional<double> discountFactor;

  SimulatorSettings() = default;
  static SimulatorSettings getDefault() { return {}; }

  nlohmann::json toJson() const;
  explicit SimulatorSettings(const nlohmann::json& json);
};
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

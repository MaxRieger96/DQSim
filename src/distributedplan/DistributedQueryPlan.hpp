#pragma once
//----------------------------------------------------------------------------------------------------------------------
#include <iostream>
#include <json_fwd.hpp>
#include <optional>
#include <ranges>
#include <span>

#include "src/distributedplan/DataUnit.hpp"
#include "src/distributedplan/Pipeline.hpp"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2022 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
struct Node;
//----------------------------------------------------------------------------------------------------------------------
class DistributedQueryPlan {
 public:
  /// All data units used in the query
  std::vector<DataUnit> dataUnits;
  /// All pipelines of the Query
  std::vector<Pipeline> pipelines;
  /// All shuffle stages of the Query
  std::vector<ShuffleStage> shuffleStages;
  /// Get pipelines in dependency order (i.e. dependent pipeline comes after its dependencies)
  std::vector<PipelineId> dependencyOrder;

  /// Constructor
  DistributedQueryPlan(std::vector<DataUnit> dataUnits, std::vector<Pipeline> pipelines,
                       std::vector<ShuffleStage> shuffleStages, std::vector<PipelineId> dependencyOrder)
      : dataUnits(std::move(dataUnits)),
        pipelines(std::move(pipelines)),
        shuffleStages(std::move(shuffleStages)),
        dependencyOrder(std::move(dependencyOrder)) {
    consistencyCheck();
    getResultDataUnit();
  };
  /// Constructor
  DistributedQueryPlan() = default;
  /// Copy Constructor
  DistributedQueryPlan(const DistributedQueryPlan& other) = default;
  /// Copy Assignment
  DistributedQueryPlan& operator=(const DistributedQueryPlan& other) = default;
  /// Move Constructor
  DistributedQueryPlan(DistributedQueryPlan&& other) noexcept = default;
  /// Move Assignment
  DistributedQueryPlan& operator=(DistributedQueryPlan&& other) noexcept = default;
  /// Equality
  bool operator==(const DistributedQueryPlan& other) const = default;
  /// Verify the indexes of data units and pipelines
  void verifyIndexes() const;
  /// Check the consistency of the plan
  void consistencyCheck() const;
  /// Get the data units
  std::span<DataUnit> getDataUnits();
  /// Get the data unit ids
  std::ranges::iota_view<uint64_t, uint64_t> getDataUnitIds();
  /// Get a specific data unit
  DataUnit& getDataUnit(DataUnitId dataUnitId);
  /// Get a specific data unit
  const DataUnit& getDataUnit(DataUnitId dataUnitId) const;
  /// Get the pipelines
  const std::vector<Pipeline>& getPipelines() const;
  /// Get a pipeline by id
  const Pipeline& getPipeline(PipelineId pipelineId) const;
  /// Get the base relations, i.e. all relations that are present in the beginning
  std::vector<DataUnitId> getBaseRelations() const;
  /// Get the result data unit
  DataUnitId getResultDataUnit() const;
  /// Print the plan in DOT format
  void toDOT(std::ostream& out) const;
  /// Check whether the data unit will be used by a shuffle
  bool isShuffleInput(DataUnitId du) const;
  /// Check whether the data unit is created by a shuffle
  bool isShuffleOutput(DataUnitId du) const;
  /// Find the shuffle output of the data unit. Nullopt if du is not shuffled
  std::optional<DataUnitId> getShuffleOutput(DataUnitId du) const;
  /// Check whether the pipeline is executed as broadcast
  bool isBroadcast(PipelineId p) const;
  /// Estimate the execution time of a task of this pipeline for a given node
  double estimateRuntime(double nCores, PipelineId pipeline, std::optional<double> slowdownFactor,
                         std::optional<std::size_t> nLinearScaling, std::optional<double> discountFactor) const;
  /// Distort the cardinalities using a fixed q-error. Cards will be randomly multiplied or divided by the factor
  void distortCardinalities(double factor, bool random);

  /// Create a variant of this distributed plan that has an aggregation on top so the result will be single node.
  DistributedQueryPlan forceResultSingleNode() const;

  nlohmann::json toJson() const;
  explicit DistributedQueryPlan(const nlohmann::json& json);
};
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

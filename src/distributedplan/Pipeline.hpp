#pragma once
//----------------------------------------------------------------------------------------------------------------------
#include <json_fwd.hpp>
#include <vector>

#include "src/CustomTypes.hpp"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2022 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
/// Abstract representation of a pipeline with data dependencies
class Pipeline {
 public:
  /// A unique id for the pipeline
  PipelineId id;
  /// The data unit scanned by the pipeline
  DataUnitId scan;
  /// The data unit produced by the pipeline
  DataUnitId output;
  /// The estimated computational load for the input measured in s per tuple on a 16 core machine
  double estimatedLoad;
  /// The other required data units
  std::vector<DataUnitId> requiredData;
  // Debugging info for the pipeline
  std::string description;

  /// Constructor
  Pipeline(PipelineId id, DataUnitId scan, DataUnitId output, std::vector<DataUnitId> requiredData,
           double estimatedLoad, std::string description)
      : id(id),
        scan(scan),
        output(output),
        estimatedLoad(estimatedLoad),
        requiredData(std::move(requiredData)),
        description(std::move(description)) {}
  /// Constructor
  Pipeline() = default;
  /// Equality
  bool operator==(const Pipeline& other) const;

  /// Iterator for all involved Pipelines
  class DUIterator {
   public:
    const Pipeline* p;
    size_t i;
    DUIterator(const Pipeline* p, size_t i) : p(p), i(i) {}
    DataUnitId operator*() const { return (i == 0) ? p->output : (i == 1) ? p->scan : p->requiredData[i - 2]; }
    DUIterator& operator++() {
      ++i;
      return *this;
    }
    bool operator!=(const DUIterator& other) const { return i != other.i; }
  };

  // DU Range type
  class DURange {
   public:
    size_t startingPoint;
    const Pipeline* p;
    DURange(const Pipeline* p, size_t startingPoint) : startingPoint(startingPoint), p(p) {}
    DUIterator begin() const { return DUIterator(p, startingPoint); }
    DUIterator end() const { return DUIterator(p, 2 + p->requiredData.size()); }
  };

  // Returns an iterable range for use with range-based for loops.
  DURange getDUs() const { return DURange(this, 0); }
  // Returns an iterable range for use with range-based for loops.
  DURange getInputDUs() const { return DURange(this, 1); }

  nlohmann::json toJson() const;
  explicit Pipeline(const nlohmann::json& json);
};
//----------------------------------------------------------------------------------------------------------------------
/// Abstract representation of a repartitioning stage, where data is newly partitioned and transferred
class ShuffleStage {
 public:
  /// The data that is shuffled
  DataUnitId from;
  /// The new layout the data is shuffled to
  DataUnitId to;

  /// Member-wise constructor
  ShuffleStage(DataUnitId from, DataUnitId to) : from(from), to(to) {}
  /// Equality
  bool operator==(const ShuffleStage& other) const = default;

  ShuffleStage() = default;

  nlohmann::json toJson() const;
  explicit ShuffleStage(const nlohmann::json& json);
};
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

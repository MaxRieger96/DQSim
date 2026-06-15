#pragma once
//----------------------------------------------------------------------------------------------------------------------
#include <boost/container/small_vector.hpp>
#include <json_fwd.hpp>

#include "src/CustomTypes.hpp"
#include "src/partitioning/Partition.hpp"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2022 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
/// A task consists of the executed pipeline and the scanned partition
struct Task {
  /// The pipeline that is executed in this task
  PipelineId pipeline;
  /// The scanned partition the pipeline is executed on
  Partition scannedPartition;
  /// Partitions which need to be present to execute the task
  boost::container::small_vector<Partition, 4> requiredPartitions;
  /// The result of the task
  Partition outputPartition;
  /// Flag for identical tasks that need to be started on all nodes
  bool broadcast;

  /// Constructor
  Task(PipelineId pipelineId, Partition scannedPartition, const std::vector<Partition>& requiredPartitions,
       Partition outputPartition, bool broadcast);
  /// Constructor
  Task() = default;
  /// Equality
  bool operator==(const Task& other) const = default;

  nlohmann::json toJson() const;
  explicit Task(const nlohmann::json& json);

  /// Iterator for all input partitions
  class PartitionIterator {
   public:
    const Task* t;
    size_t i;
    PartitionIterator(const Task* t, size_t i) : t(t), i(i) {}
    Partition operator*() const {
      return (i == 0) ? t->outputPartition : (i == 1) ? t->scannedPartition : t->requiredPartitions[i - 2];
    }
    PartitionIterator& operator++() {
      ++i;
      return *this;
    }
    bool operator!=(const PartitionIterator& other) const { return i != other.i; }
  };

  class PartitionRange {
   public:
    const Task* t;
    size_t iStart;
    PartitionRange(const Task* t, size_t iStart) : t(t), iStart(iStart) {}
    PartitionIterator begin() const { return PartitionIterator(t, iStart); }
    PartitionIterator end() const { return PartitionIterator(t, 2 + t->requiredPartitions.size()); }
  };

  // Returns an iterable range for use with range-based for loops.
  PartitionRange getPartitions() const { return PartitionRange(this, 0); }
  PartitionRange getInputPartitions() const { return PartitionRange(this, 1); }
};
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------
namespace std {
//----------------------------------------------------------------------------------------------------------------------
template <>
struct hash<dqsim::Task> {
  size_t operator()(dqsim::Task const& task) const noexcept {
    size_t seed = 0;
    size_t h1 = hash<dqsim::PipelineId>{}(task.pipeline);
    size_t h2 = hash<dqsim::Partition>{}(task.scannedPartition);
    boost::hash_combine(seed, h1);
    boost::hash_combine(seed, h2);
    return seed;
  }
};
//----------------------------------------------------------------------------------------------------------------------
}  // namespace std
//----------------------------------------------------------------------------------------------------------------------

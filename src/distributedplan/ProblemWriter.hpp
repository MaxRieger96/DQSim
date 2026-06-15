#pragma once
//----------------------------------------------------------------------------------------------------------------------
#include <json_fwd.hpp>
#include <optional>
#include <utility>

#include "src/Cluster.hpp"
#include "src/CustomTypes.hpp"
#include "src/distributedplan/DistributedQueryPlan.hpp"
#include "src/partitioning/PartitionManager.hpp"
#include "src/partitioning/Partitioner.hpp"
#include "src/tasks/Task.hpp"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2022 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
/// The entire problem instance that is written out to json
struct AssignmentProblem {
  /// The cluster
  Cluster cluster;
  /// The query plan
  DistributedQueryPlan plan;
  /// The initial partitioning
  Partitioning partitioning;
  /// The tasks that need to be located
  std::unordered_map<PipelineId, std::vector<Task>> tasks;
  /// The broadcast tasks that need to be executed on every node
  std::unordered_map<PipelineId, Task> broadcastTasks;
  /// A name for the problem for debugging
  std::string name;
  /// The node on which the result should be present.
  /// If it is set, the result has to be single node and on that node.
  /// If there is no result node, the result may have any partitioning
  std::optional<NodeId> resultNode;

  /// Equality
  bool operator==(const AssignmentProblem& other) const noexcept = default;

  nlohmann::json toJson() const;
  explicit AssignmentProblem(const nlohmann::json& json);
  /// Default constructor
  AssignmentProblem() = default;
  /// Member-wise constructor
  AssignmentProblem(Cluster cluster, DistributedQueryPlan plan, Partitioning partitioning,
                    std::unordered_map<PipelineId, std::vector<Task>> tasks,
                    std::unordered_map<PipelineId, Task> broadcastTasks, std::string name,
                    std::optional<NodeId> resultNode)
      : cluster(std::move(cluster)),
        plan(std::move(plan)),
        partitioning(std::move(partitioning)),
        tasks(std::move(tasks)),
        broadcastTasks(std::move(broadcastTasks)),
        name(std::move(name)),
        resultNode(std::move(resultNode)) {}
};
//----------------------------------------------------------------------------------------------------------------------
/// Class to create a complete problem instance and write it to json
class ProblemWriter {
  /// The plan to use for the problem
  DistributedQueryPlan* plan;
  /// The cluster to distribute the tasks over
  const Cluster* cluster;
  /// Initial Partitioning
  std::unique_ptr<PartitionManager> initialPartitioning;
  /// The tasks to which need to be assigned to machines (created by buildTasks())
  std::optional<std::unordered_map<PipelineId, std::vector<Task>>> tasks = std::nullopt;
  /// The tasks to which are broadcast (created by buildTasks())
  std::optional<std::unordered_map<PipelineId, Task>> broadcastTasks = std::nullopt;
  /// Compute the required partitions
  std::vector<Partition> getRequired(const Pipeline& pipeline, std::optional<uint64_t> partitionIndex);
  /// Name of the problem for debugging
  std::string name;
  /// The node on which the result should be present.
  /// If it is set, the result has to be single node and on that node.
  /// If there is no result node, the result may have any partitioning
  std::optional<NodeId> resultNode;

 public:
  /// Constructor
  ProblemWriter(DistributedQueryPlan& plan, const Cluster& cluster, std::string name, std::optional<NodeId> resultNode);
  /// Distribute partitions of base relations to nodes
  void distributeBaseRelations(Partitioner& planner);
  /// Load an initial partitioning for base relations
  void loadBaseRelationPartitioning(const Partitioning& partitioning);
  /// Build the tasks
  void buildTasks();
  /// Get a problem instance
  AssignmentProblem getProblem();
};
//----------------------------------------------------------------------------------------------------------------------
/// An object to track the solution of a problem
struct TaskAssignment {
  /// Task with assigned location
  struct LocatedTask {
    /// The task
    Task task;
    /// The node it is located at
    NodeId node;

    /// Member-wise constructor
    LocatedTask(Task task, NodeId node) : task(std::move(task)), node(node) {}

    nlohmann::json toJson() const;
    explicit LocatedTask(const nlohmann::json& json);
    LocatedTask() = default;
  };

  /// Transfer of a partition from one node to another
  struct Transfer {
    /// The partition to transfer
    Partition partition;
    /// The node that sends the partition
    NodeId from;
    /// The node that receives the partition
    NodeId to;

    /// Member-wise constructor
    Transfer(Partition partition, NodeId from, NodeId to) : partition(partition), from(from), to(to) {}

    nlohmann::json toJson() const;
    explicit Transfer(const nlohmann::json& json);
    Transfer() = default;
  };

  /// During a shuffle, we repartition data and collect it on a target node.
  struct ShuffleTarget {
    /// The target shuffle partition
    Partition partition;
    /// The node the partition should be shuffled to
    NodeId location;

    /// Member-wise constructor
    ShuffleTarget(Partition partition, NodeId location) : partition(partition), location(location) {}

    nlohmann::json toJson() const;
    explicit ShuffleTarget(const nlohmann::json& json);
    ShuffleTarget() = default;
  };

  /// Located Tasks
  std::vector<LocatedTask> locatedTasks;
  /// Transfers
  std::vector<Transfer> transfers;
  /// Shuffle Targets
  std::vector<ShuffleTarget> shuffleTargets;
  /// The partitions that should be written to disk
  std::optional<std::vector<Partition>> durablePartitions;

  /// Member-wise constructor
  TaskAssignment(std::vector<LocatedTask> locatedTasks, std::vector<Transfer> transfers,
                 std::vector<ShuffleTarget> shuffleTargets,
                 std::optional<std::vector<Partition>> durablePartitions = {})
      : locatedTasks(std::move(locatedTasks)),
        transfers(std::move(transfers)),
        shuffleTargets(std::move(shuffleTargets)),
        durablePartitions(std::move(durablePartitions)) {}

  nlohmann::json toJson() const;
  explicit TaskAssignment(const nlohmann::json& json);
  TaskAssignment() = default;
};
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

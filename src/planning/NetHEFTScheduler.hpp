#pragma once
//----------------------------------------------------------------------------------------------------------------------
#include "src/distributedplan/ProblemWriter.hpp"
#include "src/planning/TaskAssignmentPlanner.hpp"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2025 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
/// Scheduler that maintains blocked schedules for all resources of nodes
struct NetHEFTScheduler {
  /// A time slot that is blocked
  struct ResourceAlloc {
    double startTime;
    double finishTime;
  };
  using ResourceSchedule = std::vector<ResourceAlloc>;
  /// All resources with a sorted list of blocked time slots
  struct NodeSchedule {
    ResourceSchedule cpu;
    ResourceSchedule download;
    ResourceSchedule upload;
  };
  /// A transfer that possibly does not transfer data because source and target can be identical
  struct Transfer {
    NodeId from;
    NodeId to;
    uint64_t fromSlot;
    uint64_t toSlot;
    ResourceAlloc schedule;
    Partition partition;
  };
  /// The target at which a partition will be shuffled
  struct PartitionShuffle {
    NodeId to;
    uint64_t toSlot;
    ResourceAlloc downloadSchedule;
    Partition partition;
  };
  /// A type used when no transfer is needed
  struct NoTransfer {
    double availableTime;
  };
  /// A partition can be loaded either by a transfer or a shuffle
  using PartitionLoad = std::variant<Transfer, PartitionShuffle, NoTransfer>;
  /// Get the available time of a PartitionLoad
  static double availableTime(const NetHEFTScheduler::PartitionLoad& load);
  /// A delta object to store blocked resources and available data without adding them to the scheduler
  struct Delta {
    /// The additional blocked resources for all nodes
    std::vector<NodeSchedule> nodeSchedules;
    /// The time each partition is available
    std::unordered_map<Partition, std::vector<std::pair<NodeId, double>>> partitionTimes;
    /// The latest time at which all partitions of a shuffled DU are available
    std::unordered_map<DataUnitId, double> shuffleInputCollectedTimes;
    /// All currently assigned tasks
    std::unordered_map<PipelineId, std::vector<NodeId>> pipelineAssignments;

    /// Add a partition load
    void addPartitionLoad(PartitionLoad load, const AssignmentProblem& problem);
    /// Add a transfer
    void addTransfer(Transfer transfer, const AssignmentProblem& problem);
    /// Add a partition shuffle
    void addPartitionShuffle(PartitionShuffle shuffle);

    explicit Delta(size_t nNodes) : nodeSchedules(nNodes) {}

    /// Make delta object reusable for usage with same cluster
    void clear();
  };

  /// The assignment problem
  const AssignmentProblem& problem;
  /// All currently assigned tasks
  std::unordered_map<PipelineId, std::vector<NodeId>> pipelineAssignments;
  /// All currently assigned transfers
  std::vector<TaskAssignment::Transfer> transferAssignments;
  /// All currently assigned shuffle Targets
  std::vector<TaskAssignment::ShuffleTarget> shuffleTargetAssignments;
  /// The blocked resources for all nodes
  std::vector<NodeSchedule> nodeSchedules;
  /// The partitions with everything that is already assigned and transferred
  PartitionManager partitionManager;
  /// The time each partition is available
  std::unordered_map<Partition, std::vector<std::pair<NodeId, double>>> partitionTimes;
  /// The latest time at which all partitions of a shuffled DU are available
  std::unordered_map<DataUnitId, double> shuffleInputCollectedTimes;

  /// Constructor
  NetHEFTScheduler(const AssignmentProblem& problem);

  /// Get a task assignment from the schedules
  static TaskAssignment convertToAssignment(NetHEFTScheduler&& scheduler);

  /// Estimate the finish time of tasks that will be executed in given order without side effects
  double finishTime(std::span<const Task> tasks, NodeId node, Delta& delta) const;

  /// Get the assignment for a specific task
  NodeId getAssignment(const Task& t) const;
  /// Get transfer time window between two nodes
  Transfer transferSchedule(NodeId n1, NodeId n2, double lowerBound, double bytesTransferred, Partition p) const;
  /// Get transfer time window between two nodes, returns transfer for delta
  Transfer transferSchedule(NodeId n1, NodeId n2, double lowerBound, double bytesTransferred, Partition p,
                            const Delta& delta) const;
  /// Get transfer time window between storage and one node
  Transfer transferScheduleFromStorage(NodeId n1, NodeId n2, double lowerBound, double bytesTransferred,
                                       Partition p) const;
  /// Get transfer time window between storage and one node, returns transfer for delta
  Transfer transferScheduleFromStorage(NodeId n1, NodeId n2, double lowerBound, double bytesTransferred, Partition p,
                                       const Delta& delta) const;
  /// Add a partition load
  void addPartitionLoad(PartitionLoad load);
  /// Add a transfer
  void addTransfer(Transfer transfer);
  /// Add a partition shuffle
  void addPartitionShuffle(PartitionShuffle shuffle);
  /// Get time time a partition can be available at a node
  PartitionLoad availableTime(Partition p, NodeId n) const;
  /// Get time time a partition can be available at a node
  PartitionLoad availableTime(Partition p, NodeId n, const Delta& delta) const;
  /// Get the earliest time frame to schedule a slot
  static std::pair<uint64_t, double> earliestStartSlot(const ResourceSchedule& schedule, double lowerBound,
                                                       double duration, uint64_t lowerBoundSlot = 0);
  /// Get the earliest time frame that fits into two schedules
  static std::tuple<uint64_t, uint64_t, double> earliestStartSlot(const ResourceSchedule& schedA,
                                                                  const ResourceSchedule& schedB, double lowerBound,
                                                                  double duration);
  /// Get the earliest time frame that fits into four schedules
  static std::tuple<uint64_t, uint64_t, uint64_t, uint64_t, double> earliestStartSlot(
      const ResourceSchedule& schedA, const ResourceSchedule& schedB, const ResourceSchedule& schedC,
      const ResourceSchedule& schedD, double lowerBound, double duration);
  /// Insert a blocking slot into a schedule
  static void insertSlot(NetHEFTScheduler::ResourceSchedule& schedule, NetHEFTScheduler::ResourceAlloc slot,
                         uint64_t slotIndex);
  /// Add an execution of a task
  double addExecution(NodeId node, const Task& t, double earliestStartTime);
  /// Add an execution of a task to a delta
  double addExecutionToDelta(NodeId node, const Task& t, double earliestStartTime, Delta& delta) const;
  /// Add the loading of dependencies for a task
  double addLoads(NodeId node, const Task& t);
  /// Add the loading of dependencies for a task to a delta
  double addLoadsToDelta(NodeId node, const Task& t, Delta& delta) const;
  /// Do the shuffle upload phase of a finished pipeline
  void doShuffle(PipelineId p, double latestFinishTime);
  /// Do the shuffle upload phase of a finished pipeline on delta
  void doShuffleOnDelta(PipelineId p, double latestFinishTime, Delta& delta) const;
  /// Shuffle or transfer the result partition to node 0
  void placeResultPartition();
  /// Run a broadcast pipeline
  void runBroadcastPipeline(PipelineId p);
};
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

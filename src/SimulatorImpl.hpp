#pragma once
//----------------------------------------------------------------------------------------------------------------------
#include <boost/functional/hash.hpp>
#include <string>

#include "src/CustomTypes.hpp"
#include "src/MemorySimulator.hpp"
#include "src/Simulator.hpp"
#include "src/SimulatorSettings.hpp"
#include "src/distributedplan/DistributedQueryPlan.hpp"
#include "src/distributedplan/ProblemWriter.hpp"
#include "src/network/NetworkSimulator.hpp"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2022 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
/// State of a simulator during execution
class SimulatorImpl {
 public:
  /// A running task that has a progress
  struct RunningTask {
    TaskAssignment::LocatedTask task;
    /// Progress in percent
    double progress = 0;
    /// The maximum progress the task can currently make due to present input data
    double progressLimit = 1;
    /// Is set to true when the task already started but is suspended because of missing input data.
    bool waitingForInput = false;
    /// Constructor
    explicit RunningTask(const TaskAssignment::LocatedTask* task) : task(*task) {}
    /// Constructor
    RunningTask(const TaskAssignment::LocatedTask* task, double progressLimit)
        : task(*task), progressLimit(progressLimit) {}
    /// Constructor
    RunningTask() : task(), progress(0) {}
  };
  /// A running transfer that has a progress
  struct RunningTransfer {
    TaskAssignment::Transfer transfer;
    /// Remaining waiting time for network latency in seconds. Progress only changes if latency time is 0.
    double latencyTime;
    /// Progress in percent
    double progress = 0;
    /// The maximum progress the transfer can currently make due to present input data
    double progressLimit = 1;
    /// Is set to true when the transfer already started but is suspended because of missing input data.
    bool waitingForInput = false;
    /// Constructor
    RunningTransfer(const TaskAssignment::Transfer* transfer, double latencyTime)
        : transfer(*transfer), latencyTime(latencyTime) {}
    RunningTransfer(const TaskAssignment::Transfer* transfer, double latencyTime, double progressLimit)
        : transfer(*transfer), latencyTime(latencyTime), progressLimit(progressLimit) {}
    /// Constructor
    RunningTransfer() : transfer(), latencyTime(0), progress(0) {}
  };
  /// A partially collected shuffle output partition
  struct ShuffleOutPartition {
    /// The partition that is collected
    Partition partition;
    /// The node at which the partition is collected
    NodeId node;
    /// The collected amount in percent
    double progress = 0;
  };
  /// A running shuffle operation
  struct RunningShuffle {
    /// The partitions that this shuffle reads
    std::vector<Partition> inPartitions;
    /// The partitions that this shuffle writes
    std::vector<Partition> outPartitions;
    /// The corresponding shuffle Stage
    ShuffleStage stage;
    /// The node that sends data from its input partitions
    NodeId from;
    /// The node that collects output partitions
    NodeId to;
    /// Remaining waiting time for network latency in seconds. Progress only changes if latency time is 0.
    double latencyTime;
    /// Progress in percent of the currently available input
    double progress;
    /// The maximum progress each partition can make
    std::vector<double> progressLimits;

    double progressLimit() const;
  };

 public:
  /// Marker to keep track of data units that do not have to be shuffled anymore
  ///  This only contains data units that are created by broadcast pipelines where the shuffle
  ///  could be triggered more than once
  std::unordered_set<DataUnitId> completedShuffles;
  /// Map that contains data units that will be shuffled
  std::unordered_map<DataUnitId, const ShuffleStage*> shuffledDataUnits;

  /// The tasks that have not started yet
  std::vector<const TaskAssignment::LocatedTask*> pendingTasks;
  /// The transfers that have not started yet
  std::vector<const TaskAssignment::Transfer*> pendingTransfers;
  /// The input partitions that need to be shuffled
  std::vector<Partition> pendingShuffles;
  /// A map to look up the targets of shuffle partitions
  std::unordered_map<Partition, std::vector<NodeId>> shuffleTargets;

  /// The currently running tasks
  std::vector<RunningTask> runningTasks{};
  /// The currently running transfers
  std::vector<RunningTransfer> runningTransfers{};
  /// The currently running transfers of partitions shards during shuffle stages
  std::vector<RunningShuffle> runningShuffles{};

  /// Marker to check that progress is made even if shuffles that do not take any times are the only jobs in a step
  std::size_t instantlyFinishedShuffles = 0;

  /// The partially collected partitions of a shuffle
  std::unordered_map<LocatedPartition, ShuffleOutPartition> collectedShuffles;
  /// PartitionManager that keeps track of localization of all partitions
  PartitionManager partitions;
  /// Partial partitions that are currently being created but are not fully present yet
  /// Value shows the percentage of the partition being present
  using PartialPartitions = std::unordered_map<NodeId, std::unordered_map<Partition, double>>;
  PartialPartitions partialPartitions;
  /// A map to store the first node that has a partition
  std::unordered_map<Partition, NodeId> firstPartialPartitionNodes;

  /// MemorySimulator that keeps track of the available memory at each node
  MemorySimulator memorySimulator;
  /// A network simulator to compute transfer times
  NetworkSimulator networkSimulator;

  /// The remaining time each node has to wait until disk writes complete
  std::vector<double> diskTimes;
  /// The remaining partitions that need to be written to disk
  std::vector<Partition> pendingDiskWrites;

  /// The cluster to simulate on
  const Cluster* cluster;
  /// The query plan
  const DistributedQueryPlan* plan;
  /// The node on which the result should be present.
  /// If it is set, the result has to be single node and on that node.
  /// If there is no result node, the result may have any partitioning
  std::optional<NodeId> resultNode;
  /// Settings for the simulator execution
  SimulatorSettings settings;

  /// The simulated elapsed time in seconds
  double runtime = 0;
  /// The number of executed steps
  uint64_t nSteps = 0;
  /// Flag to indicate whether simulation is done
  bool done = false;
  /// Flag to indicate whether simulation is aborted
  bool aborted = false;
  /// Abort message
  std::string abortMessage{};

  /// Tolerance for finishing times
  static constexpr double timeTolerance = 1e-12;
  /// Tolerance for progress
  static constexpr double progressTolerance = 1e-12;

 public:
  /// Constructor
  SimulatorImpl(const Cluster& cluster, const TaskAssignment* solution, const Partitioning& partitioning,
                const DistributedQueryPlan* plan, std::optional<NodeId> resultNode, SimulatorSettings settings);
  /// Check whether input is consistent and execution is possible
  void consistencyCheck() const;
  /// Run the simulation
  Simulator::Result simulate();

 public:
  /// Check whether the simulation is still running
  bool isRunning() const;
  /// Find ready tasks and transfers
  void findReadyJobs();
  /// Update update progress limits for running partial jobs and suspend tasks that need to wait for input
  void updatePartialJobs();
  /// Run on step in the simulation (a step can have arbitrary simulated running time and ends when a job finishes)
  void runStep(bool allowEmptyTasks = false);
  /// Finish a step
  void finishStep(bool allowEmptyTasks, size_t finishingJobs);
  /// Compute the remaining times for all tasks
  std::vector<double> taskRuntimes();
  /// Compute the remaining times for all transfers and shuffles
  std::pair<std::vector<double>, std::vector<double>> transferRuntimes();
  /// Compute the complete execution time of a task on a node when it does not have to share performance
  double executionTime(const Task& task, double nCores, std::optional<double> slowdownFactor,
                       std::optional<std::size_t> nLinearScaling, std::optional<double> discountFactor);
  /// Compute the size of a partition
  double partitionSize(const Partition& partition) const;
  /// Add a percentage of a partially present partition
  void updatePartialPartition(Partition partition, NodeId node, double delta);
  /// Start shuffles
  void startShuffle(ShuffleStage stage, NodeId sourceNode, Partition sourcePartition, double progressLimit);
  void startShuffle(ShuffleStage stage, NodeId sourceNode, NodeId targetNode, Partition sourcePartition,
                    std::vector<Partition> targetPartitions, double progressLimit, double progress);
  /// Finish a shuffle
  void finishShuffle(const RunningShuffle& shuffle);
  /// Get information about why simulation got stuck
  std::string getStuckInfo() const;

  /// The fraction of data that is partially available
  double partiallyAvailableFraction(Partition partition, NodeId node) const;
  /// Compute the number of bytes of a partition that are partially available at a node
  double partiallyAvailableBytes(Partition partition, NodeId node) const;
  /// Check whether a task can be started partially and to what fraction it can be completed
  std::pair<bool, double> checkPartiallyReady(const TaskAssignment::LocatedTask& task, double previousProgress) const;
  /// Check whether a transfer can be started partially and to what fraction it can be completed
  std::pair<bool, double> checkPartiallyReady(const TaskAssignment::Transfer& task, double previousProgress) const;

  /// Check whether all inputs of a task are present anywhere in the cluster
  bool inputAvailable(const Task& task) const;

  /// Get the skew factor of a partition if one exists
  std::optional<double> getSkewFactor(const Partition& partition) const;

  double inputFraction(const RunningShuffle& shuffle) const;
  bool shuffleWaitingForInput(const RunningShuffle& shuffle, double minimumBlockSize) const;
  /// All bytes that will be transferred by this running shuffle
  double bytesTransferred(const RunningShuffle& shuffle) const;

  /// The time it takes to write a specific amount of bytes to disk
  double getDiskWritingTime(double bytes) const;
  /// At the end of the simulation, wait for outstanding disk writing times
  void waitForDisk();
};
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

#pragma once
//----------------------------------------------------------------------------------------------------------------------
#include <json_fwd.hpp>
#include <string>

#include "src/SimulatorImpl.hpp"
#include "src/TempFile.hpp"
#include "src/distributedplan/ProblemWriter.hpp"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2022 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
struct SimulatorRecording {
  std::unique_ptr<TempFile> states;
  AssignmentProblem problem;
  TaskAssignment taskAssignment;
  nlohmann::json toJson() const;
};
//----------------------------------------------------------------------------------------------------------------------
/// A serializable struct that keeps the state of the simulation
struct SimulatorState {
  /// The tasks that have not started yet
  std::vector<TaskAssignment::LocatedTask> pendingTasks;
  /// The transfers that have not started yet
  std::vector<TaskAssignment::Transfer> pendingTransfers;
  /// The input partitions that need to be shuffled
  std::vector<Partition> pendingShuffles;
  /// A map to look up the targets of shuffle partitions
  std::unordered_map<Partition, std::vector<NodeId>> shuffleTargets;
  /// The currently running tasks
  std::vector<SimulatorImpl::RunningTask> runningTasks;
  /// The currently running transfers
  std::vector<SimulatorImpl::RunningTransfer> runningTransfers;
  /// The currently running transfers of partitions shards during shuffle stages
  std::vector<SimulatorImpl::RunningShuffle> runningShuffles;
  /// The partially collected partitions of a shuffle
  std::unordered_map<LocatedPartition, SimulatorImpl::ShuffleOutPartition> collectedShuffles;
  /// Partitioning keeps track of localization of all partitions
  Partitioning partitions;
  /// The available memory at each node
  std::vector<double> freeMemory;
  /// The simulated elapsed time in seconds
  double runtime;
  /// The number of executed steps
  uint64_t nSteps;
  /// Flag to indicate whether simulation is done
  bool done;
  /// Flag to indicate whether simulation is aborted
  bool aborted;
  /// Abort message
  std::string abortMessage;
  /// Serialize to json
  nlohmann::json toJson() const;
};
//----------------------------------------------------------------------------------------------------------------------
class SimulationRecorder {
  SimulatorImpl simulator;

 public:
  SimulationRecorder(SimulatorImpl simulator) : simulator(std::move(simulator)) {}
  SimulatorState getState() const;
  /// Run the simulation and record the states
  SimulatorRecording recordSimulation(const TaskAssignment& solution, const AssignmentProblem& problem);
};
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

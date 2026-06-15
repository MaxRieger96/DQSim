#include "src/SimulatorRecording.hpp"

#include <ios>
#include <json.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "src/SimulatorImpl.hpp"
#include "src/TempFile.hpp"
#include "src/distributedplan/ProblemWriter.hpp"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2022 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
namespace {
//----------------------------------------------------------------------------------------------------------------------
nlohmann::json runningTaskToJson(const SimulatorImpl::RunningTask& value) {
  nlohmann::json json;
  json["task"] = value.task.toJson();
  json["progress"] = value.progress;
  json["progressLimit"] = value.progressLimit;
  json["waitingForInput"] = value.waitingForInput;
  return json;
}
//----------------------------------------------------------------------------------------------------------------------
nlohmann::json runningTransferToJson(const SimulatorImpl::RunningTransfer& value) {
  nlohmann::json json;
  json["transfer"] = value.transfer.toJson();
  json["progress"] = value.progress;
  json["progressLimit"] = value.progressLimit;
  json["waitingForInput"] = value.waitingForInput;
  return json;
}
//----------------------------------------------------------------------------------------------------------------------
nlohmann::json shuffleOutPartitionToJson(const SimulatorImpl::ShuffleOutPartition& value) {
  nlohmann::json json;
  json["partition"] = value.partition.toJson();
  json["node"] = value.node.t;
  json["progress"] = value.progress;
  return json;
}
//----------------------------------------------------------------------------------------------------------------------
nlohmann::json runningShuffleToJson(const SimulatorImpl::RunningShuffle& value) {
  nlohmann::json json;
  // json["targetPartition"] = value.targetPartition.toJson();
  // json["sourcePartition"] = value.sourcePartition.toJson();
  json["from"] = value.from.t;
  json["to"] = value.to.t;
  // json["shardFraction"] = value.shardFraction;
  json["progress"] = value.progress;
  // json["progressLimit"] = value.progressLimit;
  // json["waitingForInput"] = value.waitingForInput;
  return json;
}
//----------------------------------------------------------------------------------------------------------------------
}  // namespace
//----------------------------------------------------------------------------------------------------------------------
SimulatorState SimulationRecorder::getState() const {
  std::vector<TaskAssignment::LocatedTask> pendingTasksCopy;
  pendingTasksCopy.reserve(simulator.pendingTasks.size());
  for (const auto* t : simulator.pendingTasks) {
    pendingTasksCopy.push_back(*t);
  }
  std::vector<TaskAssignment::Transfer> pendingTransfersCopy;
  pendingTransfersCopy.reserve(simulator.pendingTransfers.size());
  for (const auto* t : simulator.pendingTransfers) {
    pendingTransfersCopy.push_back(*t);
  }
  std::unordered_map<Partition, std::vector<NodeId>> shuffleTargetCopy;
  for (const auto& [p, t] : simulator.shuffleTargets) {
    shuffleTargetCopy[p] = t;
  }
  std::vector<double> freeMemory(simulator.memorySimulator.getFreeMemory().begin(),
                                 simulator.memorySimulator.getFreeMemory().end());
  return SimulatorState{
      pendingTasksCopy,
      pendingTransfersCopy,
      simulator.pendingShuffles,
      shuffleTargetCopy,
      simulator.runningTasks,
      simulator.runningTransfers,
      simulator.runningShuffles,
      simulator.collectedShuffles,
      simulator.partitions.getPartitioning(),
      freeMemory,
      simulator.runtime,
      simulator.nSteps,
      simulator.done,
      simulator.aborted,
      simulator.abortMessage,
  };
}
//----------------------------------------------------------------------------------------------------------------------
SimulatorRecording SimulationRecorder::recordSimulation(const TaskAssignment& assignment,
                                                        const AssignmentProblem& problem) {
  auto states = std::make_unique<TempFile>();
  auto& stateWriter = states->getWriteStream();
  stateWriter << getState().toJson().dump();
  stateWriter << ",";
  while (simulator.isRunning()) {
    simulator.findReadyJobs();
    stateWriter << getState().toJson().dump();
    stateWriter << ",";
    simulator.runStep();
    simulator.updatePartialJobs();
  }
  simulator.waitForDisk();
  stateWriter << getState().toJson().dump();
  return {std::move(states), problem, assignment};
}
//----------------------------------------------------------------------------------------------------------------------
nlohmann::json SimulatorState::toJson() const {
  nlohmann::json json;

  json["pendingTasks"] = nlohmann::json::array();
  for (const auto& t : pendingTasks) json["pendingTasks"].push_back(t.toJson());

  json["pendingTransfers"] = nlohmann::json::array();
  for (const auto& t : pendingTransfers) json["pendingTransfers"].push_back(t.toJson());

  json["pendingShuffles"] = nlohmann::json::array();
  for (const auto& t : pendingTasks) json["pendingShuffles"].push_back(t.toJson());

  json["shuffleTargets"] = nlohmann::json::array();
  for (const auto& [partition, nodes] : shuffleTargets) {
    nlohmann::json entry;
    entry["partition"] = partition.toJson();
    entry["nodes"] = nlohmann::json::array();
    for (const auto& node : nodes) entry["nodes"].push_back(node.t);
    json["shuffleTargets"].push_back(entry);
  }

  json["runningTasks"] = nlohmann::json::array();
  for (const auto& t : runningTasks) json["runningTasks"].push_back(runningTaskToJson(t));

  json["runningTransfers"] = nlohmann::json::array();
  for (const auto& t : runningTransfers) json["runningTransfers"].push_back(runningTransferToJson(t));

  json["runningShuffles"] = nlohmann::json::array();
  for (const auto& s : runningShuffles) json["runningShuffles"].push_back(runningShuffleToJson(s));

  json["collectedShuffles"] = nlohmann::json::array();
  for (const auto& [locatedPartition, shuffleOut] : collectedShuffles) {
    nlohmann::json entry;
    entry["locatedPartition"] = locatedPartition.toJson();
    entry["shuffleOutPartition"] = shuffleOutPartitionToJson(shuffleOut);
    json["collectedShuffles"].push_back(entry);
  }

  json["partitions"] = partitions.toJson();
  json["freeMemory"] = freeMemory;
  json["runtime"] = runtime;
  json["nSteps"] = nSteps;
  json["done"] = done;
  json["aborted"] = aborted;
  json["abortMessage"] = abortMessage;

  return json;
}
//----------------------------------------------------------------------------------------------------------------------
nlohmann::json SimulatorRecording::toJson() const {
  nlohmann::json json;

  // Read the comma-separated state dumps from the TempFile and wrap in an array
  auto reader = states->getReadStream();
  std::string statesContent(states->size(), '\0');
  reader->read(statesContent.data(), static_cast<std::streamsize>(states->size()));
  json["states"] = nlohmann::json::parse("[" + statesContent + "]");

  json["problem"] = problem.toJson();
  json["taskAssignment"] = taskAssignment.toJson();

  return json;
}
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

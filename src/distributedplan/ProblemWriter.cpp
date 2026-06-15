//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2022 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
#include "src/distributedplan/ProblemWriter.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <json.hpp>
#include <json_fwd.hpp>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "src/CustomTypes.hpp"
#include "src/distributedplan/Pipeline.hpp"
#include "src/partitioning/Partition.hpp"
#include "src/partitioning/PartitionLayout.hpp"
#include "src/partitioning/PartitionManager.hpp"
#include "src/partitioning/Partitioner.hpp"
#include "src/tasks/Task.hpp"
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
// TaskAssignment::LocatedTask
//----------------------------------------------------------------------------------------------------------------------
nlohmann::json TaskAssignment::LocatedTask::toJson() const {
  nlohmann::json json;
  json["task"] = task.toJson();
  json["node"] = node.t;
  return json;
}
//----------------------------------------------------------------------------------------------------------------------
TaskAssignment::LocatedTask::LocatedTask(const nlohmann::json& json)
    : task(json.at("task")), node(json.at("node").get<uint64_t>()) {}
//----------------------------------------------------------------------------------------------------------------------
// TaskAssignment::Transfer
//----------------------------------------------------------------------------------------------------------------------
nlohmann::json TaskAssignment::Transfer::toJson() const {
  nlohmann::json json;
  json["partition"] = partition.toJson();
  json["from"] = from.t;
  json["to"] = to.t;
  return json;
}
//----------------------------------------------------------------------------------------------------------------------
TaskAssignment::Transfer::Transfer(const nlohmann::json& json)
    : partition(json.at("partition")), from(json.at("from").get<uint64_t>()), to(json.at("to").get<uint64_t>()) {}
//----------------------------------------------------------------------------------------------------------------------
// TaskAssignment::ShuffleTarget
//----------------------------------------------------------------------------------------------------------------------
nlohmann::json TaskAssignment::ShuffleTarget::toJson() const {
  nlohmann::json json;
  json["partition"] = partition.toJson();
  json["location"] = location.t;
  return json;
}
//----------------------------------------------------------------------------------------------------------------------
TaskAssignment::ShuffleTarget::ShuffleTarget(const nlohmann::json& json)
    : partition(json.at("partition")), location(json.at("location").get<uint64_t>()) {}
//----------------------------------------------------------------------------------------------------------------------
// TaskAssignment
//----------------------------------------------------------------------------------------------------------------------
nlohmann::json TaskAssignment::toJson() const {
  nlohmann::json json;
  json["locatedTasks"] = nlohmann::json::array();
  for (const auto& lt : locatedTasks) {
    json["locatedTasks"].push_back(lt.toJson());
  }
  json["transfers"] = nlohmann::json::array();
  for (const auto& t : transfers) {
    json["transfers"].push_back(t.toJson());
  }
  json["shuffleTargets"] = nlohmann::json::array();
  for (const auto& st : shuffleTargets) {
    json["shuffleTargets"].push_back(st.toJson());
  }
  if (durablePartitions) {
    json["durablePartitions"] = nlohmann::json::array();
    for (const auto& p : *durablePartitions) {
      json["durablePartitions"].push_back(p.toJson());
    }
  }
  return json;
}
//----------------------------------------------------------------------------------------------------------------------
TaskAssignment::TaskAssignment(const nlohmann::json& json) {
  for (const auto& lt : json.at("locatedTasks")) {
    locatedTasks.push_back(LocatedTask(lt));
  }
  for (const auto& t : json.at("transfers")) {
    transfers.push_back(Transfer(t));
  }
  for (const auto& st : json.at("shuffleTargets")) {
    shuffleTargets.push_back(ShuffleTarget(st));
  }
  if (json.contains("durablePartitions") && !json.at("durablePartitions").is_null()) {
    durablePartitions = std::vector<Partition>();
    for (const auto& p : json.at("durablePartitions")) {
      durablePartitions->push_back(Partition(p));
    }
  }
}
//----------------------------------------------------------------------------------------------------------------------
// AssignmentProblem
//----------------------------------------------------------------------------------------------------------------------
nlohmann::json AssignmentProblem::toJson() const {
  nlohmann::json json;
  json["cluster"] = cluster.toJson();
  json["plan"] = plan.toJson();
  json["initialPartitioning"] = partitioning.toJson();

  json["tasks"] = nlohmann::json::array();
  for (const auto& [pipelineId, taskVec] : tasks) {
    nlohmann::json entry;
    entry["k"] = pipelineId.t;
    entry["v"] = nlohmann::json::array();
    for (const auto& task : taskVec) {
      entry["v"].push_back(task.toJson());
    }
    json["tasks"].push_back(entry);
  }

  json["broadcastTasks"] = nlohmann::json::array();
  for (const auto& [pipelineId, task] : broadcastTasks) {
    nlohmann::json entry;
    entry["k"] = pipelineId.t;
    entry["v"] = task.toJson();
    json["broadcastTasks"].push_back(entry);
  }

  json["name"] = name;

  if (resultNode) {
    json["resultNode"] = resultNode->t;
  } else {
    json["resultNode"] = nullptr;
  }

  return json;
}
//----------------------------------------------------------------------------------------------------------------------
AssignmentProblem::AssignmentProblem(const nlohmann::json& json)
    : cluster(json.at("cluster")),
      plan(json.at("plan")),
      partitioning(json.at("initialPartitioning")),
      name(json.at("name").get<std::string>()),
      resultNode() {
  for (const auto& entry : json.at("tasks")) {
    PipelineId pipelineId(entry.at("k").get<uint64_t>());
    std::vector<Task> taskVec;
    for (const auto& t : entry.at("v")) {
      taskVec.push_back(Task(t));
    }
    tasks[pipelineId] = std::move(taskVec);
  }
  for (const auto& entry : json.at("broadcastTasks")) {
    PipelineId pipelineId(entry.at("k").get<uint64_t>());
    broadcastTasks[pipelineId] = Task(entry.at("v"));
  }
  if (json.contains("resultNode") && !json.at("resultNode").is_null())
    resultNode = NodeId(json.at("resultNode").get<uint64_t>());
}
//----------------------------------------------------------------------------------------------------------------------
// ProblemWriter
//----------------------------------------------------------------------------------------------------------------------
std::vector<Partition> ProblemWriter::getRequired(const Pipeline& pipeline, std::optional<uint64_t> partitionIndex) {
  std::vector<Partition> required;
  for (auto du : pipeline.requiredData) {
    auto nRequiredPartitions = plan->dataUnits[du].partitionLayout->nPartitions;
    if (plan->dataUnits[du].partitionLayout->isPartitioned()) {
      if (partitionIndex) {
        required.emplace_back(du, *partitionIndex);
      } else {
        for (uint64_t i = 0; i < nRequiredPartitions; ++i) required.emplace_back(du, i);
      }
    } else {
      required.emplace_back(du, 0);
    }
  }
  return required;
};
//----------------------------------------------------------------------------------------------------------------------
void ProblemWriter::buildTasks() {
  assert(tasks == std::nullopt);
  assert(broadcastTasks == std::nullopt);
  tasks = std::unordered_map<PipelineId, std::vector<Task>>();
  broadcastTasks = std::unordered_map<PipelineId, Task>();
  for (const Pipeline& pipeline : plan->getPipelines()) {
    auto nInPartitions = plan->dataUnits[pipeline.scan].partitionLayout->nPartitions;
    auto nOutPartitions = plan->dataUnits[pipeline.output].partitionLayout->nPartitions;
    // normal data parallel pipeline
    if (plan->getDataUnit(pipeline.scan).partitionLayout->isPartitioned()) {
      std::vector<Task> currentTasks;
      currentTasks.reserve(nInPartitions);
      for (uint64_t partitionIndex = 0; partitionIndex < nInPartitions; ++partitionIndex) {
        // compute required partitions to execute this task based on distribution requirements
        currentTasks.emplace_back(pipeline.id, Partition{pipeline.scan, partitionIndex},
                                  getRequired(pipeline, partitionIndex), Partition{pipeline.output, partitionIndex},
                                  false);
      }
      (*tasks)[pipeline.id] = std::move(currentTasks);
    } else {
      auto outType = plan->getDataUnit(pipeline.output).partitionLayout->type;
      if (outType == PartitionLayoutType::Broadcast) {
        // Broadcast task
        (*broadcastTasks)[pipeline.id] = Task{pipeline.id, Partition{pipeline.scan, 0},
                                              getRequired(pipeline, std::nullopt), Partition(pipeline.output, 0), true};
      } else if (outType == PartitionLayoutType::SingleNode) {
        // Only a single task to compute the single output partition
        (*tasks)[pipeline.id] = {Task{pipeline.id, Partition{pipeline.scan, 0}, getRequired(pipeline, std::nullopt),
                                      Partition(pipeline.output, 0), false}};
      } else if (outType == PartitionLayoutType::HashPartitioned || outType == PartitionLayoutType::Scattered) {
        // Create one task for each output partition
        std::vector<Task> currentTasks;
        currentTasks.reserve(nOutPartitions);
        for (uint64_t partitionIndex = 0; partitionIndex < nOutPartitions; ++partitionIndex) {
          currentTasks.emplace_back(pipeline.id, Partition{pipeline.scan, 0}, getRequired(pipeline, partitionIndex),
                                    Partition{pipeline.output, partitionIndex}, false);
        }
        (*tasks)[pipeline.id] = std::move(currentTasks);
      }
    }
  }
}
//----------------------------------------------------------------------------------------------------------------------
void ProblemWriter::distributeBaseRelations(Partitioner& planner) {
  initialPartitioning = std::make_unique<PartitionManager>(*cluster);
  for (DataUnitId id{0}; id < plan->getDataUnits().size(); ++id) {
    const DataUnit& du = plan->getDataUnit(id);
    auto nPartitions = du.partitionLayout->nPartitions;
    assert(nPartitions);
    if (!du.isIntermediate) {
      // assert(du.partitionLayout->type == );
      if (du.partitionLayout->type == PartitionLayoutType::Broadcast) {
        assert(nPartitions == 1);
        Partition partition{du.id, 0};
        for (std::size_t i = 0; i < cluster->getComputeNodes().size(); ++i) {
          if (i == 0) {
            initialPartitioning->registerPartition(partition, NodeId{i});
          } else {
            initialPartitioning->cachePartition(partition, NodeId{i});
          }
        }
      } else {
        planner.assignPartitions(*initialPartitioning, *cluster, nPartitions, du);
      }
    }
  }
}
//----------------------------------------------------------------------------------------------------------------------
ProblemWriter::ProblemWriter(DistributedQueryPlan& plan, const Cluster& cluster, std::string name,
                             std::optional<NodeId> resultNode)
    : plan(&plan),
      cluster(&cluster),
      initialPartitioning(std::make_unique<PartitionManager>(cluster)),
      name(std::move(name)),
      resultNode(std::move(resultNode)) {}
//----------------------------------------------------------------------------------------------------------------------
void ProblemWriter::loadBaseRelationPartitioning(const Partitioning& partitioning) {
  initialPartitioning = std::make_unique<PartitionManager>(PartitionManager::loadPartitioning(partitioning));
}
//----------------------------------------------------------------------------------------------------------------------
AssignmentProblem ProblemWriter::getProblem() {
  assert(tasks);
  assert(broadcastTasks);
  return {*cluster, *plan, initialPartitioning->getPartitioning(), *tasks, *broadcastTasks, name, resultNode};
}
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

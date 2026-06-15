#include "src/distributedplan/DistributedQueryPlan.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <ios>
#include <iterator>
#include <json.hpp>
#include <json_fwd.hpp>
#include <optional>
#include <ostream>
#include <print>
#include <random>
#include <ranges>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

#include "src/Cluster.hpp"
#include "src/CustomTypes.hpp"
#include "src/distributedplan/DataUnit.hpp"
#include "src/distributedplan/Pipeline.hpp"
#include "src/partitioning/PartitionLayout.hpp"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2022 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace {
//----------------------------------------------------------------------------------------------------------------------
static thread_local std::mt19937 gen(42);
//----------------------------------------------------------------------------------------------------------------------
void formatBytes(std::ostream& out, double bytes) {
  const char* sizes[] = {"B", "KB", "MB", "GB", "TB", "PB", "EB"};
  int order = 0;

  while (bytes >= 1024 && order < static_cast<int>(std::size(sizes)) - 1) {
    bytes /= 1024;
    ++order;
  }

  out << std::fixed << std::setprecision(2) << bytes << " " << sizes[order];
}
//----------------------------------------------------------------------------------------------------------------------
}  // namespace
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
std::span<DataUnit> DistributedQueryPlan::getDataUnits() { return dataUnits; }
//----------------------------------------------------------------------------------------------------------------------
DataUnit& DistributedQueryPlan::getDataUnit(DataUnitId dataUnitId) {
  assert(dataUnitId < dataUnits.size());
  assert(dataUnitId == dataUnits[dataUnitId].id);
  return dataUnits[dataUnitId];
}
//----------------------------------------------------------------------------------------------------------------------
std::ranges::iota_view<uint64_t, uint64_t> DistributedQueryPlan::getDataUnitIds() {
  return std::ranges::iota_view<uint64_t, uint64_t>{0, dataUnits.size()};
}
//----------------------------------------------------------------------------------------------------------------------
const std::vector<Pipeline>& DistributedQueryPlan::getPipelines() const { return pipelines; }
//----------------------------------------------------------------------------------------------------------------------
std::vector<DataUnitId> DistributedQueryPlan::getBaseRelations() const {
  std::vector<DataUnitId> result;
  for (const auto& du : dataUnits) {
    // Assume id 0 is always result relation
    if (du.id != DataUnitId{0} && !du.isIntermediate) {
      result.push_back(du.id);
    }
  }
  return result;
}
//----------------------------------------------------------------------------------------------------------------------
DataUnitId DistributedQueryPlan::getResultDataUnit() const {
  auto lastPipelineOutput = getPipeline(dependencyOrder.back()).output;
  DataUnitId result = lastPipelineOutput;
  for (const auto& shuffleStage : shuffleStages) {
    if (shuffleStage.from == lastPipelineOutput) {
      assert(result == lastPipelineOutput);
      result = shuffleStage.to;
    }
  }
  /// The output relation should always be single node
  // assert(getDataUnit(result).partitionLayout->type == PartitionLayoutType::SingleNode);
  // assert(getDataUnit(result).partitionLayout->nPartitions == 1);
  // assert(!getDataUnit(result).partitionLayout->isPartitioned());
  return result;
}
//----------------------------------------------------------------------------------------------------------------------
const Pipeline& DistributedQueryPlan::getPipeline(PipelineId pipelineId) const {
  if (pipelines[pipelineId].id != pipelineId) std::println("Internal error: Pipelines not sorted by index!");
  assert(pipelines.size() > static_cast<size_t>(pipelineId));
  assert(pipelines[pipelineId].id == pipelineId);
  return pipelines[pipelineId];
}
//----------------------------------------------------------------------------------------------------------------------
void DistributedQueryPlan::toDOT(std::ostream& out) const
/// Print the plan in DOT format
{
  out << "digraph distributed_plan {\n";
  // data units
  for (const DataUnit& du : dataUnits) {
    out << "\"du" << du.id << "\" [shape = Mrecord, style = bold, label = \"{" << du.name << " (" << du.id
        << ") | size: " << du.estimatedCard << "(";
    formatBytes(out, du.estimatedCard * du.tupleSize());
    out << ") | ";
    if (du.partitionLayout.has_value()) du.partitionLayout->prettyPrint(out);
    out << " | ";
    out << " | ";
    size_t nAttrs = 1;
    for (const auto& attr : du.attributes) {
      out << attr.name << ", ";
      if (nAttrs++ >= 3) {
        nAttrs = 1;
        out << "\\n";
      }
    }
    out << "}\"]\n";
  }
  // pipelines
  for (const Pipeline& pipeline : pipelines) {
    const double referenceRuntime = estimateRuntime(16, pipeline.id, {}, {}, {});
    out << "\"p" << pipeline.id << "\" [shape = record, label = \"{Pipeline " << pipeline.id << " | "
        << pipeline.description << " | load: " << std::scientific << pipeline.estimatedLoad << " | "
        << std::defaultfloat << referenceRuntime << "s}\"]\n";
  }
  // edges between pipelines and data units
  for (const Pipeline& pipeline : pipelines) {
    out << "du" << pipeline.scan << " -> p" << pipeline.id << "\n";
    out << "p" << pipeline.id << " -> du" << pipeline.output << "\n";
    for (DataUnitId id : pipeline.requiredData) {
      out << "du" << id << " -> p" << pipeline.id << " [color = \"grey\", arrowhead = onormal]\n";
    }
  }
  // shuffle stages
  for (const ShuffleStage& shuffle : shuffleStages) {
    out << "du" << shuffle.from << " -> du" << shuffle.to << " [label = \"shuffle\"]\n";
  }
  out << "}\n";
}
//----------------------------------------------------------------------------------------------------------------------
const DataUnit& DistributedQueryPlan::getDataUnit(DataUnitId dataUnitId) const {
  return const_cast<DistributedQueryPlan&>(*this).getDataUnit(dataUnitId);
}
//----------------------------------------------------------------------------------------------------------------------
void DistributedQueryPlan::verifyIndexes() const {
  for (size_t i = 0; i < dataUnits.size(); ++i) {
    if (dataUnits[i].id != DataUnitId{static_cast<uint64_t>(i)})
      std::println(stderr, "[ERROR] verifyIndexes: dataUnits[{}].id = {} does not match expected index", i,
                   dataUnits[i].id.t);
    assert(dataUnits[i].id == DataUnitId{static_cast<uint64_t>(i)});
  }
  for (size_t i = 0; i < pipelines.size(); ++i) {
    if (pipelines[i].id != PipelineId{static_cast<uint64_t>(i)})
      std::println(stderr, "[ERROR] verifyIndexes: pipelines[{}].id = {} does not match expected index", i,
                   pipelines[i].id.t);
    assert(pipelines[i].id == PipelineId{static_cast<uint64_t>(i)});
  }
}
//----------------------------------------------------------------------------------------------------------------------
void DistributedQueryPlan::consistencyCheck() const {
  verifyIndexes();
  // Only the result DU is not used
  // Each DU is created by at most one pipeline or shuffle
  std::vector<bool> usedDUs(dataUnits.size(), false);
  std::vector<bool> foundCreator(dataUnits.size(), false);
  // check that data unit partitioning makes sense
  for (const auto& du : dataUnits) {
    const auto& layout = du.partitionLayout;
    if (layout->type == PartitionLayoutType::Broadcast || layout->type == PartitionLayoutType::SingleNode) {
      if (layout->nPartitions != 1) {
        std::println(stderr, "[ERROR] du {} is supposed to be broadcast or single node but has {} partitions", du.id.t,
                     layout->nPartitions);
      }
      assert(layout->nPartitions == 1);
    }
  }
  // dependency order checks
  if (dependencyOrder.size() != pipelines.size()) {
    std::println(stderr, "[ERROR] dependencyOrder.size() = {} != pipelines.size() = {}", dependencyOrder.size(),
                 pipelines.size());
  }
  assert(dependencyOrder.size() == pipelines.size());
  {
    std::unordered_set<DataUnitId> availableDUs;
    for (const auto& du : dataUnits) {
      if (!du.isIntermediate) {
        availableDUs.insert(du.id);
      }
    }

    auto runShuffles = [&]() {
      for (const auto& s : shuffleStages) {
        if (availableDUs.contains(s.from)) {
          availableDUs.insert(s.to);
        }
      }
    };

    runShuffles();
    for (std::size_t i = 0; i < dependencyOrder.size(); ++i) {
      if (pipelines.size() <= dependencyOrder[i].t) {
        std::println(stderr, "[ERROR] Pipeline {} (dependency order {}) is out of bounds. Available pipelines: {}",
                     dependencyOrder[i].t, i, pipelines.size());
      }
      assert(pipelines.size() > dependencyOrder[i].t);
      const Pipeline& p = pipelines[dependencyOrder[i]];
      for (const auto& du : p.getInputDUs()) {
        if (!availableDUs.contains(du)) {
          std::vector<DataUnitId> availableDUsVec(availableDUs.begin(), availableDUs.end());
          std::ranges::sort(availableDUsVec);
          auto availableDUsStr = std::ranges::fold_left(
              availableDUsVec | std::views::transform([](const DataUnitId& i) { return std::to_string(i.t); }),
              std::string{}, [first = true](std::string acc, const std::string& s) mutable {
                if (!first) acc += ", ";
                first = false;
                return acc + s;
              });
          std::println(stderr, "[ERROR] Pipeline {} requires data unit {} which is not available.\n AvailableDUs: {}",
                       p.id.t, du.t, availableDUsStr);
        }
        assert(availableDUs.contains(du));
        availableDUs.insert(p.output);
        runShuffles();
      }
    }
  }
  assert(dependencyOrder.size() == pipelines.size());
  for (const auto& p : pipelines) {
    assert(p.scan < dataUnits.size());
    usedDUs[p.scan] = true;
    for (const auto& req : p.requiredData) {
      assert(req < dataUnits.size());
      usedDUs[req] = true;
    }
    if (!dataUnits[p.output].isIntermediate)
      std::println(stderr, "[ERROR] data unit {} claims to be not intermediate, but it is created by pipeline {}",
                   p.output.t, p.id.t);
    assert(dataUnits[p.output].isIntermediate);
    if (foundCreator[p.output])
      std::println(stderr,
                   "[ERROR] pipeline {} claims output data unit {}, but a creator was already registered for it",
                   p.id.t, p.output.t);
    assert(!foundCreator[p.output]);
    assert(p.output < dataUnits.size());
    foundCreator[p.output] = true;
  }

  for (const auto& shuffle : shuffleStages) {
    usedDUs[shuffle.from] = true;
    if (foundCreator[shuffle.to])
      std::println(stderr, "[ERROR] shuffle claims output data unit {}, but a creator was already registered for it",
                   shuffle.to.t);
    assert(!foundCreator[shuffle.to]);
    assert(shuffle.to < dataUnits.size());
    foundCreator[shuffle.to] = true;
    if (!dataUnits[shuffle.to].isIntermediate)
      std::println(stderr, "[ERROR] data unit {} claims to be not intermediate, but it is created by shuffle",
                   shuffle.to.t);
    assert(dataUnits[shuffle.to].isIntermediate);
  }

  for (std::size_t iDU = 0; iDU < usedDUs.size(); ++iDU) {
    if (!usedDUs[iDU] && getResultDataUnit() != DataUnitId(iDU))
      std::println(stderr,
                   "[ERROR] data unit {} is never used by any pipeline and is not the result data unit (result = {})\n "
                   " Did you set the dependency order correctly?",
                   iDU, getResultDataUnit().t);
    assert(usedDUs[iDU] || getResultDataUnit() == DataUnitId(iDU));
    if (!foundCreator[iDU] && dataUnits[iDU].isIntermediate) {
      std::println(
          stderr, "[ERROR] data unit {} claims to be intermediate, but no producing pipeline or shuffle could be found",
          iDU);
    }
    assert(foundCreator[iDU] || !dataUnits[iDU].isIntermediate);
  }
}
//----------------------------------------------------------------------------------------------------------------------
bool DistributedQueryPlan::isShuffleInput(DataUnitId du) const {
  for (const auto& s : shuffleStages) {
    if (s.from == du) return true;
  }
  return false;
}
//----------------------------------------------------------------------------------------------------------------------
bool DistributedQueryPlan::isShuffleOutput(DataUnitId du) const {
  for (const auto& s : shuffleStages) {
    if (s.to == du) return true;
  }
  return false;
}
//----------------------------------------------------------------------------------------------------------------------
std::optional<DataUnitId> DistributedQueryPlan::getShuffleOutput(dqsim::DataUnitId du) const {
  for (const auto& s : shuffleStages) {
    if (s.from == du) return s.to;
  }
  return std::nullopt;
}
//----------------------------------------------------------------------------------------------------------------------
bool DistributedQueryPlan::isBroadcast(dqsim::PipelineId p) const {
  const Pipeline& pipeline = getPipeline(p);
  return getDataUnit(pipeline.scan).partitionLayout->type == PartitionLayoutType::Broadcast &&
         getDataUnit(pipeline.output).partitionLayout->type == PartitionLayoutType::Broadcast;
}
//----------------------------------------------------------------------------------------------------------------------
double DistributedQueryPlan::estimateRuntime(double nCores, PipelineId pipelineId, std::optional<double> slowdownFactor,
                                             std::optional<std::size_t> nLinearScaling,
                                             std::optional<double> discountFactor) const {
  assert(nLinearScaling.has_value() == discountFactor.has_value());
  const Pipeline& pipeline = getPipeline(pipelineId);
  auto nInPartitions = dataUnits[pipeline.scan].partitionLayout->nPartitions;
  assert(nInPartitions);
  assert(nCores > 0);
  double taskTuples = (dataUnits[pipeline.scan].estimatedCard / nInPartitions);
  if (nLinearScaling && *nLinearScaling < nCores) {
    // Model cores beyond nLinearScaling with diminishing returns.
    // Summation form:
    //   nLinearScaling + sum(discountFactor^i), for i = 1 to (nCores - nLinearScaling)
    //
    // Closed form via the finite geometric series expansion:
    //   nLinearScaling
    //     + discountFactor * (1 - discountFactor^(nCores - nLinearScaling))
    //       / (1 - discountFactor)
    assert(*nLinearScaling >= 1);
    assert(*discountFactor <= 1);
    assert(*discountFactor >= 0);
    double linearCores = std::min(static_cast<double>(*nLinearScaling), nCores);
    nCores = *discountFactor == 1.0
                 ? nCores
                 : linearCores + (*discountFactor * (1.0 - std::pow(*discountFactor, nCores - linearCores))) /
                                     (1.0 - *discountFactor);
  }
  double relativePerformance = nCores / 16.0;
  double slowdown = slowdownFactor ? *slowdownFactor : 1.0;
  return pipeline.estimatedLoad * taskTuples / relativePerformance * slowdown;
}
//----------------------------------------------------------------------------------------------------------------------
void DistributedQueryPlan::distortCardinalities(double factor, bool random) {
  for (auto& du : dataUnits) {
    if (random && !du.isIntermediate) continue;
    bool grow = std::bernoulli_distribution{0.5}(gen);
    if (!random || grow)
      du.estimatedCard *= factor;
    else
      du.estimatedCard /= factor;
  }
}
//----------------------------------------------------------------------------------------------------------------------
DistributedQueryPlan DistributedQueryPlan::forceResultSingleNode() const {
  const auto& currentResultDU = getDataUnit(getResultDataUnit());
  if (currentResultDU.partitionLayout->nPartitions == 1) return *this;
  auto resDUs = dataUnits;
  auto resPipelines = pipelines;
  auto resShuffleStages = shuffleStages;
  auto resDependencyOrder = dependencyOrder;
  resDUs[getResultDataUnit()].isIntermediate = true;
  std::size_t nextDUid = dataUnits.size();
  std::size_t nextPipelineId = pipelines.size();
  DataUnit reducedResult("res_reduced", currentResultDU.attributes, currentResultDU.partitionLayout,
                         DataUnitId(nextDUid++), currentResultDU.partitionLayout->nPartitions, true);
  DataUnit shuffledResult("shuffled_result", currentResultDU.attributes,
                          PartitionLayout(PartitionLayoutType::SingleNode, 1, {}), DataUnitId(nextDUid++), 1.0, true);
  DataUnit finalResult("final_result", currentResultDU.attributes, shuffledResult.partitionLayout,
                       DataUnitId(nextDUid++), 1.0, true);
  Pipeline reducingPipeline(PipelineId(nextPipelineId++), currentResultDU.id, reducedResult.id, {}, 0.0,
                            "result reducer");
  ShuffleStage finalShuffle(reducedResult.id, shuffledResult.id);
  Pipeline finalPipeline(PipelineId(nextPipelineId++), shuffledResult.id, finalResult.id, {}, 0.0, "result dummy");

  resDUs.push_back(reducedResult);
  resDUs.push_back(shuffledResult);
  resDUs.push_back(finalResult);

  resPipelines.push_back(reducingPipeline);
  resPipelines.push_back(finalPipeline);

  resDependencyOrder.push_back(reducingPipeline.id);
  resDependencyOrder.push_back(finalPipeline.id);

  resShuffleStages.push_back(finalShuffle);

  return DistributedQueryPlan(resDUs, resPipelines, resShuffleStages, resDependencyOrder);
}
//----------------------------------------------------------------------------------------------------------------------
nlohmann::json DistributedQueryPlan::toJson() const {
  nlohmann::json result;
  result["dataUnits"] = nlohmann::json::array();
  for (const auto& du : dataUnits) {
    result["dataUnits"].push_back(du.toJson());
  }
  result["pipelines"] = nlohmann::json::array();
  for (const auto& p : pipelines) {
    result["pipelines"].push_back(p.toJson());
  }
  result["shuffleStages"] = nlohmann::json::array();
  for (const auto& s : shuffleStages) {
    result["shuffleStages"].push_back(s.toJson());
  }
  result["dependencyOrder"] = nlohmann::json::array();
  for (const auto& p : dependencyOrder) {
    result["dependencyOrder"].push_back(p.t);
  }
  return result;
}
//----------------------------------------------------------------------------------------------------------------------
DistributedQueryPlan::DistributedQueryPlan(const nlohmann::json& json) {
  for (const auto& du : json["dataUnits"]) {
    dataUnits.push_back(DataUnit(du));
  }
  for (const auto& p : json["pipelines"]) {
    pipelines.push_back(Pipeline(p));
  }
  for (const auto& s : json["shuffleStages"]) {
    shuffleStages.push_back(ShuffleStage(s));
  }
  for (const auto& p : json["dependencyOrder"]) {
    dependencyOrder.push_back(PipelineId(p.get<uint64_t>()));
  }

  // toDOT(std::cout);
}
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

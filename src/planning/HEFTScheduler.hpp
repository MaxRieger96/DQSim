#pragma once
//----------------------------------------------------------------------------------------------------------------------
#include <cassert>
#include <cstdint>
#include <vector>

#include "src/Util.hpp"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2025 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
/// An implementation of the Heterogeneous Earliest-Finish-Time (HEFT) algorithm
class SchedulingProblem {
 public:
  /// A scheduled task
  struct ScheduledTask {
    uint64_t processor;
    double startTime;
    double finishTime;
  };

  /// Dependency between two tasks that blocks execution
  struct TaskDependency {
    /// The task toTask depends on
    uint64_t fromTask;
    /// The task that depends on fromTask
    uint64_t toTask;
    /// The amount of data that has to be shuffled for this dependency
    double data;
  };

 private:
  /// The total count of tasks
  uint64_t nTasks;
  /// The processing nodes that process tasks
  uint64_t nProcessors;
  /// Actual blocking dependencies between tasks stored as vector of task indices a tasks depends on
  std::vector<std::vector<uint64_t>> dependencies;
  /// Direct successors of a task, inverse of dependencies
  std::vector<std::vector<uint64_t>> successors;
  /// Data sent between dependent tasks (nTasks x nTasks) (from, to)
  Matrix<double> dependencyData;
  /// Computation cost matrix (nTasks x nProcessors)
  Matrix<double> computationTimes;
  /// Processor bandwidth matrix (nProcessors x nProcessors)
  Matrix<double> bandwidths;
  /// Output latencies of processors (nProcessors)
  std::vector<double> latencies;

 public:
  /// Constructor
  SchedulingProblem(uint64_t nTasks, uint64_t nProcessors, const std::vector<TaskDependency>& dependencies,
                    Matrix<double> computationTimes, Matrix<double> bandwidths, std::vector<double> latencies);
  /// Constructor
  SchedulingProblem(uint64_t nTasks, uint64_t nProcessors, const std::vector<TaskDependency>& dependencies,
                    std::vector<double> computationTimes, double bandwidth, double latency);
  /// Constructor
  SchedulingProblem(uint64_t nTasks, uint64_t nProcessors, const std::vector<TaskDependency>& dependencies,
                    Matrix<double> computationTimes, double bandwidth, double latency);
  /// Run the Scheduling
  std::vector<ScheduledTask> runHEFT() const;
  /// Run the Scheduling
  std::vector<double> getUpRank() const;

 private:
  /// Assign a list of dependencies
  void assignDependencies(const std::vector<TaskDependency>& dependencies);
  /// Assign same computation time to all nodes
  void assignUniformComputationTimes(std::vector<double> computationTimes);
  /// Assign same bandwidth to all node connections
  void assignUniformBandwidth(double bandwidth);
};
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

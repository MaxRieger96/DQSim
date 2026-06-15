#pragma once
//----------------------------------------------------------------------------------------------------------------------
#include "src/distributedplan/ProblemWriter.hpp"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2023 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
struct Cluster;
//----------------------------------------------------------------------------------------------------------------------
/// A class to compute transfer speeds
class NetworkSimulator {
 public:
  struct Connection {
    NodeId from;
    NodeId to;
  };

 private:
  /// The simulated cluster
  const Cluster* cluster;
  /// The upload speeds of nodes in bytes/s
  std::vector<double> uploadSpeeds;
  /// The download speeds of nodes in bytes/s
  std::vector<double> downloadSpeeds;

 public:
  /// Constructor
  explicit NetworkSimulator(const Cluster* cluster);
  /// Compute the speeds of transfers in bytes per second
  std::vector<double> computeTransferSpeeds(std::span<Connection> transfers) const;
};
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

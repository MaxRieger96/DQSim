//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2023 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
#include "src/network/NetworkSimulator.hpp"

#include <cstddef>
#include <span>
#include <vector>

#include "src/Util.hpp"
#include "src/network/StreamSimulator.hpp"
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
NetworkSimulator::NetworkSimulator(const Cluster* cluster) : cluster(cluster), uploadSpeeds(), downloadSpeeds() {
  uploadSpeeds.reserve(cluster->nodes.size());
  downloadSpeeds.reserve(cluster->nodes.size());
  for (const auto& node : cluster->nodes) {
    /// The network speed in bytes per second instead of gbit
    size_t networkSpeed = static_cast<size_t>(node.byteBandwidth());
    uploadSpeeds.emplace_back(networkSpeed);
    downloadSpeeds.emplace_back(networkSpeed);
  }
}
//----------------------------------------------------------------------------------------------------------------------
std::vector<double> NetworkSimulator::computeTransferSpeeds(std::span<Connection> connections) const {
  if (connections.empty()) return {};
  size_t nNodes = cluster->nodes.size();
  Matrix<size_t> connectionCounts(nNodes, nNodes);
  for (auto connection : connections) {
    connectionCounts(connection.from, connection.to) += 1;
  }

  std::vector<DirectStream> streams;
  for (size_t i = 0; i < nNodes; ++i) {
    for (size_t j = 0; j < nNodes; ++j) {
      if (connectionCounts(i, j) > 0) {
        streams.emplace_back(DirectStream{i, j});
      }
    }
  }
  std::vector<double> speeds = StreamSimulator::computeSpeedsGreedy(uploadSpeeds, downloadSpeeds, streams);

  Matrix<double> speedsM(nNodes, nNodes);
  for (size_t i = 0; i < streams.size(); ++i) {
    auto nodes = streams[i];
    speedsM(nodes.first, nodes.second) = speeds[i];
  }

  std::vector<double> result;
  result.reserve(connections.size());
  for (auto c : connections) {
    result.emplace_back(speedsM(c.from, c.to) / static_cast<double>(connectionCounts(c.from, c.to)));
  }
  return result;
}
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

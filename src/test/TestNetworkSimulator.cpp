#include <numeric>

#include "src/network/NetworkSimulator.hpp"
#include "thirdparty/doctest.h"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2023 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
using namespace dqsim;
//----------------------------------------------------------------------------------------------------------------------
static Node makeNode(uint64_t nCores, double memorySize, double networkSpeed, double networkLatency) {
  Node n;
  n.nCores = nCores;
  n.memorySize = memorySize;
  n.networkSpeed = networkSpeed;
  n.networkLatency = networkLatency;
  return n;
}
//----------------------------------------------------------------------------------------------------------------------
TEST_CASE("TestNetworkSimulator") {
  // 3 nodes, each with 1 Gbps network, 4 cores, 64 GiB memory
  std::vector<Node> nodes{makeNode(4, 64, 1, 1), makeNode(4, 64, 1, 1), makeNode(4, 64, 1, 1)};
  Cluster cluster{std::span<Node>{nodes}};
  NetworkSimulator simulator(&cluster);

  // 5 connections: node 0 → 2 (once) and node 0 → 1 (four times)
  // Node 0 has 1 Gbps upload; the bottleneck should cap total throughput at 1 Gbps
  std::vector<NetworkSimulator::Connection> connections = {
      {NodeId{0}, NodeId{2}}, {NodeId{0}, NodeId{1}}, {NodeId{0}, NodeId{1}},
      {NodeId{0}, NodeId{1}}, {NodeId{0}, NodeId{1}},
  };
  auto speeds = simulator.computeTransferSpeeds(connections);

  // Sum all byte/s speeds, convert to Gbps
  double totalSpeed = std::reduce(speeds.begin(), speeds.end()) * 8.0 / 1024.0 / 1024.0 / 1024.0;
  CHECK(totalSpeed == doctest::Approx(1.0));
}
//----------------------------------------------------------------------------------------------------------------------
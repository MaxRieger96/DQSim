#pragma once
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2022 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
#include <span>
#include <vector>
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
/// Represents a single stream
struct Stream {
  /// The bottlenecks in the order they are passed
  std::vector<size_t> bottlenecks;
  /// The factors how much one stream unit affects each bottleneck
  std::vector<double> bottleneckFactors;
};
//----------------------------------------------------------------------------------------------------------------------
/// Represents a stream passing exactly two bottlenecks
struct DirectStream {
  /// The first bottleneck
  size_t first;
  /// The second bottleneck
  size_t second;
};
//----------------------------------------------------------------------------------------------------------------------
/// Represents the streaming speeds of a stream
struct StreamResult {
  /// The speeds of the resulting stream at each bottleneck
  std::vector<double> streamingSpeedsAtBottlenecks;
  /// Equality check
  bool operator==(const StreamResult& other) const = default;
};
//----------------------------------------------------------------------------------------------------------------------
/// Simulates streams with common bottlenecks using linear programming
class StreamSimulator {
 public:
  /// Compute the speed of streams at each bottleneck
  /// The solution is required to create volume only at the start of streams
  /// But volume can be lost at any point within the stream
  static std::vector<StreamResult> computeStreamSpeeds(std::span<double const> bottlenecks,
                                                       std::span<Stream const> streams);
  /// Compute the speed of individual streams
  static std::vector<double> computeSpeeds(std::span<double const> bottlenecks, std::span<Stream const> streams);
  /// Compute the speed of individual streams using max-flow instead of linear programming
  static std::vector<size_t> computeSpeedsFlow(std::span<size_t const> uploadSpeeds,
                                               std::span<size_t const> downloadSpeeds,
                                               std::span<DirectStream const> streams);
  /// Compute the speed of individual streams using a greedy method
  static std::vector<double> computeSpeedsGreedy(std::span<double const> uploadSpeeds,
                                                 std::span<double const> downloadSpeeds,
                                                 std::span<DirectStream const> streams);
};
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

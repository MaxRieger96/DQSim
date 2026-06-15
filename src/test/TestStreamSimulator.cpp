#include "src/network/StreamSimulator.hpp"
#include "thirdparty/doctest.h"
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2022 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
TEST_CASE("TestStreamSimulator") {
  {
    std::vector<double> bottlenecks{1, 1, 2};
    std::vector<dqsim::Stream> streams{
        dqsim::Stream{.bottlenecks{0, 2}, .bottleneckFactors{1, 1}},
        dqsim::Stream{.bottlenecks{1, 2}, .bottleneckFactors{1, 1}},
    };
    auto result = dqsim::StreamSimulator::computeSpeeds(bottlenecks, streams);
    std::vector<double> expected{1, 1};
    CHECK(result == expected);
  }
  {
    std::vector<double> bottlenecks{1, 1, 4};
    std::vector<dqsim::Stream> streams{
        dqsim::Stream{.bottlenecks{0, 2}, .bottleneckFactors{1, 2}},
        dqsim::Stream{.bottlenecks{1, 2}, .bottleneckFactors{1, 2}},
    };
    auto result = dqsim::StreamSimulator::computeSpeeds(bottlenecks, streams);
    std::vector<double> expected{1, 1};
    CHECK(result == expected);
  }
  {
    std::vector<double> bottlenecks{1, 1, 1, 1};
    std::vector<dqsim::Stream> streams{
        dqsim::Stream{.bottlenecks{0, 1}, .bottleneckFactors{1, 1}},
        dqsim::Stream{.bottlenecks{2, 3}, .bottleneckFactors{1, 1}},
    };
    auto result = dqsim::StreamSimulator::computeSpeeds(bottlenecks, streams);
    std::vector<double> expected{1, 1};
    CHECK(result == expected);
  }
  {
    std::vector<double> bottlenecks{1, 1, 1};
    std::vector<dqsim::Stream> streams{
        dqsim::Stream{.bottlenecks{0, 2}, .bottleneckFactors{1, 1}},
        dqsim::Stream{.bottlenecks{1, 2}, .bottleneckFactors{1, 1}},
    };
    auto result = dqsim::StreamSimulator::computeSpeeds(bottlenecks, streams);
    CHECK(result[0] + result[1] == doctest::Approx(1.0));
    CHECK(((result[0] < 1.0) || (result[1] < 1.0)));
  }
}
//----------------------------------------------------------------------------------------------------------------------
TEST_CASE("TestStreamSimulatorFlow") {
  {
    std::vector<size_t> upload{1, 1, 0};
    std::vector<size_t> download{0, 0, 2};
    std::vector<dqsim::DirectStream> streams{dqsim::DirectStream{0, 2}, dqsim::DirectStream{1, 2}};
    auto result = dqsim::StreamSimulator::computeSpeedsFlow(upload, download, streams);
    std::vector<size_t> expected{1, 1};
    CHECK(result == expected);
  }
  {
    std::vector<size_t> upload{1, 1, 0};
    std::vector<size_t> download{0, 0, 4};
    std::vector<dqsim::DirectStream> streams{dqsim::DirectStream{0, 2}, dqsim::DirectStream{1, 2}};
    auto result = dqsim::StreamSimulator::computeSpeedsFlow(upload, download, streams);
    std::vector<size_t> expected{1, 1};
    CHECK(result == expected);
  }
  {
    std::vector<size_t> upload{1, 1, 0, 0};
    std::vector<size_t> download{0, 0, 1, 1};
    std::vector<dqsim::DirectStream> streams{dqsim::DirectStream{0, 2}, dqsim::DirectStream{1, 3}};
    auto result = dqsim::StreamSimulator::computeSpeedsFlow(upload, download, streams);
    std::vector<size_t> expected{1, 1};
    CHECK(result == expected);
  }
  {
    std::vector<size_t> upload{1, 1, 0};
    std::vector<size_t> download{0, 0, 1};
    std::vector<dqsim::DirectStream> streams{dqsim::DirectStream{0, 2}, dqsim::DirectStream{1, 2}};
    auto result = dqsim::StreamSimulator::computeSpeedsFlow(upload, download, streams);
    CHECK(result[0] + result[1] == 1);
    CHECK(((result[0] == 0 && result[1] == 1) || (result[0] == 1 && result[1] == 0)));
  }
}
//----------------------------------------------------------------------------------------------------------------------
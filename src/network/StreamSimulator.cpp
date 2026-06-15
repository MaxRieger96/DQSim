// NOLINTBEGIN
//----------------------------------------------------------------------------------------------------------------------
// boost graph library raises -Werror=maybe-uninitialized
#if defined(__clang__)
#pragma clang diagnostic push
// Clang’s uninitialized‑variable warning is called “-Wuninitialized”
#pragma clang diagnostic ignored "-Wuninitialized"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
// GCC’s roughly‑equivalent warning is “-Wmaybe-uninitialized”
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2022 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
#include "src/network/StreamSimulator.hpp"

#include <glpk.h>

#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/boykov_kolmogorov_max_flow.hpp>
#include <boost/graph/graph_utility.hpp>
#include <cassert>
#include <limits>
//----------------------------------------------------------------------------------------------------------------------
namespace {
//----------------------------------------------------------------------------------------------------------------------
using namespace boost;
//----------------------------------------------------------------------------------------------------------------------
using Traits = adjacency_list_traits<vecS, vecS, directedS>;
// clang-format off
using Graph = adjacency_list<vecS, vecS, directedS,
   property<vertex_name_t, std::string,
      property<vertex_index_t, long,
         property<vertex_color_t, boost::default_color_type,
            property<vertex_distance_t, long,
               property<vertex_predecessor_t, Traits::edge_descriptor>>>>>,
   property<edge_capacity_t, long,
      property<edge_residual_capacity_t, long,
         property<edge_reverse_t, Traits::edge_descriptor>>>>;
// clang-format on
//----------------------------------------------------------------------------------------------------------------------
struct PropertyWriter {
  void operator()(std::ostream& out) const {
    out << "graph [bgcolor=lightgrey]" << std::endl;
    out << "node [shape=circle color=white]" << std::endl;
    out << "edge [style=dashed]" << std::endl;
  }
};
//----------------------------------------------------------------------------------------------------------------------
}  // namespace
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
std::vector<StreamResult> StreamSimulator::computeStreamSpeeds(std::span<double const> bottlenecks,
                                                               std::span<Stream const> streams)
/// Compute the speed of streams at each bottleneck
{
  // Linear program
  // find        x
  // maximize    c^T x
  // subject to  Ax <= b
  // and         x >= 0

  // glpk uses 1 indexing -> expect weird index calculations in this method

  glp_term_out(GLP_OFF);

  uint64_t sizeX = 0;
  for (const auto& stream : streams) {
    sizeX += stream.bottlenecks.size();
  }

  // indices and values of matrix A
  std::vector<int> iA{0};
  std::vector<int> jA{0};
  std::vector<double> valueA{0};

  uint64_t firstXOfCurrentStream = 0;
  uint64_t nLinesOfA = bottlenecks.size() + 1;
  for (const auto& stream : streams) {
    for (uint64_t iStreamBottleneck = 0; iStreamBottleneck < stream.bottlenecks.size(); ++iStreamBottleneck) {
      uint64_t iX = firstXOfCurrentStream + iStreamBottleneck;

      // add the current x to the bottleneck
      iA.emplace_back(stream.bottlenecks[iStreamBottleneck] + 1);
      jA.emplace_back(iX + 1);
      valueA.emplace_back(stream.bottleneckFactors[iStreamBottleneck]);

      // add the respective restriction of the current x
      if (iStreamBottleneck > 0) {
        // the previous x
        iA.emplace_back(nLinesOfA);
        jA.emplace_back(iX - 1 + 1);
        valueA.emplace_back(1);
        // the current x may not be larger
        iA.emplace_back(nLinesOfA);
        jA.emplace_back(iX + 1);
        valueA.emplace_back(-1);
        ++nLinesOfA;
      }
    }
    firstXOfCurrentStream += stream.bottlenecks.size();
  }

  glp_prob* lp;
  lp = glp_create_prob();
  glp_set_obj_dir(lp, GLP_MAX);

  // columns: bounds and objective coefficients
  assert(sizeX <= std::numeric_limits<int>::max());
  glp_add_cols(lp, static_cast<int>(sizeX));
  for (int iColumn = 0; iColumn < static_cast<int>(sizeX); ++iColumn) {
    glp_set_col_bnds(lp, iColumn + 1, GLP_LO, 0, 0);
    glp_set_obj_coef(lp, iColumn + 1, 1);  // here we could add different weights for streams
  }

  // rows and bounds
  glp_add_rows(lp, static_cast<int>(nLinesOfA));
  // bottleneck values
  for (int iRow = 0; iRow < static_cast<int>(bottlenecks.size()); ++iRow) {
    glp_set_row_bnds(lp, iRow + 1, GLP_UP, 0, bottlenecks[iRow]);
  }
  // zero values for interdependencies
  for (int iRow = static_cast<int>(bottlenecks.size()); iRow < static_cast<int>(nLinesOfA); ++iRow) {
    glp_set_row_bnds(lp, iRow + 1, GLP_UP, 0, 0);
  }

  // A
  glp_load_matrix(lp, static_cast<int>(iA.size() - 1), iA.data(), jA.data(), valueA.data());

  // compute result
  int failed [[maybe_unused]] = glp_simplex(lp, nullptr);
  assert(!failed);

  // create and populate result object
  std::vector<StreamResult> result;
  result.reserve(streams.size());
  int iX = 0;
  for (const auto& stream : streams) {
    std::vector<double> currentSpeeds;
    for (uint64_t iCurrentStreamBottleneck = 0; iCurrentStreamBottleneck < stream.bottlenecks.size();
         ++iCurrentStreamBottleneck) {
      currentSpeeds.emplace_back(glp_get_col_prim(lp, iX + 1));
      ++iX;
    }
    result.push_back({.streamingSpeedsAtBottlenecks = {currentSpeeds}});
  }

  // clean up
  glp_delete_prob(lp);
  glp_free_env();
  return result;
}
//----------------------------------------------------------------------------------------------------------------------
std::vector<double> StreamSimulator::computeSpeeds(std::span<const double> bottlenecks,
                                                   std::span<const Stream> streams) {
  glp_term_out(GLP_OFF);
  // indices and values of matrix A
  std::vector<int> iA{0};
  std::vector<int> jA{0};
  std::vector<double> valueA{0};

  auto addValue = [&](size_t i, size_t j, double v) { iA.push_back(i), jA.push_back(j), valueA.push_back(v); };

  size_t iStream = 1;
  for (const auto& stream : streams) {
    for (size_t bottleneck : stream.bottlenecks) {
      addValue(bottleneck + 1, iStream, 1);
    }
    ++iStream;
  }

  glp_prob* lp;
  lp = glp_create_prob();
  glp_set_obj_dir(lp, GLP_MAX);

  // rows and bounds
  glp_add_rows(lp, static_cast<int>(bottlenecks.size()));
  // bottleneck values
  for (int iRow = 0; iRow < static_cast<int>(bottlenecks.size()); ++iRow) {
    glp_set_row_bnds(lp, iRow + 1, GLP_UP, 0, bottlenecks[iRow]);
  }

  // columns: bounds and objective coefficients
  assert(streams.size() <= std::numeric_limits<int>::max());
  glp_add_cols(lp, static_cast<int>(streams.size()));
  for (int iColumn = 0; iColumn < static_cast<int>(streams.size()); ++iColumn) {
    glp_set_col_bnds(lp, iColumn + 1, GLP_LO, 0, 0);
    glp_set_obj_coef(lp, iColumn + 1, 1);  // here we could add different weights for streams
  }

  // A
  glp_load_matrix(lp, static_cast<int>(iA.size() - 1), iA.data(), jA.data(), valueA.data());

  // compute result
  int failed [[maybe_unused]] = glp_simplex(lp, nullptr);
  assert(!failed);

  // create and populate result object
  std::vector<double> result;
  result.reserve(streams.size());
  for (int i = 0; static_cast<size_t>(i) < streams.size(); ++i) {
    result.emplace_back(glp_get_col_prim(lp, i + 1));
  }
  assert(result.size() == streams.size());

  // clean up
  glp_delete_prob(lp);
  glp_free_env();
  return result;
}
//----------------------------------------------------------------------------------------------------------------------
std::vector<size_t> StreamSimulator::computeSpeedsFlow(std::span<size_t const> uploadSpeeds,
                                                       std::span<size_t const> downloadSpeeds,
                                                       std::span<DirectStream const> streams) {
  // build the graph
  Graph g(uploadSpeeds.size() * 2 + 2);
  property_map<Graph, edge_capacity_t>::type capacity = get(edge_capacity, g);
  property_map<Graph, edge_residual_capacity_t>::type residual_capacity = get(edge_residual_capacity, g);
  property_map<Graph, edge_reverse_t>::type rev = get(edge_reverse, g);
  size_t s = uploadSpeeds.size() * 2;
  size_t t = s + 1;

  auto add_edge = [&g, &capacity, &rev](size_t from, size_t to, size_t c) {
    auto e = boost::add_edge(from, to, g).first;
    auto re = boost::add_edge(to, from, g).first;
    capacity[e] = c;
    capacity[re] = 0;
    rev[e] = re;
    rev[re] = e;
  };

  // capacity for uploads and downloads
  assert(uploadSpeeds.size() == downloadSpeeds.size());
  for (size_t i = 0; i < uploadSpeeds.size(); ++i) {
    add_edge(s, i, uploadSpeeds[i]);
    add_edge(i + uploadSpeeds.size(), t, downloadSpeeds[i]);
  }

  // sufficient capacity for connections between machines
  for (const auto& stream : streams) {
    add_edge(stream.first, stream.second + uploadSpeeds.size(), uploadSpeeds[stream.first]);
  }

  /*
  if (printGraph) {
     property_map<Graph, vertex_name_t>::type name = get(vertex_name, g);
     name[s] = "s";
     name[t] = "t";
     boost::make_label_writer(capacity);
     boost::write_graphviz(std::cout, g, boost::make_label_writer(name), boost::make_label_writer(capacity));
  }
  */

  boost::boykov_kolmogorov_max_flow(g, s, t);

  // get the result
  std::vector<size_t> result;
  for (const auto& stream : streams) {
    auto e = boost::edge(stream.first, stream.second + uploadSpeeds.size(), g).first;
    result.push_back(capacity[e] - residual_capacity[e]);
  }
  return result;
}
//----------------------------------------------------------------------------------------------------------------------
std::vector<double> StreamSimulator::computeSpeedsGreedy(std::span<const double> uploadSpeeds,
                                                         std::span<const double> downloadSpeeds,
                                                         std::span<const DirectStream> streams) {
  assert(uploadSpeeds.size() == downloadSpeeds.size());
  std::vector<double> result(streams.size(), 0);

  std::vector<size_t> outgoingConnections(uploadSpeeds.size(), 0);
  std::vector<size_t> ingoingConnections(downloadSpeeds.size(), 0);
  for (const DirectStream& stream : streams) {
    outgoingConnections[stream.first] += 1;
    ingoingConnections[stream.second] += 1;
  }

  std::vector<double> usedUploadSpeed(uploadSpeeds.size(), 0);
  std::vector<double> usedDownloadSpeed(downloadSpeeds.size(), 0);
  for (size_t i = 0; i < streams.size(); ++i) {
    const DirectStream& stream = streams[i];
    double connectionSpeed = std::min(uploadSpeeds[stream.first] / outgoingConnections[stream.first],
                                      downloadSpeeds[stream.second] / ingoingConnections[stream.second]);
    usedUploadSpeed[stream.first] += connectionSpeed;
    usedDownloadSpeed[stream.second] += connectionSpeed;
    result[i] = connectionSpeed;
  }

  for (size_t i = 0; i < streams.size(); ++i) {
    const DirectStream& stream = streams[i];
    double improvement = std::min(uploadSpeeds[stream.first] - usedUploadSpeed[stream.first],
                                  downloadSpeeds[stream.second] - usedDownloadSpeed[stream.second]);
    usedUploadSpeed[stream.first] += improvement;
    usedDownloadSpeed[stream.second] += improvement;
    result[i] += improvement;
  }

  return result;
}
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
// NOLINTEND

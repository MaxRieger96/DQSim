#pragma once
//----------------------------------------------------------------------------------------------------------------------
#include <cassert>
#include <string_view>
#include <vector>
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2023 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
/// Remove all elements from elements that are referenced by deleteIndexes
/// DeleteIndexes has to be sorted in ascending order
template <typename T>
void filter(std::vector<T>& elements, const std::vector<size_t>& deleteIndexes) {
  if (deleteIndexes.size() == 0) return;
  size_t iDel = 0;
  size_t iOut = 0;
  for (size_t iIn = 0; iIn < elements.size(); ++iIn) {
    if (iDel >= deleteIndexes.size() || iIn != deleteIndexes[iDel]) {
      elements[iOut++] = elements[iIn];
    } else {
      ++iDel;
    }
  }
  elements.resize(elements.size() - deleteIndexes.size());
}
//----------------------------------------------------------------------------------------------------------------------
template <typename T>
class Matrix {
  /// The contained data
  std::vector<T> data;
  size_t m;
  size_t n;

 public:
  /// Constructor with zero initialization
  Matrix(size_t n, size_t m) : data(m * n, 0), m(m), n(n) {}
  /// Access
  T& operator()(size_t i, size_t j) {
    assert(i < n);
    assert(j < m);
    assert((m * i + j) < n * m);
    assert(data.size() == n * m);
    return data[m * i + j];
  }
  const T& operator()(size_t i, size_t j) const {
    assert(i < n);
    assert(j < m);
    assert((m * i + j) < n * m);
    assert(data.size() == n * m);
    return data[m * i + j];
  }
};
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

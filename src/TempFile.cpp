//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2024 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
#include "src/TempFile.hpp"

#include <cassert>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <istream>
#include <memory>
#include <ostream>
#include <random>
#include <string>
//----------------------------------------------------------------------------------------------------------------------
namespace {
//----------------------------------------------------------------------------------------------------------------------
std::string generateRandomFilename() {
  const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  const size_t length = 16;
  std::string filename;

  static thread_local std::mt19937 generator{std::random_device{}()};
  std::uniform_int_distribution<size_t> dist(0, sizeof(charset) - 2);

  for (size_t i = 0; i < length; ++i) {
    filename += charset[dist(generator)];
  }
  filename += ".tmp";
  return filename;
}
//----------------------------------------------------------------------------------------------------------------------
}  // namespace
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
TempFile::TempFile() : filePath(generateRandomFilename()), stream(filePath.c_str()) {}
//----------------------------------------------------------------------------------------------------------------------
std::ostream& TempFile::getWriteStream() {
  assert(stream.is_open());
  return stream;
}
//----------------------------------------------------------------------------------------------------------------------
std::unique_ptr<std::istream> TempFile::getReadStream() {
  stream.flush();
  return std::make_unique<std::ifstream>(filePath.c_str());
}
//----------------------------------------------------------------------------------------------------------------------
TempFile::~TempFile() { std::filesystem::remove(filePath); }
//----------------------------------------------------------------------------------------------------------------------
std::size_t TempFile::size() const { return std::filesystem::file_size(filePath); }
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

#pragma once
//----------------------------------------------------------------------------------------------------------------------
#include <filesystem>
#include <fstream>
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2024 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
/// A temporary file, that deletes when de-allocated
class TempFile {
  /// The Path of the file
  std::filesystem::path filePath;
  /// The ostream to write to
  std::ofstream stream;

 public:
  /// Constructor
  TempFile();
  /// Destructor
  ~TempFile();

  /// TempFile is not movable or copyable
  TempFile(TempFile&&) = delete;
  TempFile(const TempFile&) = delete;
  TempFile& operator=(TempFile&&) = delete;
  TempFile& operator=(const TempFile&) = delete;

  /// Get a stream to write to
  std::ostream& getWriteStream();
  /// Get a stream to read from
  std::unique_ptr<std::istream> getReadStream();
  /// Get the file size
  std::size_t size() const;
};
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------

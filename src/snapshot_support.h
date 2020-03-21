#ifndef SRC_SNAPSHOT_SUPPORT_H_
#define SRC_SNAPSHOT_SUPPORT_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "v8.h"
#include <map>
#include <unordered_map>

namespace node {

class SnapshotData final {
 public:
  static constexpr size_t kEmptyIndex = static_cast<size_t>(-1);

  void StartWriteEntry(const char* name);
  void EndWriteEntry();

  void WriteBool(bool value);
  void WriteInt32(int32_t value);
  void WriteInt64(int64_t value);
  void WriteUint32(uint32_t value);
  void WriteUint64(uint64_t value);
  void WriteIndex(size_t value);
  void WriteString(const char* str, size_t length = static_cast<size_t>(-1));
  void WriteString(const std::string& str);

  v8::Maybe<std::string> StartReadEntry(const char* expected_name);
  v8::Maybe<bool> EndReadEntry();

  v8::Maybe<bool> ReadBool();
  v8::Maybe<int32_t> ReadInt32();
  v8::Maybe<int64_t> ReadInt64();
  v8::Maybe<uint32_t> ReadUint32();
  v8::Maybe<uint64_t> ReadUint64();
  v8::Maybe<size_t> ReadIndex();
  v8::Maybe<std::string> ReadString();

  void add_error(const std::string& error);
  inline const std::vector<std::string>& errors() const;
  inline std::vector<uint8_t> release_storage();

  explicit inline SnapshotData(std::vector<uint8_t>&& storage);
  SnapshotData() = default;

 private:
  void WriteTag(uint8_t tag);
  bool ReadTag(uint8_t tag);
  void WriteRawData(const uint8_t* data, size_t length);
  bool ReadRawData(uint8_t* data, size_t length);

  void EnsureSpace(size_t addition);
  bool HasSpace(size_t addition) const;

  std::vector<uint8_t> storage_;
  size_t current_index_ = 0;
  std::vector<std::string> errors_;
  std::vector<std::string> entry_stack_;
};

class Snapshottable {
 public:
  virtual ~Snapshottable() = 0;
  virtual void Serialize(v8::SnapshotCreator* creator,
                         SnapshotData* snapshot_data) const;
};

class ExternalReferences {
 public:
  template <typename... Args>
  inline ExternalReferences(const char* id, Args*... args);

  void AddPointer(intptr_t ptr);

  // Returns the list of all references collected so far, not yet terminated
  // by kEnd.
  static std::vector<intptr_t> get_list();

  static constexpr intptr_t kEnd = reinterpret_cast<intptr_t>(nullptr);

 private:
  void Register(const char* id, ExternalReferences* self);
  static std::map<std::string, ExternalReferences*>* map();
  std::vector<intptr_t> references_;

  inline void HandleArgs();
  template <typename T, typename... Args>
  inline void HandleArgs(T* ptr, Args*... args);
};

}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_SNAPSHOT_SUPPORT_H_

#include "snapshot_support-inl.h"
#include "debug_utils-inl.h"
#include "util.h"

using v8::Just;
using v8::Maybe;
using v8::Nothing;
using v8::SnapshotCreator;

namespace node {

Snapshottable::~Snapshottable() {}

void Snapshottable::Serialize(
    SnapshotCreator* creator,
    SnapshotData* snapshot_data) const {
  snapshot_data->add_error("Unserializable object encountered");
}

enum Tag {
  kEntryStart,
  kEntryEnd,
  kBool,
  kInt32,
  kInt64,
  kUint32,
  kUint64,
  kIndex,
  kString,
};

void SnapshotData::EnsureSpace(size_t addition) {
  if (LIKELY(HasSpace(addition))) return;  // Enough space.
  addition = std::max<size_t>(addition, 4096);
  storage_.resize(storage_.size() + addition);
}

bool SnapshotData::HasSpace(size_t addition) const {
  return storage_.size() - current_index_ >= addition;
}

void SnapshotData::WriteRawData(const uint8_t* data, size_t length) {
  EnsureSpace(length);
  memcpy(storage_.data() + current_index_, data, length);
  current_index_ += length;
}

bool SnapshotData::ReadRawData(uint8_t* data, size_t length) {
  if (UNLIKELY(!HasSpace(length))) {
    add_error("Unexpected end of input");
    return false;
  }
  memcpy(data, storage_.data() + current_index_, length);
  current_index_ += length;
  return true;
}

void SnapshotData::WriteTag(uint8_t tag) {
  WriteRawData(&tag, 1);
}

bool SnapshotData::ReadTag(uint8_t expected) {
  uint8_t actual;
  if (!ReadRawData(&actual, 1)) return false;
  if (actual != expected) {
    add_error(SPrintF("Unexpected tag %d (expected %d)", actual, expected));
    return false;
  }
  return true;
}

void SnapshotData::StartWriteEntry(const char* name) {
  WriteTag(kEntryStart);
  WriteString(name);
  entry_stack_.push_back(name);
}

void SnapshotData::EndWriteEntry() {
  entry_stack_.pop_back();
  WriteTag(kEntryEnd);
}

void SnapshotData::WriteBool(bool value) {
  WriteTag(kBool);
  uint8_t data = value ? 1 : 0;
  WriteRawData(&data, 1);
}

void SnapshotData::WriteInt32(int32_t value) {
  WriteTag(kInt32);
  WriteRawData(reinterpret_cast<const uint8_t*>(&value), sizeof(value));
}

void SnapshotData::WriteInt64(int64_t value) {
  WriteTag(kInt64);
  WriteRawData(reinterpret_cast<const uint8_t*>(&value), sizeof(value));
}

void SnapshotData::WriteUint32(uint32_t value) {
  WriteTag(kUint32);
  WriteRawData(reinterpret_cast<const uint8_t*>(&value), sizeof(value));
}

void SnapshotData::WriteUint64(uint64_t value) {
  WriteTag(kUint64);
  WriteRawData(reinterpret_cast<const uint8_t*>(&value), sizeof(value));
}

void SnapshotData::WriteIndex(size_t value) {
  WriteTag(kIndex);
  WriteRawData(reinterpret_cast<const uint8_t*>(&value), sizeof(value));
}

void SnapshotData::WriteString(const char* str, size_t length) {
  WriteTag(kString);
  if (length == static_cast<size_t>(-1)) length = strlen(str);
  WriteUint64(length);
  WriteRawData(reinterpret_cast<const uint8_t*>(str), length);
}

void SnapshotData::WriteString(const std::string& str) {
  WriteString(str.c_str(), str.size());
}

v8::Maybe<std::string> SnapshotData::StartReadEntry(const char* expected) {
  if (!ReadTag(kEntryStart)) return Nothing<std::string>();
  std::string actual;
  if (!ReadString().To(&actual)) return Nothing<std::string>();
  if (expected != nullptr && actual != expected) {
    add_error(SPrintF("Unexpected entry %s (expected %s)", actual, expected));
    return Nothing<std::string>();
  }
  entry_stack_.push_back(actual);
  return Just(std::move(actual));
}

v8::Maybe<bool> SnapshotData::EndReadEntry() {
  if (!ReadTag(kEntryEnd)) return Nothing<bool>();
  entry_stack_.pop_back();
  return Just(true);
}

v8::Maybe<bool> SnapshotData::ReadBool() {
  if (!ReadTag(kBool)) return Nothing<bool>();
  uint8_t value;
  if (!ReadRawData(&value, 1)) return Nothing<bool>();
  return Just(static_cast<bool>(value));
}

v8::Maybe<int32_t> SnapshotData::ReadInt32() {
  if (!ReadTag(kInt32)) return Nothing<int32_t>();
  int32_t value;
  if (!ReadRawData(reinterpret_cast<uint8_t*>(&value), sizeof(value)))
    return Nothing<int32_t>();
  return Just(value);
}

v8::Maybe<int64_t> SnapshotData::ReadInt64() {
  if (!ReadTag(kInt64)) return Nothing<int64_t>();
  int64_t value;
  if (!ReadRawData(reinterpret_cast<uint8_t*>(&value), sizeof(value)))
    return Nothing<int64_t>();
  return Just(value);
}

v8::Maybe<uint32_t> SnapshotData::ReadUint32() {
  if (!ReadTag(kUint32)) return Nothing<uint32_t>();
  uint32_t value;
  if (!ReadRawData(reinterpret_cast<uint8_t*>(&value), sizeof(value)))
    return Nothing<uint32_t>();
  return Just(static_cast<uint32_t>(value));
}

v8::Maybe<uint64_t> SnapshotData::ReadUint64() {
  if (!ReadTag(kUint64)) return Nothing<uint64_t>();
  uint64_t value;
  if (!ReadRawData(reinterpret_cast<uint8_t*>(&value), sizeof(value)))
    return Nothing<uint64_t>();
  return Just(value);
}

v8::Maybe<size_t> SnapshotData::ReadIndex() {
  if (!ReadTag(kIndex)) return Nothing<size_t>();
  size_t value;
  if (!ReadRawData(reinterpret_cast<uint8_t*>(&value), sizeof(value)))
    return Nothing<size_t>();
  return Just(value);
}

v8::Maybe<std::string> SnapshotData::ReadString() {
  if (!ReadTag(kString)) return Nothing<std::string>();
  uint64_t size;
  if (!ReadUint64().To(&size)) return Nothing<std::string>();
  std::string str(size, '\0');
  if (!ReadRawData(reinterpret_cast<uint8_t*>(&str[0]), size))
    return Nothing<std::string>();
  return Just(std::move(str));
}

void SnapshotData::add_error(const std::string& error) {
  std::string location = "At ";
  for (const std::string& entry : entry_stack_) {
    location += entry;
    location += ':';
  }
  errors_.push_back(location + " " + error);
}

void ExternalReferences::AddPointer(intptr_t ptr) {
  DCHECK_NE(ptr, kEnd);
  references_.push_back(ptr);
}

std::map<std::string, ExternalReferences*>* ExternalReferences::map() {
  static std::map<std::string, ExternalReferences*> map_;
  return &map_;
}

std::vector<intptr_t> ExternalReferences::get_list() {
  static std::vector<intptr_t> list;
  if (list.empty()) {
    for (const auto& entry : *map()) {
      std::vector<intptr_t>* source = &entry.second->references_;
      list.insert(list.end(), source->begin(), source->end());
      source->clear();
      source->shrink_to_fit();
    }
  }
  return list;
}

void ExternalReferences::Register(const char* id, ExternalReferences* self) {
  auto result = map()->insert({id, this});
  DCHECK(result.second);
}

constexpr intptr_t ExternalReferences::kEnd;

}  // namespace node

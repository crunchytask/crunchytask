#include "taskqueue/task_id.h"

#include <array>
#include <cstdio>
#include <random>

namespace tq {

namespace {

bool IsHexDigit(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
         (c >= 'A' && c <= 'F');
}

bool IsValidUuidFormat(std::string_view value) {
  if (value.size() != 36) {
    return false;
  }

  for (std::size_t i = 0; i < value.size(); ++i) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (value[i] != '-') {
        return false;
      }
      continue;
    }
    if (!IsHexDigit(value[i])) {
      return false;
    }
  }

  return true;
}

}  // namespace

TaskId::TaskId() = default;

TaskId::TaskId(std::string value) : value_(std::move(value)) {}

TaskId TaskId::Generate() {
  std::array<unsigned char, 16> bytes{};
  std::random_device random_device;
  std::mt19937 generator(random_device());
  std::uniform_int_distribution<int> distribution(0, 255);

  for (auto& byte : bytes) {
    byte = static_cast<unsigned char>(distribution(generator));
  }

  bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0F) | 0x40);
  bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3F) | 0x80);

  char buffer[37];
  std::snprintf(
      buffer, sizeof(buffer),
      "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
      bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6],
      bytes[7], bytes[8], bytes[9], bytes[10], bytes[11], bytes[12], bytes[13],
      bytes[14], bytes[15]);

  return TaskId(std::string(buffer));
}

ParseResult<TaskId> TaskId::Parse(std::string_view value) {
  if (value.empty()) {
    return ParseResult<TaskId>::Fail("task id must not be empty");
  }
  if (!IsValidUuidFormat(value)) {
    return ParseResult<TaskId>::Fail("task id must be a UUID string");
  }
  return ParseResult<TaskId>::Ok(TaskId(std::string(value)));
}

}  // namespace tq

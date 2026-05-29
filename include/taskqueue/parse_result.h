#ifndef TASKQUEUE_PARSE_RESULT_H_
#define TASKQUEUE_PARSE_RESULT_H_

#include <string>
#include <utility>

namespace tq {

template <typename T>
class ParseResult {
 public:
  static ParseResult Ok(T value) {
    return ParseResult(std::move(value), std::string{});
  }

  static ParseResult Fail(std::string message) {
    return ParseResult(T{}, std::move(message));
  }

  bool Ok() const { return error_.empty(); }

  const T& Value() const { return value_; }
  T& Value() { return value_; }

  const std::string& Error() const { return error_; }

 private:
  ParseResult(T value, std::string error)
      : value_(std::move(value)), error_(std::move(error)) {}

  T value_;
  std::string error_;
};

}  // namespace tq

#endif  // TASKQUEUE_PARSE_RESULT_H_

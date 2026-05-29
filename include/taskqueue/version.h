#ifndef TASKQUEUE_VERSION_H_
#define TASKQUEUE_VERSION_H_

#include <string_view>

namespace tq {

inline constexpr std::string_view kVersionString = "0.1.0";

const char* Version();

}  // namespace tq

#endif  // TASKQUEUE_VERSION_H_

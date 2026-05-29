#ifndef TASKQUEUE_CLOCK_H_
#define TASKQUEUE_CLOCK_H_

#include <cstdint>

namespace tq {

std::int64_t NowUnixMs();

}  // namespace tq

#endif  // TASKQUEUE_CLOCK_H_

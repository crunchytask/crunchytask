#ifndef TASKQUEUE_SCHEDULER_H_
#define TASKQUEUE_SCHEDULER_H_

#include "taskqueue/broker.h"

namespace tq {

void RunSchedulerTick(Broker& broker, std::int64_t visibility_timeout_ms);

}  // namespace tq

#endif  // TASKQUEUE_SCHEDULER_H_

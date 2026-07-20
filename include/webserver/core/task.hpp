#pragma once

// Re-export coro::Task as webserver::Task
#include "coro/core/task.hpp"

namespace webserver {

using coro::Task;
using coro::make_task;

} // namespace webserver

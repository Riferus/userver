#pragma once

#include <engine/coro/pool.hpp>
#include <engine/ev/thread_pool.hpp>
#include <engine/task/task_context.hpp>

USERVER_NAMESPACE_BEGIN

namespace engine::impl {

class TaskProcessorPools final {
 public:
  using CoroPool = coro::Pool<TaskContext>;

  TaskProcessorPools(coro::PoolConfig coro_pool_config,
                     ev::ThreadPoolConfig ev_pool_config)
      // NOLINTNEXTLINE(hicpp-move-const-arg,performance-move-const-arg,clang-analyzer-core.uninitialized.UndefReturn)
      : coro_pool_(std::move(coro_pool_config), &TaskContext::CoroFunc),
        event_thread_pool_(std::move(ev_pool_config),
                           ev::ThreadPool::kUseDefaultEvLoop) {}

  CoroPool& GetCoroPool() { return coro_pool_; }
  ev::ThreadPool& EventThreadPool() { return event_thread_pool_; }

 private:
  CoroPool coro_pool_;
  ev::ThreadPool event_thread_pool_;
};

}  // namespace engine::impl

USERVER_NAMESPACE_END

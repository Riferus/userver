#include "cluster_shard.hpp"

#include <limits>
#include <memory>
#include <optional>
#include <vector>

#include <userver/utils/assert.hpp>

USERVER_NAMESPACE_BEGIN

namespace redis {

namespace {

bool IsNearestServerPing(const CommandControl& control) {
  switch (control.strategy) {
    case CommandControl::Strategy::kEveryDc:
    case CommandControl::Strategy::kDefault:
      return false;
    case CommandControl::Strategy::kLocalDcConductor:
    case CommandControl::Strategy::kNearestServerPing:
      return true;
  }
  /* never reachable */
  UASSERT(false);
  return false;
}

}  // namespace

ClusterShard& ClusterShard::operator=(const ClusterShard& other) {
  if (&other == this) {
    return *this;
  }
  replicas_ = other.replicas_;
  master_ = other.master_;
  current_ = other.current_.load();
  shard_ = other.shard_;
  return *this;
}

ClusterShard& ClusterShard::operator=(ClusterShard&& other) noexcept {
  if (&other == this) {
    return *this;
  }
  replicas_ = std::move(other.replicas_);
  master_ = std::move(other.master_);
  current_ = other.current_.load();
  shard_ = other.shard_;
  return *this;
}

bool ClusterShard::IsReady(WaitConnectedMode mode) const {
  switch (mode) {
    case WaitConnectedMode::kNoWait:
      return true;
    case WaitConnectedMode::kMaster:
      return IsMasterReady();
    case WaitConnectedMode::kMasterOrSlave:
      return IsMasterReady() || IsReplicaReady();
    case WaitConnectedMode::kSlave:
      return IsReplicaReady();
    case WaitConnectedMode::kMasterAndSlave:
      return IsMasterReady() && IsReplicaReady();
  }

  UINVARIANT(false, "Unexpected WaitConnectedMode value");
}

bool ClusterShard::AsyncCommand(CommandPtr command) const {
  const auto& command_control = command->control;
  const auto read_only = command->read_only;

  if (!read_only || !command_control.force_server_id.IsAny()) {
    if (const auto& instance = GetAvailableServer(command_control, read_only);
        instance) {
      return instance->AsyncCommand(command);
    }
    return false;
  }

  const auto current = current_++;
  const auto& available_servers = GetAvailableServers(command_control);
  const auto servers_count = available_servers.size();
  const auto is_nearest_ping_server = IsNearestServerPing(command_control);

  const auto masters_count = 1;
  const auto max_attempts = replicas_.size() + masters_count + 1;
  for (size_t attempt = 0; attempt < max_attempts; attempt++) {
    const size_t start_idx =
        GetStartIndex(command_control, attempt, is_nearest_ping_server,
                      command->instance_idx, current, servers_count);

    size_t idx = SentinelImpl::kDefaultPrevInstanceIdx;
    const auto instance = GetInstance(available_servers, start_idx, attempt,
                                      is_nearest_ping_server,
                                      command_control.best_dc_count, &idx);
    if (!instance) {
      continue;
    }
    command->instance_idx = idx;
    if (instance->AsyncCommand(command)) return true;
  }

  LOG_LIMITED_WARNING() << "No Redis server is ready for shard=" << shard_
                        << " slave=" << command->read_only
                        << " available_servers=" << available_servers.size()
                        << command->GetLogExtra();
  return false;
}

ShardStatistics ClusterShard::GetStatistics(
    bool master, const MetricsSettings& settings) const {
  ShardStatistics stats(settings);
  auto add_to_stats = [&settings, &stats](const auto& instance) {
    if (!instance) {
      return;
    }
    auto inst_stats =
        redis::InstanceStatistics(settings, instance->GetStatistics());
    stats.shard_total.Add(inst_stats);
    auto master_host_port = instance->GetServerHost() + ":" +
                            std::to_string(instance->GetServerPort());
    stats.instances.emplace(std::move(master_host_port), std::move(inst_stats));
  };

  if (master) {
    if (master_) {
      add_to_stats(master_->Get());
    }
  } else {
    for (const auto& instance : replicas_) {
      if (instance) {
        add_to_stats(instance->Get());
      }
    }
  }

  stats.is_ready = IsReady(WaitConnectedMode::kMasterAndSlave);
  return stats;
}

/// Prioritize first command_control.best_dc_count nearest by ping instances.
/// Leave others to be able to fallback
void ClusterShard::GetNearestServersPing(
    const CommandControl& command_control,
    std::vector<RedisConnectionPtr>& instances) {
  const auto num_instances =
      std::min(command_control.best_dc_count, instances.size());
  if (num_instances == 0 && num_instances == instances.size()) {
    /// We want to leave all instances
    return;
  }
  std::partial_sort(
      instances.begin(), instances.begin() + num_instances, instances.end(),
      [](const RedisConnectionPtr& l, const RedisConnectionPtr& r) {
        UASSERT(r->Get() && l->Get());
        return l->Get()->GetPingLatency() < r->Get()->GetPingLatency();
      });
}

ClusterShard::RedisPtr ClusterShard::GetAvailableServer(
    const CommandControl& command_control, bool read_only) const {
  if (!read_only) {
    return master_->Get();
  }

  if (command_control.force_server_id.IsAny()) {
    return {};
  }

  const auto& id = command_control.force_server_id;
  auto master = master_->Get();
  if (master->GetServerId() == id) {
    return master;
  }

  for (const auto& replica_connection : replicas_) {
    auto replica = replica_connection->Get();
    if (replica->GetServerId() == id) {
      return replica;
    }
  }
  const logging::LogExtra log_extra({{"server_id", id.GetId()}});
  LOG_LIMITED_WARNING() << "server_id not found in Redis shard (dead server?)"
                        << log_extra;
  return {};
}

std::vector<ClusterShard::RedisConnectionPtr> ClusterShard::GetAvailableServers(
    const CommandControl& command_control) const {
  if (!IsNearestServerPing(command_control)) {
    /// allow_reads_from_master does not matter here.
    /// We just choose right index in GetStartIndex to avoid choosing master
    /// in first try of read_only requests
    return MakeReadonlyWithMasters();
  }

  if (command_control.allow_reads_from_master) {
    auto ret = MakeReadonlyWithMasters();
    ClusterShard::GetNearestServersPing(command_control, ret);
    return ret;
  }

  auto ret = replicas_;
  ClusterShard::GetNearestServersPing(command_control, ret);
  ret.push_back(master_);
  return ret;
}

ClusterShard::RedisPtr ClusterShard::GetInstance(
    const std::vector<RedisConnectionPtr>& instances, size_t start_idx,
    size_t attempt, bool is_nearest_ping_server, size_t best_dc_count,
    size_t* pinstance_idx) {
  RedisPtr ret;
  const auto end = (is_nearest_ping_server && attempt == 0 && best_dc_count)
                       ? std::min(instances.size(), best_dc_count)
                       : instances.size();
  for (size_t i = 0; i < end; ++i) {
    const auto idx = (start_idx + i) % end;
    const auto& cur_inst = instances[idx]->Get();

    if (cur_inst && !cur_inst->IsDestroying() &&
        (cur_inst->GetState() == Redis::State::kConnected) &&
        !cur_inst->IsSyncing() &&
        (!ret || ret->IsDestroying() ||
         cur_inst->GetRunningCommands() < ret->GetRunningCommands())) {
      if (pinstance_idx) *pinstance_idx = idx;
      ret = cur_inst;
    }
  }
  return ret;
}

bool ClusterShard::IsMasterReady() const {
  return master_ && master_->GetState() == Redis::State::kConnected;
}

bool ClusterShard::IsReplicaReady() const {
  return std::any_of(
      replicas_.begin(), replicas_.end(), [](const auto& replica) {
        return replica && replica->GetState() == Redis::State::kConnected;
      });
}

size_t GetStartIndex(const CommandControl& command_control, size_t attempt,
                     bool is_nearest_ping_server, size_t prev_instance_idx,
                     size_t current, size_t servers_count) {
  const bool allow_reads_from_master = command_control.allow_reads_from_master;
  const size_t best_dc_count = !command_control.best_dc_count
                                   ? std::numeric_limits<size_t>::max()
                                   : command_control.best_dc_count;
  const bool first_attempt = attempt == 0;
  const bool first_try =
      prev_instance_idx == SentinelImplBase::kDefaultPrevInstanceIdx;
  /// For compatibility with non-cluster-autotopology driver:
  /// List of available servers for readonly
  /// requests still contains master. Master is the last server in list.
  /// Reads from master are posible even with allow_reads_from_master=false
  /// in cases when there no available replica (replicas are broken or
  /// master is the only instance in cluster shard).
  servers_count = (first_try && first_attempt && !allow_reads_from_master)
                      ? std::max<size_t>(servers_count - 1, 1)
                      : servers_count;
  if (is_nearest_ping_server) {
    /// start index for nearest replicas:
    /// for first try and attempt - just first instance (nearest).
    /// then try other
    return (attempt + (first_try ? (current % std::min<size_t>(best_dc_count,
                                                               servers_count))
                                 : (prev_instance_idx + 1))) %
           servers_count;
  }

  if (first_try) {
    return (current + attempt) % servers_count;
  }

  return (prev_instance_idx + 1 + attempt) % servers_count;
}

std::vector<ClusterShard::RedisConnectionPtr>
ClusterShard::MakeReadonlyWithMasters() const {
  std::vector<RedisConnectionPtr> ret;
  ret.reserve(replicas_.size() + 1);
  ret.insert(ret.end(), replicas_.begin(), replicas_.end());
  ret.push_back(master_);
  return ret;
}

}  // namespace redis

USERVER_NAMESPACE_END
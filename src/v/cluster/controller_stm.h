/*
 * Copyright 2020 Redpanda Data, Inc.
 *
 * Use of this software is governed by the Business Source License
 * included in the file licenses/BSL.md
 *
 * As of the Change Date specified in that file, in accordance with
 * the Business Source License, use of this software will be governed
 * by the Apache License, Version 2.0
 */

#pragma once

#include "cluster/bootstrap_backend.h"
#include "cluster/config_manager.h"
#include "cluster/controller_log_limiter.h"
#include "cluster/feature_backend.h"
#include "cluster/security_manager.h"
#include "cluster/topic_updates_dispatcher.h"
#include "raft/mux_state_machine.h"

#include <utility>

namespace cluster {

class members_manager;

// single instance
class controller_stm final
  : public raft::mux_state_machine<
      topic_updates_dispatcher,
      security_manager,
      members_manager,
      config_manager,
      feature_backend,
      bootstrap_backend> {
public:
    template<typename... Args>
    controller_stm(
      limiter_configuration limiter_conf,
      const ss::sharded<features::feature_table>& feature_table,
      config::binding<std::chrono::seconds>&& snapshot_max_age,
      Args&&... stm_args)
      : mux_state_machine(std::forward<Args>(stm_args)...)
      , _limiter(std::move(limiter_conf))
      , _feature_table(feature_table.local())
      , _snapshot_max_age(std::move(snapshot_max_age))
      , _snapshot_debounce_timer([this] { snapshot_timer_callback(); }) {}

    controller_stm(controller_stm&&) = delete;
    controller_stm(const controller_stm&) = delete;
    controller_stm& operator=(controller_stm&&) = delete;
    controller_stm& operator=(const controller_stm&) = delete;
    ~controller_stm() = default;

    template<typename Cmd>
    requires ControllerCommand<Cmd>
    bool throttle() { return _limiter.throttle<Cmd>(); }

    virtual ss::future<> stop() final;

private:
    ss::future<> on_batch_applied() final;
    void snapshot_timer_callback();

    ss::future<std::optional<iobuf>>
    maybe_make_snapshot(ssx::semaphore_units apply_mtx_holder) final;
    ss::future<> apply_snapshot(model::offset, storage::snapshot_reader&) final;

private:
    controller_log_limiter _limiter;
    const features::feature_table& _feature_table;
    config::binding<std::chrono::seconds> _snapshot_max_age;

    ss::timer<ss::lowres_clock> _snapshot_debounce_timer;
};

static constexpr ss::shard_id controller_stm_shard = 0;

} // namespace cluster

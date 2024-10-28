/*
 *
 * Copyright 2021-2024 Software Radio Systems Limited
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the distribution.
 *
 */

#pragma once

#include "apps/services/metrics/metrics_config.h"
#include "cu_up_wrapper.h"
#include "srsran/cu_up/cu_up.h"
#include "srsran/support/timers.h"
#include <memory>

namespace srsran {
namespace app_services {
class metrics_notifier;
}

namespace srs_cu_up {
class e1_connection_client;
}

struct cu_up_unit_config;
class dlt_pcap;
class f1u_cu_up_gateway;
class io_broker;
struct worker_manager;
class e2_connection_client;

/// CU-UP unit dependencies.
struct cu_up_unit_dependencies {
  worker_manager*                  workers;
  task_executor*                   cu_up_e2_exec    = nullptr;
  e2_connection_client*            e2_gw            = nullptr;
  app_services::metrics_notifier*  metrics_notifier = nullptr;
  srs_cu_up::e1_connection_client* e1ap_conn_client = nullptr;
  f1u_cu_up_gateway*               f1u_gateway      = nullptr;
  dlt_pcap*                        gtpu_pcap        = nullptr;
  timer_manager*                   timers           = nullptr;
  io_broker*                       io_brk           = nullptr;
};

/// Wraps the CU-CP and its supported application commands.
struct cu_up_unit {
  std::unique_ptr<cu_up_wrapper>            unit    = nullptr;
  std::vector<app_services::metrics_config> metrics = {};
};

/// Builds the CU UP using the given arguments.
cu_up_unit build_cu_up(const cu_up_unit_config& unit_cfg, const cu_up_unit_dependencies& dependencies);

} // namespace srsran

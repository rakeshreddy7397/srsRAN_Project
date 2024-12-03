/*
 *
 * Copyright 2021-2024 Software Radio Systems Limited
 *
 * This file is part of srsRAN.
 *
 * srsRAN is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * srsRAN is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * A copy of the GNU Affero General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */

#include "o_cu_up_e2_config_cli11_schema.h"
#include "apps/services/e2/e2_cli11_schema.h"
#include "o_cu_up_e2_config.h"
#include "srsran/support/cli11_utils.h"

using namespace srsran;

static void configure_cli11_pcap_args(CLI::App& app, o_cu_up_e2_pcap_config& pcap_params)
{
  add_option(app, "--e2ap_cu_up_filename", pcap_params.filename, "E2AP PCAP file output path")->capture_default_str();
  add_option(app, "--e2ap_enable", pcap_params.enabled, "Enable E2AP packet capture")->always_capture_default();
}

void srsran::configure_cli11_with_o_cu_up_e2_config_schema(CLI::App& app, o_cu_up_e2_config& unit_cfg)
{
  // E2 section.
  configure_cli11_with_e2_config_schema(app, unit_cfg.base_config, "--enable_cu_up_e2", "Enable CU-UP E2 agent");

  // PCAP section.
  CLI::App* pcap_subcmd = add_subcommand(app, "pcap", "Logging configuration")->configurable();
  configure_cli11_pcap_args(*pcap_subcmd, unit_cfg.pcaps);
}
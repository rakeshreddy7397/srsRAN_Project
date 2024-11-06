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

#pragma once

#include "srsran/f1u/cu_up/f1u_gateway.h"
#include "srsran/gtpu/gtpu_demux.h"
#include "srsran/gtpu/ngu_gateway.h"
#include "srsran/pcap/dlt_pcap.h"
#include <cstdint>

namespace srsran::srs_cu_up {

struct f1u_cu_up_split_gateway_creation_msg {
  ngu_gateway& udp_gw;
  gtpu_demux&  demux;
  dlt_pcap&    gtpu_pcap;
  uint16_t     peer_port;
  std::string  f1u_ext_addr = "auto";
};

std::unique_ptr<srsran::f1u_cu_up_udp_gateway> create_split_f1u_gw(f1u_cu_up_split_gateway_creation_msg msg);

} // namespace srsran::srs_cu_up

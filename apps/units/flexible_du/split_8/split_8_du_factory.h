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

#include "apps/units/flexible_du/split_helpers/flexible_du_factory.h"
#include "split_8_du_unit_config.h"

namespace srsran {

class split8_du_factory : public flexible_du_factory
{
  const split_8_du_unit_config& unit_config;

public:
  explicit split8_du_factory(const split_8_du_unit_config& config_) :
    flexible_du_factory({config_.odu_high_cfg, config_.du_low_cfg}), unit_config(config_)
  {
  }

private:
  std::unique_ptr<radio_unit> create_radio_unit(const flexible_du_ru_config&       ru_config,
                                                const flexible_du_ru_dependencies& ru_dependencies) override;
};

} // namespace srsran

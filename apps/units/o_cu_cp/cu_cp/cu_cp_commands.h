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

#include "apps/services/application_command.h"
#include "apps/services/stdin_command_dispatcher_utils.h"
#include "srsran/adt/expected.h"
#include "srsran/cu_cp/cu_cp_command_handler.h"
#include "srsran/ran/pci.h"
#include "srsran/ran/rnti.h"

namespace srsran {

/// Application command to trigger a handover.
class handover_app_command : public app_services::application_command
{
  srs_cu_cp::cu_cp_command_handler& cu_cp;

public:
  explicit handover_app_command(srs_cu_cp::cu_cp_command_handler& cu_cp_) : cu_cp(cu_cp_) {}

  // See interface for documentation.
  std::string_view get_name() const override { return "ho"; }

  // See interface for documentation.
  std::string_view get_description() const override { return " <serving pci> <rnti> <target pci>: force UE handover"; }
  
  std::vector<std::string> split_on_ho(span<const std::string> args)
  {
    std::vector<std::string> modifiedArgs;

    for (const auto& arg : args) {
        std::string modifiedArg = arg;

        // Find the position of "ho"
        std::size_t pos = modifiedArg.find("ho");
        if (pos != std::string::npos) {
            // If "ho" is found, split the string
            std::string beforeHo = modifiedArg.substr(0, pos);
            std::string afterHo = modifiedArg.substr(pos); // This includes "ho"

            // Trim any extra spaces before and after (optional)
            beforeHo.erase(std::remove_if(beforeHo.begin(), beforeHo.end(), ::isspace), beforeHo.end());

            // Add both parts as separate arguments
            modifiedArgs.push_back(beforeHo);
            modifiedArgs.push_back(afterHo);
        } else {
            // If "ho" is not found, add the original argument
            modifiedArgs.push_back(modifiedArg);
        }
    }

    return modifiedArgs;
  }
  
  void execute(span<const std::string> args) override
  {
    srslog::basic_logger& logger = srslog::fetch_basic_logger("E2");
    logger.info("Received HO triggered at CU with:\t{}", args);
    //fmt::print("Got args: {}\n", args);
    if (args.size() < 3) {
        fmt::print("Invalid handover command structure. Usage: ho <serving pci> <rnti> <target pci>\n");
        return;
    }

    if (args.size() != 3) {
      std::vector<std::string> splitStrings = split_on_ho(args);
      span<const std::string> modifiedargs(splitStrings.data(), splitStrings.size());

      // Iterator to track the start of each command segment
      auto start = modifiedargs.begin();
      
      while (start < modifiedargs.end()) {
          // Check if there are enough args left in the segment for a handover command
          if (std::distance(start, modifiedargs.end()) < 3) {
              fmt::print("Incomplete handover command detected.\n");
              break;
          }

          // Parse serving_pci
          expected<unsigned, std::string> serving_pci = app_services::parse_int<unsigned>(*start);
          if (!serving_pci.has_value()) {
              fmt::print("Invalid serving PCI.\n");
              return;
          }
          start++;

          // Parse rnti
          expected<unsigned, std::string> rnti = app_services::parse_unsigned_hex<unsigned>(*start);
          if (!rnti.has_value()) {
              fmt::print("Invalid UE RNTI.\n");
              return;
          }
          start++;

          // Parse target_pci
          expected<unsigned, std::string> target_pci = app_services::parse_int<unsigned>(*start);
          if (!target_pci.has_value()) {
              fmt::print("Invalid target PCI.\n");
              return;
          }
          start++;

          // Trigger the handover
          cu_cp.get_mobility_command_handler().trigger_handover(static_cast<pci_t>(serving_pci.value()),
                                                                static_cast<rnti_t>(rnti.value()),
                                                                static_cast<pci_t>(target_pci.value()));
          fmt::print("Handover triggered for UE with pci={} rnti={:#04x} to pci={}.\n",
                    serving_pci.value(),
                    rnti.value(),
                    target_pci.value());

          // Look for "ho" delimiter before continuing to the next segment
          if (start != modifiedargs.end() && *start == "ho") {
              start++;  // Skip over "ho" to start the next segment
          } else {
              break;  // Exit if no more "ho" or arguments are left
          }
      }
    }
    else {
      auto                            arg         = args.begin();
      expected<unsigned, std::string> serving_pci = app_services::parse_int<unsigned>(*arg);
      if (not serving_pci.has_value()) {
        fmt::print("Invalid serving PCI.\n");
        return;
      }
      arg++;
      expected<unsigned, std::string> rnti = app_services::parse_unsigned_hex<unsigned>(*arg);
      if (not rnti.has_value()) {
        fmt::print("Invalid UE RNTI.\n");
        return;
      }
      arg++;
      expected<unsigned, std::string> target_pci = app_services::parse_int<unsigned>(*arg);
      if (not target_pci.has_value()) {
        fmt::print("Invalid target PCI.\n");
        return;
      }

      cu_cp.get_mobility_command_handler().trigger_handover(static_cast<pci_t>(serving_pci.value()),
                                                            static_cast<rnti_t>(rnti.value()),
                                                            static_cast<pci_t>(target_pci.value()));
      fmt::print("Handover triggered for UE with pci={} rnti={:#04x} to pci={}.\n",
                serving_pci.value(),
                rnti.value(),
                target_pci.value());
    }
  }
};

} // namespace srsran

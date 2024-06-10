/*
 *
 * Copyright 2021-2024 Software Radio Systems Limited
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the distribution.
 *
 */

#include "ue_manager_impl.h"
#include "srsran/cu_cp/security_manager_config.h"

using namespace srsran;
using namespace srs_cu_cp;

void cu_cp_ue::stop()
{
  task_sched.stop();
}

ue_manager::ue_manager(const ue_configuration&        ue_config_,
                       const up_resource_manager_cfg& up_config_,
                       const security_manager_config& sec_config_,
                       timer_manager&                 timers,
                       task_executor&                 cu_cp_exec) :
  ue_config(ue_config_), up_config(up_config_), sec_config(sec_config_), ue_task_scheds(timers, cu_cp_exec, logger)
{
}

void ue_manager::stop()
{
  ue_task_scheds.stop();
}

ue_index_t ue_manager::add_ue(du_index_t du_index)
{
  if (ues.size() == ue_config.max_nof_supported_ues) {
    logger.warning("CU-CP UE creation Failed. Cause: Maximum number of UEs {} supported by the CU-CP has been reached",
                   ue_config.max_nof_supported_ues);
    return ue_index_t::invalid;
  }

  ue_index_t new_ue_index = allocate_ue_index(du_index);
  if (new_ue_index == ue_index_t::invalid) {
    logger.warning("CU-CP UE creation Failed. Cause: No free UE index available");
    return ue_index_t::invalid;
  }

  // Create a dedicated task scheduler for the UE.
  ue_task_scheduler_impl ue_sched = ue_task_scheds.create_ue_task_sched(new_ue_index);

  // Create UE object
  ues.emplace(std::piecewise_construct,
              std::forward_as_tuple(new_ue_index),
              std::forward_as_tuple(new_ue_index, du_index, up_config, sec_config, std::move(ue_sched)));

  logger.info("ue={}: Created new CU-CP UE", new_ue_index);

  return new_ue_index;
}

void ue_manager::remove_ue(ue_index_t ue_index)
{
  if (ue_index == ue_index_t::invalid) {
    logger.warning("Can't remove UE with invalid UE index");
    return;
  }

  if (ues.find(ue_index) == ues.end()) {
    logger.warning("ue={}: Remove UE called for inexistent UE", ue_index);
    return;
  }

  // remove UE from lookups
  pci_t pci = ues.at(ue_index).get_pci();
  if (pci != INVALID_PCI) {
    rnti_t c_rnti = ues.at(ue_index).get_c_rnti();
    if (c_rnti != rnti_t::INVALID_RNTI) {
      pci_rnti_to_ue_index.erase(std::make_tuple(pci, c_rnti));
    } else {
      logger.warning("ue={}: RNTI not found", ue_index);
    }
  } else {
    logger.debug("ue={}: PCI not found", ue_index);
  }

  // Remove CU-CP UE from database
  ues.erase(ue_index);

  logger.debug("ue={}: Removed", ue_index);
}

ue_index_t ue_manager::get_ue_index(pci_t pci, rnti_t rnti)
{
  if (pci_rnti_to_ue_index.find(std::make_tuple(pci, rnti)) != pci_rnti_to_ue_index.end()) {
    return pci_rnti_to_ue_index.at(std::make_tuple(pci, rnti));
  }
  logger.debug("UE index for pci={} and rnti={} not found", pci, rnti);
  return ue_index_t::invalid;
}

// common

cu_cp_ue* ue_manager::find_ue(ue_index_t ue_index)
{
  if (ues.find(ue_index) != ues.end()) {
    return &ues.at(ue_index);
  }
  return nullptr;
}

ue_task_scheduler* ue_manager::find_ue_task_scheduler(ue_index_t ue_index)
{
  if (ues.find(ue_index) != ues.end() && ues.at(ue_index).du_ue_created()) {
    return &ues.at(ue_index).get_task_sched();
  }
  return nullptr;
}

// du processor

cu_cp_ue* ue_manager::set_ue_du_context(ue_index_t ue_index, gnb_du_id_t du_id, pci_t pci, rnti_t rnti)
{
  srsran_assert(ue_index != ue_index_t::invalid, "Invalid ue_index={}", ue_index);
  srsran_assert(pci != INVALID_PCI, "Invalid pci={}", pci);
  srsran_assert(rnti != rnti_t::INVALID_RNTI, "Invalid rnti={}", rnti);

  // check if ue_index is in db
  if (ues.find(ue_index) == ues.end()) {
    logger.warning("ue={}: UE not found", ue_index);
    return nullptr;
  }

  // check if the UE is already present
  if (get_ue_index(pci, rnti) != ue_index_t::invalid) {
    logger.warning("UE with pci={} and rnti={} already exists", pci, rnti);
    return nullptr;
  }

  auto& ue = ues.at(ue_index);
  ue.update_du_ue(du_id, pci, rnti);

  // Add PCI and RNTI to lookup.
  pci_rnti_to_ue_index.emplace(std::make_tuple(pci, rnti), ue_index);

  logger.debug("ue={}: Updated UE with pci={} and rnti={}", ue_index, pci, rnti);

  return &ue;
}

cu_cp_ue* ue_manager::find_du_ue(ue_index_t ue_index)
{
  if (ues.find(ue_index) != ues.end() && ues.at(ue_index).du_ue_created()) {
    return &ues.at(ue_index);
  }
  return nullptr;
}

size_t ue_manager::get_nof_du_ues(du_index_t du_index)
{
  unsigned ue_count = 0;
  // Count UEs connected to the DU
  for (const auto& ue : ues) {
    if (ue.second.get_du_index() == du_index) {
      ue_count++;
    }
  }

  return ue_count;
}

std::vector<metrics_report::ue_info> ue_manager::handle_ue_metrics_report_request() const
{
  std::vector<metrics_report::ue_info> report;
  report.reserve(ues.size());

  for (const auto& ue : ues) {
    report.emplace_back();
    metrics_report::ue_info& ue_report = report.back();

    ue_report.rnti  = ue.second.get_c_rnti();
    ue_report.du_id = ue.second.get_du_id();
    ue_report.pci   = ue.second.get_pci();
  }

  return report;
}

// private functions

ue_index_t ue_manager::allocate_ue_index(du_index_t du_index)
{
  // Search unallocated UE index
  for (uint16_t i = 0; i < ue_config.max_nof_supported_ues; i++) {
    ue_index_t new_ue_index = generate_ue_index(du_index, i);
    if (ues.find(new_ue_index) == ues.end()) {
      logger.debug("Allocating new ue_index={} for du_index={}", new_ue_index, du_index);
      return new_ue_index;
    }
  }
  return ue_index_t::invalid;
}

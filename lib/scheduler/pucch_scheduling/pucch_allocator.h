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

#include "../cell/resource_grid.h"
#include "../config/ue_configuration.h"
#include "../ue_context/ue.h"
#include "srsran/scheduler/scheduler_slot_handler.h"

namespace srsran {

/// Contains the number of UCI HARQ-ACK and CSI information bits of a removed PUCCH grant.
struct pucch_uci_bits {
  /// Number of HARQ-ACK info bits that should have been reported in the removed PUCCH grant.
  unsigned harq_ack_nof_bits{0};
  /// Number of SR info bits that should have been reported in the removed PUCCH grant.
  sr_nof_bits sr_bits{sr_nof_bits::no_sr};
  /// Number of CSI Part 1 info bits that should have been reported in the removed PUCCH grant.
  unsigned csi_part1_nof_bits{0};
  // TODO: add extra bits for CSI Part 2.

  [[nodiscard]] unsigned get_total_bits() const
  {
    return harq_ack_nof_bits + sr_nof_bits_to_uint(sr_bits) + csi_part1_nof_bits;
  }
};

/// PUCCH scheduling interface.
class pucch_allocator
{
public:
  virtual ~pucch_allocator() = default;

  /// \brief Signal a new slot indication to reset the PUCCH common allocation grid.
  virtual void slot_indication(slot_point sl_tx) = 0;

  /// Allocate the common PUCCH resource for HARQ-ACK for a given UE.
  /// \param[out,in] res_alloc struct with scheduling results.
  /// \param[in] tcrnti temporary RNTI of the UE.
  /// \param[in] k0 k0 value, or delay (in slots) of PDSCH slot vs the corresponding PDCCH slot.
  /// \param[in] k1 delay in slots of the UE's PUCCH HARQ-ACK report with respect to the PDSCH.
  /// \param[in] dci_info information with DL DCI, needed for HARQ-(N)-ACK scheduling info.
  /// \return The PUCCH resource indicator, if the allocation is successful; a \c std::nullopt otherwise.
  /// \remark \c pucch_res_indicator, or \f$\Delta_{PRI}\f$, is the <em>PUCCH resource indicator<\em> field for DCI 1_0
  /// and 1_1 as per TS 38.213, Section 9.2.1. It indicates the UE which PUCCH resource should be used for HACK-(N)ACK
  /// reporting.
  virtual std::optional<unsigned> alloc_common_pucch_harq_ack_ue(cell_resource_allocator&    res_alloc,
                                                                 rnti_t                      tcrnti,
                                                                 unsigned                    k0,
                                                                 unsigned                    k1,
                                                                 const pdcch_dl_information& dci_info) = 0;

  /// Allocate the common PUCCH resource for HARQ-ACK for a given UE.
  /// \param[out,in] res_alloc struct with scheduling results.
  /// \param[in] rnti RNTI of the UE.
  /// \param[in] ue_cell_cfg user configuration.
  /// \param[in] k0 k0 value, or delay (in slots) of PDSCH slot vs the corresponding PDCCH slot.
  /// \param[in] k1 delay in slots of the UE's PUCCH HARQ-ACK report with respect to the PDSCH.
  /// \param[in] dci_info information with DL DCI, needed for HARQ-(N)-ACK scheduling info.
  /// \return The PUCCH resource indicator, if the allocation is successful; a \c std::nullopt otherwise.
  /// \remark \c pucch_res_indicator, or \f$\Delta_{PRI}\f$, is the <em>PUCCH resource indicator<\em> field for DCI 1_0
  /// and 1_1 as per TS 38.213, Section 9.2.1. It indicates the UE which PUCCH resource should be used for HACK-(N)ACK
  /// reporting.
  virtual std::optional<unsigned> alloc_common_and_ded_harq_res(cell_resource_allocator&     res_alloc,
                                                                rnti_t                       rnti,
                                                                const ue_cell_configuration& ue_cell_cfg,
                                                                unsigned                     k0,
                                                                unsigned                     k1,
                                                                const pdcch_dl_information&  dci_info) = 0;

  /// Allocate the PUCCH resource for a UE's SR opportunity.
  /// \param[out,in] pucch_slot_alloc struct with scheduling results.
  /// \param[in] crnti C-RNTI of the UE.
  /// \param[in] ue_cell_cfg user configuration.
  virtual void pucch_allocate_sr_opportunity(cell_slot_resource_allocator& pucch_slot_alloc,
                                             rnti_t                        crnti,
                                             const ue_cell_configuration&  ue_cell_cfg) = 0;

  /// Allocate a PUCCH HARQ-ACK grant for a given UE using dedicated resources.
  ///
  /// \remark This function does not check whether there are PUSCH grants allocated for the same UE. The check needs to
  /// be performed by the caller.
  ///
  /// \param[out,in] res_alloc struct with scheduling results.
  /// \param[in] crnti RNTI of the UE.
  /// \param[in] ue_cell_cfg user configuration.
  /// \param[in] k0 k0 value, or delay (in slots) of PDSCH slot vs the corresponding PDCCH slot.
  /// \param[in] k1 delay in slots of the UE's PUCCH HARQ-ACK report with respect to the PDSCH.
  /// \return The PUCCH resource indicator, if the allocation is successful; a \c std::nullopt otherwise.
  virtual std::optional<unsigned> alloc_ded_pucch_harq_ack_ue(cell_resource_allocator&     res_alloc,
                                                              rnti_t                       crnti,
                                                              const ue_cell_configuration& ue_cell_cfg,
                                                              unsigned                     k0,
                                                              unsigned                     k1) = 0;

  /// Allocate the PUCCH grant for a UE's CSI opportunity.
  /// \param[out,in] pucch_slot_alloc struct with scheduling results.
  /// \param[in] crnti C-RNTI of the UE.
  /// \param[in] ue_cell_cfg user configuration.
  /// \param[in] csi_part1_nof_bits Number of CSI Part 1 bits that need to be reported.
  virtual void pucch_allocate_csi_opportunity(cell_slot_resource_allocator& pucch_slot_alloc,
                                              rnti_t                        crnti,
                                              const ue_cell_configuration&  ue_cell_cfg,
                                              unsigned                      csi_part1_nof_bits) = 0;

  /// Remove UCI allocations on PUCCH for a given UE.
  /// \param[out,in] slot_alloc struct with scheduling results.
  /// \param[in] crnti RNTI of the UE.
  /// \param[in] ue_cell_cfg User configuration.
  /// \return struct with the number of HARQ-ACK and CSI info bits from the removed PUCCH grants. If there was no PUCCH
  /// to be removed, return 0 for both HARQ-ACK and CSI info bits.
  virtual pucch_uci_bits remove_ue_uci_from_pucch(cell_slot_resource_allocator& slot_alloc,
                                                  rnti_t                        crnti,
                                                  const ue_cell_configuration&  ue_cell_cfg) = 0;

  /// Returns whether a PUCCH grant using common PUCCH resource already exists at a given slot for a UE.
  /// \param[in] rnti RNTI of the UE.
  /// \param[in] sl_tx Slot to search PUCCH grants.
  /// \return Returns true if a PUCCH grant using common PUCCH resource exits. False, otherwise.
  virtual bool has_common_pucch_grant(rnti_t rnti, slot_point sl_tx) const = 0;
};

} // namespace srsran

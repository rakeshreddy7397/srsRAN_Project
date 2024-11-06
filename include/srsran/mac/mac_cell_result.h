
#pragma once

#include "srsran/adt/byte_buffer.h"
#include "srsran/adt/byte_buffer_chain.h"
#include "srsran/adt/static_vector.h"
#include "srsran/ran/pdcch/dci_packing.h"
#include "srsran/ran/slot_point.h"
#include "srsran/ran/ssb_properties.h"
#include "srsran/scheduler/scheduler_slot_handler.h"
#include "srsran/support/shared_transport_block.h"

namespace srsran {

/// \brief Describes part of the parameters that are encoded in the MIB payload as per TS38.331 Section 6.2.2 - MIB.
struct ssb_mib_data_pdu {
  /// Symbol position of the first DM-RS.
  dmrs_typeA_position dmrs_typeA_pos;
  /// Value used to derive the PDCCH, CORESET and common search space configurations.
  uint8_t pdcch_config_sib1;
  /// Flag: true if the cell is barred, false otherwise.
  bool cell_barred;
  /// Flag: true if doing cell selection/reselection into other intra-frequency cells is permitted, false otherwise.
  bool intra_freq_reselection;
};

/// \brief Describes all the parameters related to an SSB allocation.
struct dl_ssb_pdu {
  /// Physical cell identifier.
  pci_t pci;
  /// PSS to SSS EPRE ratio.
  ssb_pss_to_sss_epre pss_to_sss_epre;
  /// SSB opportunity index in a burst.
  uint8_t ssb_index;
  /// \brief Alignment offset between the resource grid and the SS/PBCH block.
  /// \sa ssb_subcarrier_offset for more details.
  ssb_subcarrier_offset subcarrier_offset;
  /// \brief Start of the SS/PBCH block relative to Point A in PRB.
  /// \sa ssb_offset_to_pointA for more details.
  ssb_offset_to_pointA offset_to_pointA;
  /// SS/PBCH pattern case.
  ssb_pattern_case ssb_case;
  /// Maximum number of SS/PBCH block candidates in a 5ms burst, described in TS38.213 section 4.1.
  uint8_t L_max;
  /// Subcarrier spacing of the SSB.
  subcarrier_spacing scs;
  /// Data for MIB generation.
  ssb_mib_data_pdu mib_data;
};

/// DL Scheduling Request generated by the MAC and received by the PHY.
struct mac_dl_sched_result {
  slot_point                                             slot;
  const dl_sched_result*                                 dl_res;
  static_vector<dl_ssb_pdu, MAX_SSB_PER_SLOT>            ssb_pdus;
  static_vector<dci_payload, MAX_DL_PDCCH_PDUS_PER_SLOT> dl_pdcch_pdus;
  static_vector<dci_payload, MAX_UL_PDCCH_PDUS_PER_SLOT> ul_pdcch_pdus;
};

/// List of DL PDUs produced by MAC in a given slot and cell.
struct mac_dl_data_result {
  /// Describes the parameters related to a downlink PDU.
  struct dl_pdu {
    dl_pdu(unsigned cw_index_, shared_transport_block&& pdu_) : cw_index(cw_index_), pdu(std::move(pdu_)) {}

    /// Codeword index.
    unsigned cw_index;
    /// PDU contents.
    shared_transport_block pdu;
  };

  slot_point                                      slot;
  static_vector<dl_pdu, MAX_SI_PDUS_PER_SLOT>     si_pdus;
  static_vector<dl_pdu, MAX_RAR_PDUS_PER_SLOT>    rar_pdus;
  static_vector<dl_pdu, MAX_UE_PDUS_PER_SLOT>     ue_pdus;
  static_vector<dl_pdu, MAX_PAGING_PDUS_PER_SLOT> paging_pdus;
};

struct mac_ul_sched_result {
  slot_point             slot;
  const ul_sched_result* ul_res;
};

class mac_cell_result_notifier
{
public:
  virtual ~mac_cell_result_notifier() = default;

  /// Notifies scheduled SSB/PDCCH/PDSCH grants.
  virtual void on_new_downlink_scheduler_results(const mac_dl_sched_result& dl_res) = 0;

  /// Notifies scheduled PDSCH PDUs.
  virtual void on_new_downlink_data(const mac_dl_data_result& dl_data) = 0;

  /// Notifies slot scheduled PUCCH/PUSCH grants.
  virtual void on_new_uplink_scheduler_results(const mac_ul_sched_result& ul_res) = 0;

  /// Notifies the completion of all cell results for the given slot.
  virtual void on_cell_results_completion(slot_point slot) = 0;
};

class mac_result_notifier
{
public:
  virtual ~mac_result_notifier() = default;

  virtual mac_cell_result_notifier& get_cell(du_cell_index_t cell_index) = 0;
};

} // namespace srsran

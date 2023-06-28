/*
 *
 * Copyright 2021-2023 Software Radio Systems Limited
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the distribution.
 *
 */

#pragma once

#include "f1u_bearer_logger.h"
#include "srsran/f1u/cu_up/f1u_bearer.h"
#include "srsran/f1u/cu_up/f1u_rx_delivery_notifier.h"
#include "srsran/f1u/cu_up/f1u_rx_sdu_notifier.h"
#include "srsran/f1u/cu_up/f1u_tx_pdu_notifier.h"
#include "srsran/ran/lcid.h"
#include "srsran/support/timers.h"

namespace srsran {
namespace srs_cu_up {

class f1u_bearer_impl final : public f1u_bearer, public f1u_rx_pdu_handler, public f1u_tx_sdu_handler
{
public:
  f1u_bearer_impl(uint32_t                  ue_index,
                  drb_id_t                  drb_id_,
                  f1u_tx_pdu_notifier&      tx_pdu_notifier_,
                  f1u_rx_delivery_notifier& rx_delivery_notifier_,
                  f1u_rx_sdu_notifier&      rx_sdu_notifier_,
                  timer_factory             timers,
                  f1u_bearer_disconnector&  diconnector_,
                  gtpu_teid_t               ul_teid_);
  f1u_bearer_impl(const f1u_bearer_impl&)            = delete;
  f1u_bearer_impl& operator=(const f1u_bearer_impl&) = delete;

  ~f1u_bearer_impl() override { disconnector.disconnect_cu_bearer(ul_teid); }

  f1u_rx_pdu_handler& get_rx_pdu_handler() override { return *this; }
  f1u_tx_sdu_handler& get_tx_sdu_handler() override { return *this; }

  void handle_pdu(nru_ul_message msg) override;
  void handle_sdu(pdcp_tx_pdu sdu) override;
  void discard_sdu(uint32_t pdcp_sn) override;

  /// \brief Returns the UL TEID that was assigned upon construction.
  /// \return The UL TEID associated with this bearer.
  gtpu_teid_t get_ul_teid() const { return ul_teid; }

  /// \brief This function handles the periodic transmission of downlink notification towards lower layers with all
  /// discard blocks that have aggregated since the previous DL PDU.
  void on_expired_dl_notif_timer();

private:
  f1u_bearer_logger         logger;
  f1u_tx_pdu_notifier&      tx_pdu_notifier;
  f1u_rx_delivery_notifier& rx_delivery_notifier;
  f1u_rx_sdu_notifier&      rx_sdu_notifier;
  f1u_bearer_disconnector&  disconnector;
  gtpu_teid_t               ul_teid;

  /// Downlink notification timer that triggers periodic transmission of discard blocks towards lower layers. The
  /// purpose of this timer is to avoid excessive downlink notifications for every PDCP SN that is discarded by upper
  /// layers.
  unique_timer dl_notif_timer;

  /// Collection of pending \c nru_pdcp_sn_discard_block objects.
  nru_pdcp_sn_discard_blocks discard_blocks;

  /// \brief Fills the provided \c nru_dl_message with all SDU discard blocks that have been aggregated since the last
  /// call of this function (or since creation of this object).
  /// \param msg The message to be filled with SDU discard blocks.
  void fill_discard_blocks(nru_dl_message& msg);

  /// \brief Immediately transmits a \c nru_dl_message with all currently aggregated SDU discard blocks, if any.
  void flush_discard_blocks();
};

} // namespace srs_cu_up
} // namespace srsran

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

#include "../e1ap_cu_cp_impl.h"
#include "../ue_context/e1ap_cu_cp_ue_context.h"
#include "common/e1ap_asn1_utils.h"
#include "srsran/e1ap/common/e1ap_message.h"
#include "srsran/e1ap/cu_cp/e1ap_cu_cp.h"
#include "srsran/support/async/async_task.h"

namespace srsran {
namespace srs_cu_cp {

class bearer_context_release_procedure
{
public:
  bearer_context_release_procedure(const e1ap_message&              command_,
                                   e1ap_bearer_transaction_manager& ev_mng_,
                                   e1ap_message_notifier&           e1ap_notif_,
                                   e1ap_ue_logger&                  logger_);

  void operator()(coro_context<async_task<void>>& ctx);

  static const char* name() { return "Bearer Context Release Procedure"; }

private:
  /// Send Bearer Context Release Command to CU-UP.
  void send_bearer_context_release_command();

  /// Handles procedure result and returns back to procedure caller.
  void handle_bearer_context_release_complete();

  const e1ap_message               command;
  e1ap_bearer_transaction_manager& ev_mng;
  e1ap_message_notifier&           e1ap_notifier;
  e1ap_ue_logger&                  logger;

  protocol_transaction_outcome_observer<asn1::e1ap::bearer_context_release_complete_s> transaction_sink;
};

} // namespace srs_cu_cp
} // namespace srsran
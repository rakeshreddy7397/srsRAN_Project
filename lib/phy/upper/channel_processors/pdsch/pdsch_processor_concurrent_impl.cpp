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

#include "pdsch_processor_concurrent_impl.h"
#include "pdsch_processor_helpers.h"
#include "pdsch_processor_validator_impl.h"
#include "srsran/instrumentation/traces/du_traces.h"
#include "srsran/phy/support/resource_grid_mapper.h"
#include "srsran/ran/ptrs/ptrs_pattern.h"
#include "srsran/support/tracing/event_tracing.h"

using namespace srsran;

/// \brief Looks at the output of the validator and, if unsuccessful, fills msg with the error message.
///
/// This is used to call the validator inside the process methods only if asserts are active.
[[maybe_unused]] static bool handle_validation(std::string& msg, const error_type<std::string>& err)
{
  bool is_success = err.has_value();
  if (!is_success) {
    msg = err.error();
  }
  return is_success;
}

void pdsch_processor_concurrent_impl::process(resource_grid_writer&                                           grid_,
                                              pdsch_processor_notifier&                                       notifier_,
                                              static_vector<shared_transport_block, MAX_NOF_TRANSPORT_BLOCKS> data_,
                                              const pdsch_processor::pdu_t&                                   pdu_)
{
  // Saves inputs.
  save_inputs(grid_, notifier_, std::move(data_), pdu_);

  // Makes sure the PDU is valid.
  [[maybe_unused]] std::string msg;
  srsran_assert(handle_validation(msg, pdsch_processor_validator_impl().is_valid(pdu_)), "{}", msg);

  // Set the number of asynchronous tasks. It counts as CB processing and DM-RS generation.
  async_task_counter = 2;

  // Add PT-RS to the asynchronous tasks.
  if (config.ptrs) {
    ++async_task_counter;

    // Process PT-RS concurrently.
    auto ptrs_task = [this]() {
      trace_point process_ptrs_tp = l1_tracer.now();

      pdsch_process_ptrs(*grid, ptrs_generator_pool->get(), config);

      l1_tracer << trace_event("process_ptrs", process_ptrs_tp);

      // Decrement asynchronous task counter.
      if (async_task_counter.fetch_sub(1) == 1) {
        // Notify end of the processing.
        notifier->on_finish_processing();
      }
    };

    bool success = false;
    if (cb_processor_pool->capacity() > 1) {
      success = executor.execute(ptrs_task);
    }

    if (!success) {
      ptrs_task();
    }
  }

  // Process DM-RS concurrently.
  auto dmrs_task = [this]() {
    trace_point process_dmrs_tp = l1_tracer.now();

    pdsch_process_dmrs(*grid, dmrs_generator_pool->get(), config);

    l1_tracer << trace_event("process_dmrs", process_dmrs_tp);

    // Decrement asynchronous task counter.
    if (async_task_counter.fetch_sub(1) == 1) {
      // Notify end of the processing.
      notifier->on_finish_processing();
    }
  };
  if (!executor.execute(dmrs_task)) {
    dmrs_task();
  }

  // Fork codeblock processing tasks.
  fork_cb_batches();
}

void pdsch_processor_concurrent_impl::save_inputs(resource_grid_writer&     grid_,
                                                  pdsch_processor_notifier& notifier_,
                                                  static_vector<shared_transport_block, MAX_NOF_TRANSPORT_BLOCKS> data_,
                                                  const pdsch_processor::pdu_t&                                   pdu)
{
  using namespace units::literals;

  // Save process parameter inputs.
  grid     = &grid_;
  notifier = &notifier_;
  data     = std::move(data_.front());
  config   = pdu;

  // Codeword index is fix.
  static constexpr unsigned i_cw = 0;

  // The number of layers is equal to the number of ports.
  unsigned nof_layers = config.precoding.get_nof_layers();

  // Calculate the number of resource elements used to map PDSCH on the grid. Common for all codewords.
  unsigned nof_re_pdsch = pdsch_compute_nof_data_re(config);

  // Calculate the total number of the channel modulated symbols.
  nof_ch_symbols = nof_layers * nof_re_pdsch;

  // Calculate scrambling initial state.
  scrambler->init((static_cast<unsigned>(config.rnti) << 15U) + (i_cw << 14U) + config.n_id);

  // Calculate transport block size.
  tbs = units::bytes(data.get_buffer().size()).to_bits();

  // Calculate number of codeblocks.
  nof_cb = ldpc::compute_nof_codeblocks(tbs, config.ldpc_base_graph);

  // Number of segments that will have a short rate-matched length. In TS38.212 Section 5.4.2.1, these correspond to
  // codeblocks whose length E_r is computed by rounding down - floor. For the remaining codewords, the length is
  // rounded up.
  unsigned nof_short_segments = nof_cb - (nof_re_pdsch % nof_cb);

  // Compute number of CRC bits for the transport block.
  units::bits nof_tb_crc_bits = ldpc::compute_tb_crc_size(tbs);

  // Compute number of CRC bits for each codeblock.
  units::bits nof_cb_crc_bits = (nof_cb > 1) ? 24_bits : 0_bits;

  // Calculate the total number of bits including transport block and codeblock CRC.
  units::bits nof_tb_bits_out = tbs + nof_tb_crc_bits + units::bits(nof_cb_crc_bits * nof_cb);

  // Compute the number of information bits that is assigned to a codeblock.
  cb_info_bits = units::bits(divide_ceil(nof_tb_bits_out.value(), nof_cb)) - nof_cb_crc_bits;

  unsigned lifting_size = ldpc::compute_lifting_size(tbs, config.ldpc_base_graph, nof_cb);
  segment_length        = ldpc::compute_codeblock_size(config.ldpc_base_graph, lifting_size);

  modulation_scheme modulation = config.codewords.front().modulation;
  units::bits       bits_per_symbol(get_bits_per_symbol(modulation));

  units::bits full_codeblock_size = ldpc::compute_full_codeblock_size(config.ldpc_base_graph, segment_length);
  units::bits cw_length           = units::bits(nof_re_pdsch * nof_layers * bits_per_symbol);
  zero_pad                        = (cb_info_bits + nof_cb_crc_bits) * nof_cb - nof_tb_bits_out;

  // Calculate rate match buffer size.
  units::bits Nref = ldpc::compute_N_ref(config.tbs_lbrm, nof_cb);

  // Prepare codeblock metadata.
  cb_metadata.tb_common.base_graph        = config.ldpc_base_graph;
  cb_metadata.tb_common.lifting_size      = static_cast<ldpc::lifting_size_t>(lifting_size);
  cb_metadata.tb_common.rv                = config.codewords.front().rv;
  cb_metadata.tb_common.mod               = modulation;
  cb_metadata.tb_common.Nref              = Nref.value();
  cb_metadata.tb_common.cw_length         = cw_length.value();
  cb_metadata.cb_specific.full_length     = full_codeblock_size.value();
  cb_metadata.cb_specific.rm_length       = 0;
  cb_metadata.cb_specific.nof_filler_bits = (segment_length - cb_info_bits - nof_cb_crc_bits).value();
  cb_metadata.cb_specific.cw_offset       = 0;
  cb_metadata.cb_specific.nof_crc_bits    = nof_tb_crc_bits.value();

  // Calculate RM length for each codeblock.
  rm_length.resize(nof_cb);
  cw_offset.resize(nof_cb);
  re_offset.resize(nof_cb);
  unsigned re_count_sum = 0;
  for (unsigned i_cb = 0; i_cb != nof_cb; ++i_cb) {
    // Calculate RM length in RE.
    unsigned rm_length_re = divide_ceil(nof_re_pdsch, nof_cb);
    if (i_cb < nof_short_segments) {
      rm_length_re = nof_re_pdsch / nof_cb;
    }

    // Convert RM length from RE to bits.
    rm_length[i_cb] = rm_length_re * nof_layers * bits_per_symbol;

    // Set and increment CW offset.
    cw_offset[i_cb] = re_count_sum * nof_layers * bits_per_symbol;

    // Set RE offset for the resource mapper.
    re_offset[i_cb] = re_count_sum;

    // Increment RE count.
    re_count_sum += rm_length_re;
  }
  srsran_assert(re_count_sum * nof_layers * bits_per_symbol == cw_length,
                "RM length sum (i.e., {}) must be equal to the codeword length (i.e., {}).",
                units::bits(re_count_sum * nof_layers * bits_per_symbol),
                cw_length);

  // Get the PRB allocation mask.
  const bounded_bitset<MAX_RB> prb_allocation_mask =
      config.freq_alloc.get_prb_mask(config.bwp_start_rb, config.bwp_size_rb);

  // First symbol used in this transmission.
  unsigned start_symbol_index = config.start_symbol_index;

  // Calculate the end symbol index (excluded) and assert it does not exceed the slot boundary.
  unsigned end_symbol_index = config.start_symbol_index + config.nof_symbols;

  srsran_assert(end_symbol_index <= MAX_NSYMB_PER_SLOT,
                "The time allocation of the transmission ({}:{}) exceeds the slot boundary.",
                start_symbol_index,
                end_symbol_index);

  // PDSCH OFDM symbol mask.
  symbol_slot_mask symbols;
  symbols.fill(start_symbol_index, end_symbol_index);

  // Allocation pattern for the mapper.
  allocation.clear();
  re_pattern pdsch_pattern;

  // Reserved REs, including DM-RS and CSI-RS.
  reserved = re_pattern_list(config.reserved);

  // Get DM-RS RE pattern.
  re_pattern dmrs_pattern = config.dmrs.get_dmrs_pattern(
      config.bwp_start_rb, config.bwp_size_rb, config.nof_cdm_groups_without_data, config.dmrs_symbol_mask);

  // Merge DM-RS RE pattern into the reserved RE patterns.
  reserved.merge(dmrs_pattern);

  // Set PDSCH allocation pattern.
  pdsch_pattern.prb_mask = prb_allocation_mask;
  pdsch_pattern.re_mask  = ~re_prb_mask();
  pdsch_pattern.symbols  = symbols;
  allocation.merge(pdsch_pattern);

  // Calculate modulation scaling.
  float scaling = convert_dB_to_amplitude(-config.ratio_pdsch_data_to_sss_dB);
  scaling *= cb_processor_pool->get().get_scaling(config.codewords.front().modulation);

  // Apply scaling to the precoding matrix.
  precoding = config.precoding;
  precoding *= scaling;
}

void pdsch_processor_concurrent_impl::fork_cb_batches()
{
  // Create a task for eack thread in the pool.
  unsigned nof_cb_tasks = cb_processor_pool->capacity();

  // Limit the number of tasks to the number of codeblocks.
  nof_cb_tasks = std::min(nof_cb_tasks, nof_cb);

  // Set number of codeblock batches.
  cb_task_counter = nof_cb_tasks;
  cb_counter      = 0;

  auto async_task = [this]() {
    // Select codeblock processor.
    pdsch_codeblock_processor& cb_processor = cb_processor_pool->get();

    // For each segment within the batch.
    unsigned absolute_i_cb;
    while ((absolute_i_cb = cb_counter.fetch_add(1)) < nof_cb) {
      trace_point process_pdsch_tp = l1_tracer.now();

      // As the last codeblock has a higher overhead due to the transport block CRC calculation. Reverse codeblock order
      // to process first the last CB.
      absolute_i_cb = nof_cb - 1 - absolute_i_cb;

      // Limit the codeblock number of information bits.
      units::bits nof_info_bits = std::min<units::bits>(cb_info_bits, tbs - cb_info_bits * absolute_i_cb);

      // Set CB processor configuration.
      pdsch_codeblock_processor::configuration cb_config;
      cb_config.tb_offset    = cb_info_bits * absolute_i_cb;
      cb_config.has_cb_crc   = nof_cb > 1;
      cb_config.cb_info_size = nof_info_bits;
      cb_config.cb_size      = segment_length;
      cb_config.zero_pad     = zero_pad;
      cb_config.metadata     = cb_metadata;
      cb_config.c_init       = scrambler->get_state();

      // Update codeblock specific metadata fields.
      cb_config.metadata.cb_specific.cw_offset = cw_offset[absolute_i_cb].value();
      cb_config.metadata.cb_specific.rm_length = rm_length[absolute_i_cb].value();

      // Process codeblock.
      pdsch_codeblock_processor::result result = cb_processor.process(data.get_buffer(), cb_config);

      // Build resource grid mapper adaptor.
      resource_grid_mapper::symbol_buffer_adapter buffer(result.cb_symbols);

      // Map into the resource grid.
      mapper->map(*grid, buffer, allocation, reserved, precoding, re_offset[absolute_i_cb]);

      l1_tracer << trace_event((absolute_i_cb == (nof_cb - 1)) ? "Last CB" : "CB", process_pdsch_tp);
    }

    // Decrement code block batch counter.
    if (cb_task_counter.fetch_sub(1) == 1) {
      // No more code block tasks pending to execute, it is now safe to discard the TB buffer.
      data.release();
      // Decrement asynchronous task counter.
      if (async_task_counter.fetch_sub(1) == 1) {
        // Notify end of the processing.
        notifier->on_finish_processing();
      }
    }
  };

  // Spawn tasks.
  for (unsigned i_task = 0; i_task != nof_cb_tasks; ++i_task) {
    // Try to execute task asynchronously.
    bool successful = false;
    if (nof_cb_tasks > 1) {
      successful = executor.execute(async_task);
    }

    // Execute task locally if it was not enqueued.
    if (!successful) {
      async_task();
    }
  }
}

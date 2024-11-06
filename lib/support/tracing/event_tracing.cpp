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

#include "srsran/support/tracing/event_tracing.h"
#include "srsran/srslog/srslog.h"
#include "srsran/support/executors/task_worker.h"
#include "srsran/support/executors/unique_thread.h"
#include "srsran/support/format/custom_formattable.h"
#include "srsran/support/format/fmt_basic_parser.h"
#include "srsran/support/tracing/rusage_trace_recorder.h"
#include "fmt/chrono.h"
#include <sched.h>

using namespace srsran;
using namespace std::chrono;

namespace {

/// Helper class to write trace events to a file.
class event_trace_writer
{
public:
  explicit event_trace_writer(const char* trace_file) :
    fptr(::fopen(trace_file, "w")), trace_worker("tracer_worker", 2048, std::chrono::microseconds{200})
  {
    if (fptr == nullptr) {
      report_fatal_error("ERROR: Failed to open trace file {}", trace_file);
    }
    fmt::print(fptr, "[");
  }
  event_trace_writer(event_trace_writer&& other) noexcept                 = delete;
  event_trace_writer(const event_trace_writer& other) noexcept            = delete;
  event_trace_writer& operator=(event_trace_writer&& other) noexcept      = delete;
  event_trace_writer& operator=(const event_trace_writer& other) noexcept = delete;

  ~event_trace_writer()
  {
    trace_worker.wait_pending_tasks();
    trace_worker.stop();
    fmt::print(fptr, "\n]");
    ::fclose(fptr);
  }

  template <typename EventType>
  void write_trace(const EventType& ev)
  {
    if (not trace_worker.push_task([this, ev]() {
          if (SRSRAN_LIKELY(not first_entry)) {
            fmt::print(fptr, ",\n{}", ev);
          } else {
            fmt::print(fptr, "\n{}", ev);
            first_entry = false;
          }
        })) {
      if (not warn_logged.exchange(true, std::memory_order_relaxed)) {
        file_event_tracer<true> warn_tracer;
        warn_tracer << instant_trace_event{"trace_overflow", instant_trace_event::cpu_scope::global};
        srslog::fetch_basic_logger("ALL").warning("Tracing thread cannot keep up with the number of events.");
      }
    }
  }

private:
  FILE* fptr;
  /// Task worker to process events.
  general_task_worker<concurrent_queue_policy::lockfree_mpmc, concurrent_queue_wait_policy::sleep> trace_worker;
  bool                                                                                             first_entry = true;
  std::atomic<bool>                                                                                warn_logged{false};
};

struct trace_event_extended : public trace_event {
  unsigned       cpu;
  const char*    thread_name;
  trace_duration duration;

  trace_event_extended(const trace_event& event, trace_duration duration_) :
    trace_event(event), cpu(sched_getcpu()), thread_name(this_thread_name()), duration(duration_)
  {
  }
};

struct instant_trace_event_extended : public instant_trace_event {
  unsigned    cpu;
  const char* thread_name;
  trace_point tp;

  instant_trace_event_extended(const instant_trace_event& event) :
    instant_trace_event(event), cpu(sched_getcpu()), thread_name(this_thread_name()), tp(trace_point::clock::now())
  {
  }
};

struct rusage_trace_event_extended : public trace_event_extended {
  resource_usage::diff rusage_diff;

  rusage_trace_event_extended(const trace_event& event, trace_duration duration_, resource_usage::diff rusage_diff_) :
    trace_event_extended(event, duration_), rusage_diff(rusage_diff_)
  {
  }
};

} // namespace

static trace_point run_epoch = trace_clock::now();
/// Unique event trace file writer.
static std::unique_ptr<event_trace_writer> trace_file_writer;

void srsran::open_trace_file(std::string_view trace_file_name)
{
  if (trace_file_writer != nullptr) {
    report_fatal_error("Trace file '{}' already open", trace_file_name);
  }
  trace_file_writer = std::make_unique<event_trace_writer>(trace_file_name.data());
}

void srsran::close_trace_file()
{
  trace_file_writer = nullptr;
}

bool srsran::is_trace_file_open()
{
  return trace_file_writer != nullptr;
}

static auto formatted_date(trace_point tp)
{
  return make_formattable([tp](auto& ctx) {
    std::tm current_time = fmt::gmtime(std::chrono::high_resolution_clock::to_time_t(tp));
    auto us_fraction = std::chrono::duration_cast<std::chrono::microseconds>(tp.time_since_epoch()).count() % 1000000u;
    return fmt::format_to(ctx.out(), "{:%H:%M:%S}.{:06}", current_time, us_fraction);
  });
}

namespace fmt {

/// Common fmt parser to all events.
struct trace_format_parser {
  bool is_log;

  template <typename ParseContext>
  auto parse(ParseContext& ctx)
  {
    auto it = ctx.begin();
    is_log  = false;
    while (it != ctx.end() and *it != '}') {
      if (*it == 'l') {
        is_log = true;
      }
      ++it;
    }
    return it;
  }
};

template <>
struct formatter<trace_event_extended> : public trace_format_parser {
  template <typename FormatContext>
  auto format(const trace_event_extended& event, FormatContext& ctx)
  {
    auto ts = duration_cast<microseconds>(event.start_tp - run_epoch).count();

    if (is_log) {
      return format_to(ctx.out(),
                       "event=\"{}\": cpu={} tid=\"{}\" tstamp={} ts={}_usec dur={}_usec",
                       event.name,
                       event.cpu,
                       event.thread_name,
                       formatted_date(event.start_tp),
                       ts,
                       event.duration.count());
    }

    return format_to(ctx.out(),
                     "{{\"args\": {{}}, \"pid\": {}, \"tid\": \"{}\", "
                     "\"dur\": {}, \"ts\": {}, \"cat\": \"process\", \"ph\": \"X\", "
                     "\"name\": \"{}\"}}",
                     event.cpu,
                     event.thread_name,
                     event.duration.count(),
                     ts,
                     event.name);
  }
};

template <>
struct formatter<instant_trace_event_extended> : public trace_format_parser {
  template <typename FormatContext>
  auto format(const instant_trace_event_extended& event, FormatContext& ctx)

  {
    static const char* scope_str[] = {"g", "p", "t"};

    auto ts = duration_cast<microseconds>(event.tp - run_epoch).count();

    if (is_log) {
      return format_to(ctx.out(),
                       "instant_event=\"{}\": cpu={} tid=\"{}\" tstamp={} ts={}_usec",
                       event.name,
                       event.cpu,
                       event.thread_name,
                       formatted_date(event.tp),
                       ts);
    }

    return format_to(ctx.out(),
                     "{{\"args\": {{\"tstamp\": \"{}\"}}, \"pid\": {}, \"tid\": \"{}\", "
                     "\"ts\": {}, \"cat\": \"process\", \"ph\": \"i\", \"s\": \"{}\", "
                     "\"name\": \"{}\"}}",
                     formatted_date(event.tp),
                     event.cpu,
                     event.thread_name,
                     ts,
                     scope_str[(unsigned)event.scope],
                     event.name);
  }
};

template <>
struct formatter<rusage_trace_event_extended> : public trace_format_parser {
  template <typename FormatContext>
  auto format(const rusage_trace_event_extended& event, FormatContext& ctx)

  {
    auto ts = duration_cast<microseconds>(event.start_tp - run_epoch).count();

    if (is_log) {
      return format_to(ctx.out(),
                       "rusage_event=\"{}\": cpu={} tid=\"{}\" start_tstamp={} ts={}_usec dur={}_usec "
                       "vol_ctxt_switch={} invol_ctxt_switch={}",
                       event.name,
                       event.cpu,
                       event.thread_name,
                       formatted_date(event.start_tp),
                       ts,
                       event.duration.count(),
                       event.rusage_diff.vol_ctxt_switch_count,
                       event.rusage_diff.invol_ctxt_switch_count);
    }

    return format_to(ctx.out(),
                     "{{\"args\": {{\"start_tstamp\": \"{}\", \"vol_ctxt_switch\": {}, \"invol_ctxt_switch\": {}}}, "
                     "\"pid\": {}, \"tid\": \"{}\", \"dur\": {}, "
                     "\"ts\": {}, \"cat\": \"process\", \"ph\": \"X\", "
                     "\"name\": \"{}\"}}",
                     formatted_date(event.start_tp),
                     event.rusage_diff.vol_ctxt_switch_count,
                     event.rusage_diff.invol_ctxt_switch_count,
                     event.cpu,
                     event.thread_name,
                     event.duration.count(),
                     ts,
                     event.name);
  }
};

} // namespace fmt

template <>
bool file_event_tracer<true>::is_enabled() const
{
  return is_trace_file_open();
}

template <>
void file_event_tracer<true>::operator<<(const trace_event& event) const
{
  if (not is_enabled()) {
    return;
  }
  const auto dur = std::chrono::duration_cast<trace_duration>(now() - event.start_tp);
  trace_file_writer->write_trace(trace_event_extended{event, dur});
}

template <>
void file_event_tracer<true>::operator<<(const trace_thres_event& event) const
{
  if (not is_enabled()) {
    return;
  }
  const auto dur = std::chrono::duration_cast<trace_duration>(now() - event.start_tp);
  if (dur >= event.thres) {
    trace_file_writer->write_trace(trace_event_extended{trace_event{event.name, event.start_tp}, dur});
  }
}

template <>
void file_event_tracer<true>::operator<<(const instant_trace_event& event) const
{
  if (not is_enabled()) {
    return;
  }
  trace_file_writer->write_trace(instant_trace_event_extended{event});
}

template <>
void file_event_tracer<true>::operator<<(const rusage_trace_event& event) const
{
  if (not is_enabled()) {
    return;
  }
  const auto dur = std::chrono::duration_cast<trace_duration>(now() - event.start_tp);
  trace_file_writer->write_trace(rusage_trace_event_extended{
      trace_event{event.name, event.start_tp}, dur, resource_usage::now().value() - event.rusg_capture});
}

template <>
void file_event_tracer<true>::operator<<(const rusage_thres_trace_event& event) const
{
  if (not is_enabled()) {
    return;
  }
  const auto dur = std::chrono::duration_cast<trace_duration>(now() - event.start_tp);
  if (dur >= event.thres) {
    trace_file_writer->write_trace(rusage_trace_event_extended{
        trace_event{event.name, event.start_tp}, dur, resource_usage::now().value() - event.rusg_capture});
  }
}

template <>
void file_event_tracer<true>::operator<<(span<const rusage_trace_event> events) const
{
  if (not is_enabled()) {
    return;
  }
  // Log total
  trace_file_writer->write_trace(rusage_trace_event_extended{
      trace_event{events.front().name, events.front().start_tp},
      std::chrono::duration_cast<trace_duration>(events.back().start_tp - events.front().start_tp),
      events.back().rusg_capture - events.front().rusg_capture});
  if (events.size() > 2) {
    auto prev_it = events.begin();
    for (auto it = prev_it + 1; it != events.end(); prev_it = it++) {
      trace_file_writer->write_trace(
          rusage_trace_event_extended{trace_event{it->name, prev_it->start_tp},
                                      std::chrono::duration_cast<trace_duration>(it->start_tp - prev_it->start_tp),
                                      it->rusg_capture - prev_it->rusg_capture});
    }
  }
}

template <>
void logger_event_tracer<true>::push(const trace_event& event) const
{
  (*log_ch)("{:l}", trace_event_extended{event, std::chrono::duration_cast<trace_duration>(now() - event.start_tp)});
}

template <>
void logger_event_tracer<true>::push(const trace_thres_event& event) const
{
  const trace_duration dur = std::chrono::duration_cast<trace_duration>(now() - event.start_tp);
  if (dur >= event.thres) {
    (*log_ch)("{:l}", trace_event_extended{trace_event{event.name, event.start_tp}, dur});
  }
}

template <>
void logger_event_tracer<true>::push(const instant_trace_event& event) const
{
  (*log_ch)("{:l}", instant_trace_event_extended{event});
}

template <>
void logger_event_tracer<true>::push(const rusage_trace_event& event) const
{
  const auto dur = std::chrono::duration_cast<trace_duration>(now() - event.start_tp);
  (*log_ch)(
      "{:l}",
      rusage_trace_event_extended{trace_event{event.name, event.start_tp},
                                  dur,
                                  resource_usage::now().value_or(resource_usage::snapshot{0, 0}) - event.rusg_capture});
}

template <>
void logger_event_tracer<true>::push(const rusage_thres_trace_event& event) const
{
  const auto dur = std::chrono::duration_cast<trace_duration>(now() - event.start_tp);
  if (dur >= event.thres) {
    (*log_ch)("{:l}",
              rusage_trace_event_extended{trace_event{event.name, event.start_tp},
                                          dur,
                                          resource_usage::now().value_or(resource_usage::snapshot{0, 0}) -
                                              event.rusg_capture});
  }
}

template <>
void logger_event_tracer<true>::push(span<const rusage_trace_event> events) const
{
  // Log total
  (*log_ch)("{:l}",
            rusage_trace_event_extended{
                trace_event{events.front().name, events.front().start_tp},
                std::chrono::duration_cast<trace_duration>(events.back().start_tp - events.front().start_tp),
                events.back().rusg_capture - events.front().rusg_capture});
  if (events.size() > 2) {
    auto prev_it = events.begin();
    for (auto it = prev_it + 1; it != events.end(); prev_it = it++) {
      (*log_ch)(
          "{:l}",
          rusage_trace_event_extended{trace_event{it->name, prev_it->start_tp},
                                      std::chrono::duration_cast<trace_duration>(it->start_tp - prev_it->start_tp),
                                      it->rusg_capture - prev_it->rusg_capture});
    }
  }
}

void test_event_tracer::operator<<(const trace_event& event)
{
  const auto end_tp = now();
  last_events.push_back(
      fmt::format(is_log_stype ? "{:l}" : "{}",
                  trace_event_extended{event, std::chrono::duration_cast<trace_duration>(end_tp - event.start_tp)}));
}

void test_event_tracer::operator<<(const trace_thres_event& event)
{
  const trace_duration dur = std::chrono::duration_cast<trace_duration>(now() - event.start_tp);
  if (dur >= event.thres) {
    last_events.push_back(
        fmt::format(is_log_stype ? "{:l}" : "{}", trace_event_extended{trace_event{event.name, event.start_tp}, dur}));
  }
}

void test_event_tracer::operator<<(const instant_trace_event& event)
{
  last_events.push_back(fmt::format(is_log_stype ? "{:l}" : "{}", instant_trace_event_extended{event}));
}

void test_event_tracer::operator<<(const rusage_trace_event& event)
{
  const auto dur = std::chrono::duration_cast<trace_duration>(now() - event.start_tp);
  last_events.push_back(fmt::format(is_log_stype ? "{:l}" : "{}",
                                    rusage_trace_event_extended{trace_event{event.name, event.start_tp},
                                                                dur,
                                                                resource_usage::now().value() - event.rusg_capture}));
}

void test_event_tracer::operator<<(const rusage_thres_trace_event& event)
{
  const auto dur = std::chrono::duration_cast<trace_duration>(now() - event.start_tp);
  if (dur >= event.thres) {
    last_events.push_back(fmt::format(is_log_stype ? "{:l}" : "{}",
                                      rusage_trace_event_extended{trace_event{event.name, event.start_tp},
                                                                  dur,
                                                                  resource_usage::now().value() - event.rusg_capture}));
  }
}

void test_event_tracer::operator<<(span<const rusage_trace_event> events)
{
  // Log total
  last_events.push_back(
      fmt::format(is_log_stype ? "{:l}" : "{}",
                  rusage_trace_event_extended{
                      trace_event{events.front().name, events.front().start_tp},
                      std::chrono::duration_cast<trace_duration>(events.back().start_tp - events.front().start_tp),
                      events.back().rusg_capture - events.front().rusg_capture}));
  if (events.size() > 2) {
    auto prev_it = events.begin();
    for (auto it = prev_it + 1; it != events.end(); prev_it = it++) {
      last_events.push_back(fmt::format(
          is_log_stype ? "{:l}" : "{}",
          rusage_trace_event_extended{trace_event{it->name, prev_it->start_tp},
                                      std::chrono::duration_cast<trace_duration>(it->start_tp - prev_it->start_tp),
                                      it->rusg_capture - prev_it->rusg_capture}));
    }
  }
}

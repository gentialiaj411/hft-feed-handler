#include "mf/service/tick_replay_service.hpp"

#include "mf/journal/journal_reader.hpp"
#include "mf/service/event_codec.hpp"

namespace mf::service {

namespace {
constexpr const char* kServiceMode =
    "recorded_market_data_replay_query; not live exchange connectivity";
constexpr const char* kBuildId = "multifeed-track1-v1";
}  // namespace

TickReplayServiceImpl::TickReplayServiceImpl(std::shared_ptr<const ReplayCatalog> catalog)
    : catalog_(std::move(catalog)) {}

grpc::Status TickReplayServiceImpl::Health(grpc::ServerContext* /*context*/,
                                           const mf::replay::v1::HealthRequest* /*request*/,
                                           mf::replay::v1::HealthResponse* response) {
  std::lock_guard lock(mu_);
  response->set_book_journal_path(catalog_->book_journal_path);
  response->set_nbbo_journal_path(catalog_->nbbo_journal_path);
  response->set_book_record_count(catalog_->book_record_count);
  response->set_nbbo_record_count(catalog_->nbbo_record_count);
  response->set_uptime_seconds(catalog_->uptime_seconds());
  response->set_build_id(kBuildId);
  response->set_service_mode(kServiceMode);
  return grpc::Status::OK;
}

grpc::Status TickReplayServiceImpl::StreamTicks(
    grpc::ServerContext* /*context*/,
    const mf::replay::v1::StreamTicksRequest* request,
    grpc::ServerWriter<mf::replay::v1::BookEventMsg>* writer) {
#if !defined(__linux__)
  return grpc::Status(grpc::StatusCode::UNIMPLEMENTED, "journal replay requires Linux");
#else
  const bool filter_symbol = !request->symbol().empty();
  const std::uint64_t want_u64 = filter_symbol ? symbol_to_u64(request->symbol()) : 0;
  const std::uint64_t start_ts = request->start_ts_ns();
  const std::uint64_t end_ts = request->end_ts_ns();
  const std::uint32_t limit = request->limit();

  mf::journal::JournalReader reader;
  {
    std::lock_guard lock(mu_);
    if (!reader.open(catalog_->book_journal_path)) {
      return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "book journal not open");
    }
  }

  mf::core::BookEvent ev{};
  std::uint64_t ingest = 0;
  std::uint64_t seq = 0;
  std::uint32_t sent = 0;
  while (reader.next(ev, ingest, seq)) {
    if (filter_symbol && ev.symbol.as_u64() != want_u64) {
      continue;
    }
    if (!in_ts_window(ev.exchange_ts_ns, start_ts, end_ts)) {
      continue;
    }
    mf::replay::v1::BookEventMsg msg;
    book_event_to_proto(ev, msg);
    if (!writer->Write(msg)) {
      return grpc::Status::OK;
    }
    ++sent;
    if (limit > 0 && sent >= limit) {
      break;
    }
  }
  if (reader.had_error()) {
    return grpc::Status(grpc::StatusCode::DATA_LOSS, reader.error_reason());
  }
  return grpc::Status::OK;
#endif
}

grpc::Status TickReplayServiceImpl::QueryNbbo(grpc::ServerContext* /*context*/,
                                              const mf::replay::v1::QueryNbboRequest* request,
                                              mf::replay::v1::NbboSnapshot* response) {
  std::lock_guard lock(mu_);
  if (catalog_->nbbo_events.empty()) {
    return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "NBBO journal not loaded");
  }
  const std::uint64_t want_u64 = symbol_to_u64(request->symbol());
  const std::uint64_t ts = request->timestamp_ns();

  const mf::journal::NbboEvent* best = nullptr;
  for (const auto& row : catalog_->nbbo_events) {
    if (row.symbol_u64 != want_u64) {
      continue;
    }
    if (row.exchange_ts_ns > ts) {
      continue;
    }
    if (best == nullptr || row.exchange_ts_ns > best->exchange_ts_ns) {
      best = &row;
    }
  }

  if (best == nullptr) {
    response->set_found(false);
    return grpc::Status::OK;
  }
  response->set_found(true);
  nbbo_event_to_proto(*best, *response->mutable_nbbo());
  return grpc::Status::OK;
}

grpc::Status TickReplayServiceImpl::StreamNbbo(
    grpc::ServerContext* /*context*/,
    const mf::replay::v1::StreamNbboRequest* request,
    grpc::ServerWriter<mf::replay::v1::NbboEventMsg>* writer) {
  std::lock_guard lock(mu_);
  if (catalog_->nbbo_events.empty()) {
    return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "NBBO journal not loaded");
  }

  const bool filter_symbol = !request->symbol().empty();
  const std::uint64_t want_u64 = filter_symbol ? symbol_to_u64(request->symbol()) : 0;
  const std::uint64_t start_ts = request->start_ts_ns();
  const std::uint64_t end_ts = request->end_ts_ns();
  const std::uint32_t limit = request->limit();

  std::uint32_t sent = 0;
  for (const auto& row : catalog_->nbbo_events) {
    if (filter_symbol && row.symbol_u64 != want_u64) {
      continue;
    }
    if (!in_ts_window(row.exchange_ts_ns, start_ts, end_ts)) {
      continue;
    }
    mf::replay::v1::NbboEventMsg msg;
    nbbo_event_to_proto(row, msg);
    if (!writer->Write(msg)) {
      return grpc::Status::OK;
    }
    ++sent;
    if (limit > 0 && sent >= limit) {
      break;
    }
  }
  return grpc::Status::OK;
}

}  // namespace mf::service

#pragma once

#include <memory>
#include <mutex>

#include "mf/service/replay_catalog.hpp"
#include "tick_service.grpc.pb.h"

namespace mf::service {

class TickReplayServiceImpl final : public mf::replay::v1::TickReplayService::Service {
 public:
  explicit TickReplayServiceImpl(std::shared_ptr<const ReplayCatalog> catalog);

  grpc::Status Health(grpc::ServerContext* context,
                        const mf::replay::v1::HealthRequest* request,
                        mf::replay::v1::HealthResponse* response) override;

  grpc::Status StreamTicks(grpc::ServerContext* context,
                           const mf::replay::v1::StreamTicksRequest* request,
                           grpc::ServerWriter<mf::replay::v1::BookEventMsg>* writer) override;

  grpc::Status QueryNbbo(grpc::ServerContext* context,
                         const mf::replay::v1::QueryNbboRequest* request,
                         mf::replay::v1::NbboSnapshot* response) override;

  grpc::Status StreamNbbo(grpc::ServerContext* context,
                          const mf::replay::v1::StreamNbboRequest* request,
                          grpc::ServerWriter<mf::replay::v1::NbboEventMsg>* writer) override;

 private:
  std::shared_ptr<const ReplayCatalog> catalog_;
  mutable std::mutex mu_;
};

}  // namespace mf::service

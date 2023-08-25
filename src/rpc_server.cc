#include <absl/strings/str_format.h>
#include <glog/logging.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include <cstdint>
#include <cstdlib>
#include <grpcpp/support/status.h>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

#include "db.h"
#include "url_shortener.grpc.pb.h"
#include "url_shortening.h"

namespace {

struct Config {
  const uint64_t *highwayhash_key;
  uint16_t rpc_port;

  struct ConfigPtrDeleter {
    void operator()(Config *c) const noexcept {
      if (c->highwayhash_key != nullptr) {
        delete c->highwayhash_key;
      }
    }
  };
  [[nodiscard]] static std::unique_ptr<Config, ConfigPtrDeleter> env_config();
};

auto Config::env_config() -> std::unique_ptr<Config, Config::ConfigPtrDeleter> {
  const char *highwayhash_key_inp =
      std::getenv("EC_PRV_URL_SHORTENER__HIGHWAYHASH_KEY");
  if (nullptr == highwayhash_key_inp) {
    std::cerr << "Missing highwayhash key\n";
    return nullptr;
  }
  const uint64_t *hhkey =
      ec_prv::url_shortener::url_shortening::create_highwayhash_key(
          highwayhash_key_inp);

  uint16_t rpc_port = 50051;
  const char *rpc_port_s = std::getenv("EC_PRV_URL_SHORTENER__RPC_PORT");
  if (rpc_port_s != nullptr) {
    rpc_port = static_cast<uint16_t>(std::atoi(rpc_port_s));
  }
  std::unique_ptr<Config, Config::ConfigPtrDeleter> dst{new Config{},
                                                        ConfigPtrDeleter{}};
  dst->highwayhash_key = hhkey;
  dst->rpc_port = rpc_port;
  return dst;
}

class UrlShortenerImpl final : public ec_prv::UrlShortener::Service {
private:
  std::shared_ptr<ec_prv::url_shortener::db::ShortenedUrlsDatabase>
      shortened_urls_database_;
  const Config *cfg_;

public:
  explicit UrlShortenerImpl(
      std::shared_ptr<ec_prv::url_shortener::db::ShortenedUrlsDatabase>
          shortened_urls_database,
      const Config *cfg)
      : shortened_urls_database_(shortened_urls_database), cfg_(cfg) {}

  ::grpc::Status ShortenUrl(::grpc::ServerContext *context,
                            const ::ec_prv::ShortenUrlRequest *request,
                            ::ec_prv::ShortenedUrlResponse *response) override {
    auto generated =
        ec_prv::url_shortener::url_shortening::generate_shortened_url(
            request->long_url(), cfg_->highwayhash_key);
    auto res = shortened_urls_database_->put(generated, request->long_url());
    if (std::holds_alternative<
            ::ec_prv::url_shortener::db::UrlShorteningDbError>(res)) {
      return ::grpc::Status(::grpc::StatusCode::ABORTED, "Fail");
    }
    response->set_long_url(request->long_url());
    response->set_short_url(generated);
    return ::grpc::Status::OK;
  }
  ::grpc::Status LookupUrl(::grpc::ServerContext *context,
                           const ::ec_prv::UrlLookupRequest *request,
                           ::ec_prv::UrlLookupResponse *response) override {
    auto answer = shortened_urls_database_->get(request->short_url());
    if (std::holds_alternative<
            ::ec_prv::url_shortener::db::UrlShorteningDbError>(answer)) {
      auto err = std::get_if<::ec_prv::url_shortener::db::UrlShorteningDbError>(
          &answer);
      if (*err == ::ec_prv::url_shortener::db::UrlShorteningDbError::NotFound) {
        return ::grpc::Status(::grpc::StatusCode::NOT_FOUND, "Not Found");
      }
      LOG(ERROR) << "lookup failed in database for slug: "
                 << request->short_url();
      return ::grpc::Status(::grpc::StatusCode::ABORTED, "Fail");
    }
    const std::string *long_url = std::get_if<std::string>(&answer);
    CHECK(long_url != nullptr);
    response->set_long_url(*long_url);
    response->set_short_url(request->short_url());
    return ::grpc::Status::OK;
  }
};

void run_server(uint16_t port) {
  std::unique_ptr<Config, Config::ConfigPtrDeleter> cfg = Config::env_config();
  if (!cfg) {
    throw std::runtime_error{"Configuration not found"};
  }
  std::shared_ptr<ec_prv::url_shortener::db::ShortenedUrlsDatabase> db =
      ec_prv::url_shortener::db::ShortenedUrlsDatabase::open();
  UrlShortenerImpl service(db, cfg.get());
  ::grpc::EnableDefaultHealthCheckService(true);
  ::grpc::reflection::InitProtoReflectionServerBuilderPlugin();
  ::grpc::ServerBuilder builder;
  std::string listen_address = absl::StrFormat("0.0.0.0:%d", cfg->rpc_port);
  builder.AddListeningPort(listen_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<::grpc::Server> server(builder.BuildAndStart());
  std::cerr << "gRPC server listening\n";
  server->Wait();
}

} // namespace

int main(int argc, char **argv) {
  ::run_server(0);
  return 0;
}

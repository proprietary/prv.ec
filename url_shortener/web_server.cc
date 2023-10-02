#include <chrono>
#include <folly/Memory.h>
#include <folly/ThreadLocal.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/executors/GlobalExecutor.h>
#include <folly/init/Init.h>
#include <folly/io/async/EventBaseManager.h>
#include <folly/portability/GFlags.h>
#include <folly/portability/Unistd.h>
#include <glog/logging.h>
#include <memory>
#include <proxygen/httpserver/HTTPServer.h>
#include <proxygen/httpserver/RequestHandler.h>
#include <proxygen/httpserver/RequestHandlerFactory.h>
#include <proxygen/httpserver/ResponseBuilder.h>
#include <string>

#include "app_config.h"
#include "url_shortening.h"

// request handlers
#include "ddos_protection.h"
#include "frontend_handler.h"
#include "make_url_request_handler.h"
#include "static_handler.h"
#include "url_shortener_handler.h"

DEFINE_int32(h2_port, 11002, "Port to listen on with HTTP/2 protocol");
DEFINE_int32(threads, 1,
             "Number of threads to listen on. Numbers <= 0 "
             "will use the number of cores on this machine. Default 1.");
DEFINE_string(config_file, "", "Path to configuration file");

namespace {

class NotFoundHandler : public proxygen::RequestHandler {
public:
  void onRequest(std::unique_ptr<proxygen::HTTPMessage> req) noexcept override {
    DLOG(INFO) << "Responded with 404";
    proxygen::ResponseBuilder(downstream_)
        .status(404, "Not Found")
        .sendWithEOM();
  }
  void onBody(std::unique_ptr<folly::IOBuf> body) noexcept override {}
  void onUpgrade(proxygen::UpgradeProtocol _) noexcept override {}
  void onEOM() noexcept override {}
  void requestComplete() noexcept override { delete this; }
  void onError(proxygen::ProxygenError err) noexcept override { delete this; }
};

class MyRequestHandlerFactory : public proxygen::RequestHandlerFactory {
public:
  MyRequestHandlerFactory(
      const ::ec_prv::url_shortener::app_config::ReadOnlyAppConfig *app_state,
      const ::ec_prv::url_shortener::url_shortening::UrlShorteningConfig
          *const url_shortening_svc,
      std::shared_ptr<::ec_prv::url_shortener::db::ShortenedUrlsDatabase> db,
      const folly::F14NodeMap<std::string, std::vector<uint8_t>>
          *const frontend_dir_cache)
      : app_state_(app_state), url_shortening_svc_(url_shortening_svc), db_(db),
        frontend_dir_cache_(frontend_dir_cache) {}
  void onServerStart(folly::EventBase *evb) noexcept override {
    static_file_cache_.reset(
        new ::ec_prv::url_shortener::web::StaticFileCache{});
    timer_ = folly::HHWheelTimer::newTimer(
        evb,
        std::chrono::milliseconds(folly::HHWheelTimer::DEFAULT_TICK_INTERVAL),
        folly::AsyncTimeout::InternalEnum::NORMAL,
        std::chrono::milliseconds(5000));
  }
  void onServerStop() noexcept override {
    static_file_cache_.reset();
    timer_.reset();
  }
  proxygen::RequestHandler *
  onRequest(proxygen::RequestHandler *request_handler,
            proxygen::HTTPMessage *msg) noexcept override {
    std::string_view path{msg->getPathAsStringPiece().begin(),
                          msg->getPathAsStringPiece().end()};
    auto method = msg->getMethod();
    ::ec_prv::url_shortener::web::FrontendHandler *maybe_frontend =
        ::ec_prv::url_shortener::web::FrontendHandler::lookup(
            frontend_dir_cache_, path);
    if (maybe_frontend != nullptr) {
      return maybe_frontend;
    }
    if (path.starts_with("/static/") && method == proxygen::HTTPMethod::GET) {
      // serve static files
      DLOG(INFO) << "Route \"static\" found. Serving static files.";
      return new ::ec_prv::url_shortener::web::StaticHandler(
          static_file_cache_, app_state_->static_file_doc_root);
    } else if (path.starts_with("/api/")) {
      DLOG(INFO) << "Route \"/api/*\" found.";
      if (path == "/api/v1/create" && (method == proxygen::HTTPMethod::POST ||
                                       method == proxygen::HTTPMethod::PUT)) {
        return new ::ec_prv::url_shortener::web::MakeUrlRequestHandler(
            db_.get(), timer_.get(), app_state_, url_shortening_svc_);
      } else {
        return new NotFoundHandler{};
      }
    } else if (std::string_view parsed =
                   ec_prv::url_shortener::url_shortening::parse_out_request_str(
                       path);
               parsed.length() > 0 && method == proxygen::HTTPMethod::GET) {
      return new ::ec_prv::url_shortener::web::UrlRedirectHandler(
          std::string{parsed}, db_.get(), app_state_);
    }
    return new NotFoundHandler();
  }

private:
  const ::ec_prv::url_shortener::app_config::ReadOnlyAppConfig
      *app_state_; // TODO: make this a folly::ThreadLocalPtr
  const ::ec_prv::url_shortener::url_shortening::UrlShorteningConfig
      *const url_shortening_svc_;
  std::shared_ptr<::ec_prv::url_shortener::db::ShortenedUrlsDatabase>
      db_; // TODO(zds): make access to rocksdb threadsafe
  // folly::ThreadLocalPtr<::ec_prv::url_shortener::web::StaticFileCache>
  // static_file_cache_{nullptr};
  std::shared_ptr<::ec_prv::url_shortener::web::StaticFileCache>
      static_file_cache_{nullptr};
  folly::HHWheelTimer::UniquePtr timer_;
  const folly::F14NodeMap<std::string, std::vector<uint8_t>>
      *const frontend_dir_cache_;
};

} // namespace

int main(int argc, char *argv[]) {
  // gflags::ParseCommandLineFlags(&argc, &argv, true);
  // google::InitGoogleLogging(argv[0]);
  // google::InstallFailureSignalHandler();

  LOG(INFO) << "Starting up url shortener...";

  folly::Init _folly_init{&argc, &argv, false};

  std::unique_ptr<::ec_prv::url_shortener::app_config::ReadOnlyAppConfig,
                  ::ec_prv::url_shortener::app_config::ReadOnlyAppConfig::
                      ReadOnlyAppConfigDeleter>
      ro_app_state{nullptr, ::ec_prv::url_shortener::app_config::
                                ReadOnlyAppConfig::ReadOnlyAppConfigDeleter{}};
  if (!FLAGS_config_file.empty()) {
    const auto config_file_path = std::filesystem::path{FLAGS_config_file};
    CHECK(std::filesystem::exists(config_file_path))
        << "Config file at \"" << FLAGS_config_file << "\" does not exist";
    ro_app_state =
        ::ec_prv::url_shortener::app_config::ReadOnlyAppConfig::new_from_yaml(
            config_file_path);
  } else {
    ro_app_state =
        ::ec_prv::url_shortener::app_config::ReadOnlyAppConfig::new_from_env();
  }

  std::unique_ptr<::ec_prv::url_shortener::url_shortening::UrlShorteningConfig>
      url_shortening_svc = std::make_unique<
          ::ec_prv::url_shortener::url_shortening::UrlShorteningConfig>(
          ro_app_state->alphabet, ro_app_state->slug_length,
          ro_app_state->highwayhash_key);

  std::vector<proxygen::HTTPServer::IPConfig> IPs = {
      {folly::SocketAddress(ro_app_state->web_server_bind_host,
                            ro_app_state->web_server_port, true),
       proxygen::HTTPServer::Protocol::HTTP},
      {folly::SocketAddress(ro_app_state->web_server_bind_host, FLAGS_h2_port,
                            true),
       proxygen::HTTPServer::Protocol::HTTP2},
  };
  // TODO(zds): add support for http3?

  if (FLAGS_threads <= 0) {
    FLAGS_threads = sysconf(_SC_NPROCESSORS_ONLN);
    CHECK(FLAGS_threads > 0);
  }

  std::shared_ptr<::ec_prv::url_shortener::db::ShortenedUrlsDatabase> db =
      ::ec_prv::url_shortener::db::ShortenedUrlsDatabase::open(
          ro_app_state->urls_db_path);

  const char *highwayhash_key_inp =
      std::getenv("EC_PRV_URL_SHORTENER__HIGHWAYHASH_KEY");
  CHECK(nullptr != highwayhash_key_inp)
      << "missing environment variable for highwayhash key";
  if (nullptr == highwayhash_key_inp) {
    std::cerr << "Missing highwayhash key\n";
    return 1;
  }
  const uint64_t *highwayhash_key =
      ::ec_prv::url_shortener::url_shortening::create_highwayhash_key(
          highwayhash_key_inp);

  // build cache of frontend directory files
  std::unique_ptr<folly::F14NodeMap<std::string, std::vector<uint8_t>>>
      frontend_dir_cache =
          ::ec_prv::url_shortener::web::build_frontend_dir_cache(
              ro_app_state->frontend_doc_root);

  proxygen::HTTPServerOptions options;
  options.threads = static_cast<size_t>(FLAGS_threads);
  options.idleTimeout = std::chrono::milliseconds(60000);
  options.shutdownOn = {SIGINT, SIGTERM};
  options.enableContentCompression = false;
  options.handlerFactories =
      proxygen::RequestHandlerChain()
          .addThen<::ec_prv::url_shortener::web::AntiAbuseProtection>(
              ro_app_state.get())
          .addThen<MyRequestHandlerFactory>(ro_app_state.get(),
                                            url_shortening_svc.get(), db,
                                            frontend_dir_cache.get())
          .build();
  // Increase the default flow control to 1MB/10MB
  options.initialReceiveWindow = uint32_t(1 << 20);
  options.receiveStreamWindowSize = uint32_t(1 << 20);
  options.receiveSessionWindowSize = 10 * (1 << 20);
  options.h2cEnabled = true;

  proxygen::HTTPServer server(std::move(options));
  server.bind(IPs);

  // Start HTTPServer mainloop in a separate thread
  std::thread t([&]() { server.start(); });
  t.join();

  return 0;
}

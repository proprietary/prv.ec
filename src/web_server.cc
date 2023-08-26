#include <chrono>
#include <folly/Memory.h>
#include <folly/ThreadLocal.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/executors/GlobalExecutor.h>
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
#include "static_handler.h"
#include "url_shortener_handler.h"
#include "make_url_request_handler.h"

DEFINE_int32(http_port, 11000, "Port to listen on with HTTP protocol");
DEFINE_int32(spdy_port, 11001, "Port to listen on with SPDY protocol");
DEFINE_int32(h2_port, 11002, "Port to listen on with HTTP/2 protocol");
DEFINE_string(ip, "localhost", "IP/Hostname to bind to");
DEFINE_int32(threads, 0,
             "Number of threads to listen on. Numbers <= 0 "
             "will use the number of cores on this machine.");

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
			  std::shared_ptr<::ec_prv::url_shortener::db::ShortenedUrlsDatabase> db)
    : app_state_(app_state), db_(db) {
  }
  void onServerStart(folly::EventBase *evb) noexcept override {
    static_file_cache_.reset(new ::ec_prv::url_shortener::web::StaticFileCache{});
    timer_ = folly::HHWheelTimer::newTimer(
					       evb,
					       std::chrono::milliseconds(folly::HHWheelTimer::DEFAULT_TICK_INTERVAL),
					       folly::AsyncTimeout::InternalEnum::NORMAL, std::chrono::milliseconds(5000));
  }
  void onServerStop() noexcept override {
    static_file_cache_.reset();
    timer_.reset();
  }
  proxygen::RequestHandler *
  onRequest(proxygen::RequestHandler *request_handler,
            proxygen::HTTPMessage *msg) noexcept override {
    if (msg->getPathAsStringPiece() == "/" &&
        msg->getMethod() == proxygen::HTTPMethod::GET) {
      // serve home page
      DLOG(INFO) << "Detected route \"/\". Serving home page.";
      // use StaticHandler to serve a specific file
      return new ::ec_prv::url_shortener::web::StaticHandler(static_file_cache_, app_state_->static_file_doc_root, "index.html");
    } else if (msg->getPath().starts_with("/static/") &&
               msg->getMethod() == proxygen::HTTPMethod::GET) {
      // serve static files
      DLOG(INFO) << "Route \"static\" found. Serving static files.";
      return new ::ec_prv::url_shortener::web::StaticHandler(static_file_cache_, app_state_->static_file_doc_root, {}); 
    } else if (msg->getPath().starts_with("/api/")) {
      DLOG(INFO) << "Route \"/api/*\" found.";
      return new ::ec_prv::url_shortener::web::MakeUrlRequestHandler(db_.get(), timer_.get(), app_state_->highwayhash_key);
    } // else if
      // (ec_prv::url_shortener::url_shortening::is_ok_request_path(msg->getPath())
      // && msg->getMethod() == proxygen::HTTPMethod::GET) {
    std::string_view parsed = ec_prv::url_shortener::url_shortening::parse_out_request_str(msg->getPathAsStringPiece());
    if (parsed.length() > 0 || (msg->getPathAsStringPiece() == "/" && msg->getMethod() == proxygen::HTTPMethod::POST)) {
      return new ::ec_prv::url_shortener::web::UrlShortenerApiRequestHandler(
          db_.get() /* TODO: fix unsafe pointer passing */,
          app_state_->highwayhash_key);
    }
    return new NotFoundHandler();
  }

private:
  const ::ec_prv::url_shortener::app_config::ReadOnlyAppConfig
      *app_state_; // TODO: make this a folly::ThreadLocalPtr
  std::shared_ptr<::ec_prv::url_shortener::db::ShortenedUrlsDatabase> db_;
  // folly::ThreadLocalPtr<::ec_prv::url_shortener::web::StaticFileCache> static_file_cache_{nullptr};
  std::shared_ptr<::ec_prv::url_shortener::web::StaticFileCache> static_file_cache_{nullptr};
  folly::HHWheelTimer::UniquePtr timer_;
};

} // namespace

int main(int argc, char *argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);
  google::InstallFailureSignalHandler();

  std::unique_ptr<::ec_prv::url_shortener::app_config::ReadOnlyAppConfig, ::ec_prv::url_shortener::app_config::ReadOnlyAppConfig::ReadOnlyAppConfigDeleter> ro_app_state = ::ec_prv::url_shortener::app_config::ReadOnlyAppConfig::new_from_env();

  std::vector<proxygen::HTTPServer::IPConfig> IPs = {
      {folly::SocketAddress(ro_app_state->web_server_bind_host, ro_app_state->web_server_port, true),
       proxygen::HTTPServer::Protocol::HTTP},
      {folly::SocketAddress(ro_app_state->web_server_bind_host, FLAGS_spdy_port, true),
       proxygen::HTTPServer::Protocol::SPDY},
      {folly::SocketAddress(ro_app_state->web_server_bind_host, FLAGS_h2_port, true),
       proxygen::HTTPServer::Protocol::HTTP2},
  };
  // TODO(zds): add support for http3?

  if (FLAGS_threads <= 0) {
    FLAGS_threads = sysconf(_SC_NPROCESSORS_ONLN);
    CHECK(FLAGS_threads > 0);
  }

  std::shared_ptr<::ec_prv::url_shortener::db::ShortenedUrlsDatabase> db =
      ::ec_prv::url_shortener::db::ShortenedUrlsDatabase::open();

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

  proxygen::HTTPServerOptions options;
  options.threads = static_cast<size_t>(FLAGS_threads);
  options.idleTimeout = std::chrono::milliseconds(60000);
  options.shutdownOn = {SIGINT, SIGTERM};
  options.enableContentCompression = false;
  options.handlerFactories =
      proxygen::RequestHandlerChain()
    .addThen<MyRequestHandlerFactory>(ro_app_state.get(), db)
          .build();
  // Increase the default flow control to 1MB/10MB
  options.initialReceiveWindow = uint32_t(1 << 20);
  options.receiveStreamWindowSize = uint32_t(1 << 20);
  options.receiveSessionWindowSize = 10 * (1 << 20);
  options.h2cEnabled = true;

  auto disk_io_thread_pool = std::make_shared<folly::CPUThreadPoolExecutor>(
      FLAGS_threads, std::make_shared<folly::NamedThreadFactory>(
                         "url_shortener_web_server_disk_io_thread"));
  folly::setUnsafeMutableGlobalCPUExecutor(disk_io_thread_pool);

  proxygen::HTTPServer server(std::move(options));
  server.bind(IPs);

  // Start HTTPServer mainloop in a separate thread
  std::thread t([&]() { server.start(); });
  t.join();

  return 0;
}

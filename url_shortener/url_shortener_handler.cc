#include "url_shortener_handler.h"

#include <folly/GLog.h>
#include <folly/Try.h>
#include <folly/executors/GlobalExecutor.h>
#include <folly/futures/Future.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/EventBaseManager.h>
#include <memory>
#include <proxygen/httpserver/RequestHandler.h>
#include <proxygen/httpserver/ResponseBuilder.h>
#include <string>

#include "url_shortening.h"

namespace ec_prv {
namespace url_shortener {
namespace web {

// TODO(zds): Read directly from rocksdb PinnableSlice and write the same buffer
// to the wire.
// TODO(zds): Consider adding functionality to make this a pastebin as well.

// TODO(zds): Put RocksDB operations in its own threadpool

using namespace ::ec_prv::url_shortener;

UrlRedirectHandler::UrlRedirectHandler(
    std::string &&short_url, db::ShortenedUrlsDatabase *db,
    const app_config::ReadOnlyAppConfig *const ro_app_config)
    : db_(db), ro_app_config_(ro_app_config), short_url_(std::move(short_url)) {
}

void UrlRedirectHandler::onRequest(
    std::unique_ptr<proxygen::HTTPMessage> req) noexcept {
  auto method = req->getMethod();
  if (method != proxygen::HTTPMethod::GET) {
    proxygen::ResponseBuilder(downstream_)
        .status(405, "Method Not Allowed")
        .sendWithEOM();
    return;
  }
  DLOG_IF(INFO, req->getMethod() != proxygen::HTTPMethod::GET)
      << "Request handler factory should ensure this is a GET before creating "
         "`UrlRedirectHandler`";
  DLOG_IF(INFO, short_url_ != url_shortening::parse_out_request_str(
                                  req->getPathAsStringPiece()))
      << "Incorrect `short_url` passed to `UrlRedirectHandler` constructor";
  if (short_url_.length() == 0) {
    proxygen::ResponseBuilder(downstream_)
        .status(400, "Bad Request")
        .sendWithEOM();
    return;
  }
  folly::EventBase *evb = folly::EventBaseManager::get()->getEventBase();
  buf_.reserve(max_long_url_length);
  auto f = folly::via(folly::getGlobalCPUExecutor(), [this]() mutable {
    bool ok = db_->get_fast(&buf_, short_url_);
    return ok;
  });
  std::move(f).via(evb).thenTry([this](folly::Try<bool> result) mutable {
    if (result.hasValue() && result.value()) {
      proxygen::ResponseBuilder(downstream_)
          .status(301, "Moved Permanently")
          .header(proxygen::HTTPHeaderCode::HTTP_HEADER_LOCATION,
                  std::move(buf_))
          .sendWithEOM();
    } else {
      proxygen::ResponseBuilder(downstream_)
          .status(404, "Not Found")
          .sendWithEOM();
    }
  });
}

void UrlRedirectHandler::onEOM() noexcept {
  // nop
}

void UrlRedirectHandler::onBody(
    std::unique_ptr<folly::IOBuf> /*body*/) noexcept {
  // nop
  // This optimized request handler only needs to see the headers.
}

void UrlRedirectHandler::onUpgrade(
    proxygen::UpgradeProtocol /* proto */) noexcept {
  // not applicable
}

void UrlRedirectHandler::requestComplete() noexcept { delete this; }

void UrlRedirectHandler::onError(proxygen::ProxygenError err) noexcept {
  DLOG(INFO) << "proxygen onError" << err;
  delete this;
}

} // namespace web
} // namespace url_shortener
} // namespace ec_prv

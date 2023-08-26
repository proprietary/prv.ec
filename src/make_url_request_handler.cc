#include "make_url_request_handler.h"

#include <chrono>
#include <folly/executors/GlobalExecutor.h>
#include <folly/executors/ThreadedExecutor.h>
#include <proxygen/httpserver/ResponseBuilder.h>
#include <proxygen/httpserver/RequestHandler.h>
#include <iostream>
#include <glog/logging.h>
#include <folly/json.h>
#include <folly/Range.h>
#include <folly/io/async/EventBaseManager.h>
#include <folly/io/SocketOptionMap.h>
#include <proxygen/lib/http/HTTPCommonHeaders.h>
#include <proxygen/lib/utils/URL.h>

namespace ec_prv {
namespace url_shortener {
namespace web {

MakeUrlRequestHandler::MakeUrlRequestHandler(db::ShortenedUrlsDatabase *db,
                                             folly::HHWheelTimer *timer,
                                             const uint64_t *highwayhash_key)
  :db_(db), highwayhash_key_(highwayhash_key), txn_handler_(*this), connector_(this, timer) {
  DLOG(INFO) << "created new request handler for make url";
}

void MakeUrlRequestHandler::connectSuccess(proxygen::HTTPUpstreamSession *session) {
  session_ = std::make_unique<SessionWrapper>(session);
  txn_ = session->newTransaction(&txn_handler_);
  DLOG(INFO) << "Sending http client request to: " << request_headers_to_captcha_service_->getURL();
  request_headers_to_captcha_service_->dumpMessage(google::INFO);
  CHECK(request_headers_to_captcha_service_) << "request headers to captcha service cannot be null";
  txn_->sendHeaders(*request_headers_to_captcha_service_);
  if (request_body_to_captcha_service_) {
    txn_->sendBody(std::move(request_body_to_captcha_service_));
  }
  txn_->sendEOM();
  downstream_->resumeIngress();
}

void MakeUrlRequestHandler::connectError(const folly::AsyncSocketException &ex) {
  DLOG(INFO) << "connectError";
  if (client_terminated_) {
    downstream_->sendAbort();
  } else {
    proxygen::ResponseBuilder(downstream_).status(503, "Bad Gateway").sendWithEOM();
    return;
  }
}

void MakeUrlRequestHandler::on_captcha_service_detach_transaction() noexcept {
  DLOG(INFO) << "captcha service detached";
  txn_ = nullptr;
  check_for_shutdown();
}

void MakeUrlRequestHandler::on_captcha_service_EOM(std::unique_ptr<proxygen::HTTPMessage> headers, std::unique_ptr<folly::IOBuf> body) {
  CHECK(headers) << "headers cannot be null";
  DLOG(INFO) << "received status code from captcha service: " << headers->getStatusCode();
  // CHECK(body) << "body cannot be null";
  if (headers->getStatusCode() != 200) {
    LOG(ERROR) << "Received status code " << headers->getStatusCode() << " from captcha service: " << headers->getStatusMessage();
    proxygen::ResponseBuilder(downstream_).status(500, "Internal Server Error").sendWithEOM();
    return;
  }
  auto response_body = body->coalesce();
  std::string_view response_text {reinterpret_cast<const char*>(response_body.data()), response_body.size()};
  DLOG(INFO) << "Got from captcha service: " << response_text;
  try {
    folly::dynamic response_json = folly::parseJson(response_text);
    // captcha_service_result_promise_.setValue(response_json);
    use_result_(response_json);
  } catch (...) {
    LOG(ERROR) << "Something happened.";
    proxygen::ResponseBuilder(downstream_).status(500, "Internal Server Error").sendWithEOM();
  }
}

void MakeUrlRequestHandler::on_captcha_service_error(const proxygen::HTTPException &err) noexcept {
  LOG(ERROR) << "captcha service error: " << err.what();
  proxygen::ResponseBuilder(downstream_).status(500, "Internal Server Error").sendWithEOM();
}

void MakeUrlRequestHandler::onRequest(std::unique_ptr<proxygen::HTTPMessage> request) noexcept {
  request_ = std::move(request);
}

void MakeUrlRequestHandler::onBody(std::unique_ptr<folly::IOBuf> body) noexcept {
  if (body_) {
    body_->appendToChain(std::move(body));
  } else {
    body_ = std::move(body);
  }
}

void MakeUrlRequestHandler::onEOM() noexcept {
  CHECK(!client_terminated_);
  // TODO: make request to captcha service
  folly::SocketAddress addr;
  proxygen::URL external_service_url{ "http://api.ipify.org/?format=json" /*"http://checkip.amazonaws.com/"*/};
   // TODO(zds): this is a synchronous DNS lookup! bad in an event loop!
  addr.setFromHostPort(external_service_url.getHost(), external_service_url.getPort());
  request_headers_to_captcha_service_ = std::make_unique<proxygen::HTTPMessage>();
  request_headers_to_captcha_service_->setURL(external_service_url.getPath());
  request_headers_to_captcha_service_->setQueryString(external_service_url.getQuery());
  request_headers_to_captcha_service_->setMethod(proxygen::HTTPMethod::GET);
  request_headers_to_captcha_service_->getHeaders().add(proxygen::HTTPHeaderCode::HTTP_HEADER_ORIGIN, "localhost");
  request_headers_to_captcha_service_->getHeaders().add(proxygen::HTTPHeaderCode::HTTP_HEADER_HOST, external_service_url.getHost());
  request_headers_to_captcha_service_->getHeaders().add(proxygen::HTTPHeaderCode::HTTP_HEADER_ACCEPT, "*/*");
  request_headers_to_captcha_service_->getHeaders().add(proxygen::HTTPHeaderCode::HTTP_HEADER_USER_AGENT, "curl/7.74.0");
  request_headers_to_captcha_service_->setHTTPVersion(1, 1);
  auto evb = folly::EventBaseManager::get()->getEventBase();
  // TODO(zds): use a connection pool here
  const folly::SocketOptionMap opts { {{SOL_SOCKET, SO_REUSEADDR}, 1 }};
  // downstream_->pauseIngress();
  // if (url.isSecure()) {
  //   auto sslContext_ = std::make_shared<folly::SSLContext>();
  //   sslContext_->setOptions(SSL_OP_NO_COMPRESSION);
  //   sslContext_->setCipherList(folly::ssl::SSLCommonOptions::kCipherList);
  //   _connector.connectSSL(evb, addr, sslContext_, nullptr, std::chrono::milliseconds(1000), opts);
  // }
  connector_.connect(evb, addr, std::chrono::milliseconds(1000), opts);
  use_result_ = [this](folly::dynamic &result) mutable {
    DLOG(INFO) << "in callback with result";
    DLOG(INFO) << "result: " << folly::toPrettyJson(result);
    //downstream_->resumeIngress();
    proxygen::ResponseBuilder(downstream_).status(200, "OK").body(result["ip"].asString()).sendWithEOM();
  };
  // TODO: parse input `long_url`
  // TODO: shorten input `long_url`; put it into database
}

void MakeUrlRequestHandler::requestComplete() noexcept {
  client_terminated_ = true;
  delete this;
}

void MakeUrlRequestHandler::onError(proxygen::ProxygenError err) noexcept {
  DLOG(INFO) << "proxygen error: " << err;
  client_terminated_ = true;
  delete this;
}

bool MakeUrlRequestHandler::check_for_shutdown() noexcept { return true; }

void MakeUrlRequestHandler::abort_downstream() noexcept {
  if (!client_terminated_) {
    downstream_->sendAbort();
  }
}


} // namespace web
} // namespace url_shortener
} // namespace ec_prv

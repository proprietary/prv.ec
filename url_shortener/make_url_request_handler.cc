#include "make_url_request_handler.h"

#include <chrono>
#include <folly/Format.h>
#include <folly/Range.h>
#include <folly/String.h>
#include <folly/Try.h>
#include <folly/executors/GlobalExecutor.h>
#include <folly/executors/ThreadedExecutor.h>
#include <folly/futures/Future.h>
#include <folly/io/SocketOptionMap.h>
#include <folly/io/async/EventBaseManager.h>
#include <folly/io/async/SSLContext.h>
#include <folly/io/async/SSLOptions.h>
#include <folly/json.h>
#include <glog/logging.h>
#include <iostream>
#include <proxygen/httpserver/RequestHandler.h>
#include <proxygen/httpserver/ResponseBuilder.h>
#include <proxygen/lib/http/HTTPCommonHeaders.h>
#include <proxygen/lib/http/HTTPMethod.h>
#include <proxygen/lib/http/session/HTTPUpstreamSession.h>
#include <proxygen/lib/utils/URL.h>
#include <variant>

#include "url_shortener/db.h"
#include "url_shortening.h"

namespace ec_prv {
namespace url_shortener {
namespace web {

MakeUrlRequestHandler::MakeUrlRequestHandler(
    db::ShortenedUrlsDatabase *db, folly::HHWheelTimer *timer,
    const app_config::ReadOnlyAppConfig *const ro_app_config,
    const ::ec_prv::url_shortener::url_shortening::UrlShorteningConfig
        *const url_shortening_svc)
    : db_(db), ro_app_config_(ro_app_config), txn_handler_(*this),
      connector_(this, timer), url_shortening_svc_(url_shortening_svc) {
  DLOG(INFO) << "created new request handler for make url";
}

auto MakeUrlRequestHandler::query_captcha_service(
    const std::string &captcha_user_response) -> folly::Future<std::string> {
  // connect
  folly::SocketAddress addr;
  proxygen::URL external_service_url{
      "https://www.google.com/recaptcha/api/siteverify"};
  // proxygen::URL external_service_url{"https://api.ipify.org/?format=json"};
  // TODO(zds): this is a synchronous DNS lookup! bad in an event loop!
  addr.setFromHostPort(external_service_url.getHost(),
                       external_service_url.getPort());
  request_headers_to_captcha_service_ =
      std::make_unique<proxygen::HTTPMessage>();
  request_headers_to_captcha_service_->setURL(external_service_url.getPath());
  request_headers_to_captcha_service_->setQueryString(
      external_service_url.getQuery());
  request_headers_to_captcha_service_->setMethod(proxygen::HTTPMethod::POST);
  request_headers_to_captcha_service_->getHeaders().add(
      proxygen::HTTPHeaderCode::HTTP_HEADER_ORIGIN,
      /*ro_app_config_->url_shortener_service_base_url*/ "localhost");
  request_headers_to_captcha_service_->getHeaders().add(
      proxygen::HTTPHeaderCode::HTTP_HEADER_HOST,
      external_service_url.getHost());
  request_headers_to_captcha_service_->getHeaders().add(
      proxygen::HTTPHeaderCode::HTTP_HEADER_ACCEPT, "application/json");
  request_headers_to_captcha_service_->getHeaders().add(proxygen::HTTPHeaderCode::HTTP_HEADER_USER_AGENT,
  ro_app_config_->server_user_agent);
  request_headers_to_captcha_service_->getHeaders().add(
      proxygen::HTTPHeaderCode::HTTP_HEADER_CONTENT_TYPE,
      "application/x-www-form-urlencoded");
  request_headers_to_captcha_service_->setHTTPVersion(1, 1);

  auto secret_encoded = folly::uriEscape<std::string>(
      folly::StringPiece{ro_app_config_->captcha_service_api_key});
  auto response_encoded =
      folly::uriEscape<std::string>(folly::StringPiece{captcha_user_response});
  std::string captcha_service_request_body =
      folly::sformat("secret={}&response={}", secret_encoded, response_encoded);
  DLOG(INFO) << "recaptcha request body: " << captcha_service_request_body;
  request_body_to_captcha_service_ = folly::IOBuf::copyBuffer(
      captcha_service_request_body.data(), captcha_service_request_body.size());
  request_headers_to_captcha_service_->getHeaders().add(
      proxygen::HTTPHeaderCode::HTTP_HEADER_CONTENT_LENGTH,
      std::to_string(captcha_service_request_body.size()));

  auto evb = folly::EventBaseManager::get()->getEventBase();
  // TODO(zds): use a connection pool here
  const folly::SocketOptionMap opts{{{SOL_SOCKET, SO_REUSEADDR}, 1}};
  if (external_service_url.isSecure()) {
    ssl_context_ = std::make_shared<folly::SSLContext>();
    ssl_context_->setOptions(SSL_OP_NO_COMPRESSION);
    folly::ssl::setCipherSuites<folly::ssl::SSLCommonOptions>(
        *ssl_context_.get());
    ssl_context_->loadTrustedCertificates(
        ro_app_config_->trusted_certificates_path.c_str());
    // to do client certs:
    // ssl_context_->loadCertKeyPairFromFiles(cert_path, key_path);
    std::list<std::string> next_proto_list;
    // TODO(zds): do not split this string at runtime
    folly::splitTo<std::string>(
        ',', "h2,h2-14,spdy/3.1,spdy/3,http/1.1",
        std::inserter(next_proto_list, next_proto_list.begin()));
    ssl_context_->setAdvertisedNextProtocols(next_proto_list);
    downstream_->pauseIngress();
    connector_.connectSSL(
        evb, addr, ssl_context_, nullptr, std::chrono::milliseconds(1000), opts,
        folly::AsyncSocket::anyAddress(), external_service_url.getHost());
  } else {
    downstream_->pauseIngress();
    connector_.connect(evb, addr, std::chrono::milliseconds(1000), opts);
  }
  auto f = captcha_service_result_promise_.getFuture();
  return f;
}

void MakeUrlRequestHandler::connectSuccess(
    proxygen::HTTPUpstreamSession *session) {
  txn_ = session->newTransaction(&txn_handler_);
  DLOG(INFO) << "Sending http client request to: "
             << request_headers_to_captcha_service_->getURL();
  CHECK(request_headers_to_captcha_service_)
      << "request headers to captcha service cannot be null";
  txn_->sendHeaders(*request_headers_to_captcha_service_);
  request_headers_to_captcha_service_->dumpMessage(google::INFO);
  if (request_body_to_captcha_service_) {
    txn_->sendBody(std::move(request_body_to_captcha_service_));
  }
  txn_->sendEOM();
  downstream_->resumeIngress();
}

void MakeUrlRequestHandler::connectError(
    const folly::AsyncSocketException &ex) {
  DLOG(INFO) << "connectError: " << ex;
  // captcha_service_result_promise_.setException(std::runtime_error{"connectError"});
  if (client_terminated_) {
    downstream_->sendAbort();
  } else {
    proxygen::ResponseBuilder(downstream_)
        .status(503, "Bad Gateway")
        .sendWithEOM();
    return;
  }
}

void MakeUrlRequestHandler::on_captcha_service_detach_transaction() noexcept {
  DLOG(INFO) << "captcha service detached";
  txn_ = nullptr;
  check_for_shutdown();
}

void MakeUrlRequestHandler::on_captcha_service_EOM(
    std::unique_ptr<proxygen::HTTPMessage> headers,
    std::unique_ptr<folly::IOBuf> body) {
  CHECK(headers) << "headers cannot be null";
  DLOG(INFO) << "received status code from captcha service: "
             << headers->getStatusCode();
  if (headers->getStatusCode() != 200) {
    LOG(ERROR) << "Received status code " << headers->getStatusCode()
               << " from captcha service: " << headers->getStatusMessage();
    captcha_service_result_promise_.setException(std::runtime_error{
        folly::sformat("received status code: {}", headers->getStatusCode())});
    return;
  }
  if (!body) {
    LOG(ERROR) << "Received empty body from captcha service";
    captcha_service_result_promise_.setException(
        std::runtime_error{"body missing"});
    return;
  }
  auto response_body = body->coalesce();
  std::string_view response_text{
      reinterpret_cast<const char *>(response_body.data()),
      response_body.size()};
  DLOG(INFO) << "Got from captcha service: " << response_text;
  captcha_service_result_promise_.setValue(std::string(response_text));
}

void MakeUrlRequestHandler::on_captcha_service_error(
    const proxygen::HTTPException &err) noexcept {
  LOG(ERROR) << "captcha service error: " << err.describe();
  captcha_service_result_promise_.setException(
      std::runtime_error{err.describe()});
}

void MakeUrlRequestHandler::onRequest(
    std::unique_ptr<proxygen::HTTPMessage> request) noexcept {
  DLOG(INFO) << "make url request: onRequest";
  headers_ = std::move(request);
  auto method = headers_->getMethod();
  if (method != proxygen::HTTPMethod::POST &&
      method != proxygen::HTTPMethod::PUT) {
    proxygen::ResponseBuilder(downstream_)
        .status(405, "Method Not Allowed")
        .sendWithEOM();
    return;
  }
}

void MakeUrlRequestHandler::onBody(
    std::unique_ptr<folly::IOBuf> body) noexcept {
  DLOG(INFO) << "receive body";
  if (body_) {
    body_->appendToChain(std::move(body));
  } else {
    body_ = std::move(body);
  }
}

auto MakeUrlRequestHandler::do_shorten_url(const std::string &long_url)
    -> std::string {
  std::string generated_short_url;
  // keep generating slugs until it doesn't collide with previous entries
  uint8_t nth_try = 1;
  constexpr uint8_t max_tries = 100; // don't hang forever
  while (nth_try++ < max_tries) {
    bool ok = url_shortening_svc_->generate_slug(generated_short_url, long_url,
                                                 nth_try);
    LOG_IF(ERROR, !ok) << "what should be a very rare error: ran out of hashes "
                          "in creating slug for long_url=\""
                       << long_url << "\"";
    if (!ok) {
      // TODO(zds): just default to creating a random string
      return {};
    }
    auto got = db_->get(generated_short_url);
    if (std::holds_alternative<db::UrlShorteningDbError>(got)) {
      auto err = std::get_if<db::UrlShorteningDbError>(&got);
      if (*err == db::UrlShorteningDbError::NotFound) {
        // not found in database; this generated slug works
        break;
      } else {
        LOG(ERROR) << "database failure";
        return {};
      }
    }
    auto got_str = std::get_if<std::string>(&got);
    if (*got_str == "") {
      // not found in database; this generated slug works
      break;
    } else if (*got_str == long_url) {
      // already in database
      // no need to re-insert it
      return generated_short_url;
    }
    LOG(WARNING) << "slug generated for \"" << long_url << "\": \""
                 << generated_short_url
                 << "\" collides with the slug for an existing entry: \""
                 << *got_str
                 << "\". Consider increasing size of the alphabet used (in the "
                    "app configuration).";
  }
  auto err = db_->put(generated_short_url, long_url);
  if (err) {
    LOG(ERROR) << "rocksdb errored during insert of: long_url=\"" << long_url
               << "\", generated_short_url=\"" << generated_short_url << "\"";
    return {};
  }
  return generated_short_url;
}

void MakeUrlRequestHandler::onEOM() noexcept {
  if (client_terminated_) {
    DLOG(ERROR) << "client terminated; no need to create requested URL";
    return;
  }
  if (!headers_) {
    DLOG(ERROR) << "Missing headers; should not happen";
    proxygen::ResponseBuilder(downstream_)
        .status(400, "Bad Request")
        .sendWithEOM();
  }
  DLOG(INFO) << "make url request handler EOM";
  // // parse input
  // std::string user_captcha_response =
  //     headers_->getQueryParam("user_captcha_response");
  // std::string long_url = headers_->getQueryParam("long_url");

  // parse input from request JSON
  DLOG_IF(WARNING, !body_) << "Body missing";
  std::string user_captcha_response, long_url;
  if (body_) {
    auto body_bytes = body_->coalesce();
    std::string_view body_str{reinterpret_cast<const char *>(body_bytes.data()),
                              body_bytes.size()};
    auto body_json = folly::parseJson(body_str);
    if (body_json.isObject() && !body_json["user_captcha_response"].isNull() &&
        !body_json["long_url"].isNull()) {
      user_captcha_response = body_json["user_captcha_response"].asString();
      long_url = body_json["long_url"].asString();
    }
  } else {
    proxygen::ResponseBuilder(downstream_)
        .status(400, "Bad Request")
        .sendWithEOM();
    return;
  }
  if (user_captcha_response.length() == 0) {
    DLOG(INFO) << "`user_captcha_response` parameter missing";
    proxygen::ResponseBuilder(downstream_)
        .status(400, "Bad Request")
        .sendWithEOM();
    return;
  }
  if (long_url.length() == 0) {
    DLOG(INFO) << "`long_url` parameter missing";
    proxygen::ResponseBuilder(downstream_)
        .status(400, "Bad Request")
        .sendWithEOM();
    return;
  }
  folly::EventBase *evb = folly::EventBaseManager::get()->getEventBase();
  // Make sure not to close the connection (deleting `this`) while we still need
  // `this` in this promise/future
  query_captcha_service(user_captcha_response)
      .thenValue([](std::string result) {
        DLOG(INFO) << "in promise thenValue";
        auto json = folly::parseJson(result);
        DLOG(INFO) << "parsed json in future";
        if (json.isObject() && !json["success"].isNull()) {
          DLOG(INFO) << "parsed result from captcha service";
          return json["success"].asBool();
        } else {
          DLOG(ERROR) << "failed to parse JSON result from captcha service, "
                         "but got this string from the captcha service: "
                      << result;
          throw std::runtime_error{"json parse fail"};
        }
        return false;
      })
      .via(folly::getGlobalCPUExecutor())
      .thenValue([this, long_url](bool success) mutable {
        if (success) {
          return do_shorten_url(long_url);
        }
        return std::string{};
      })
      .via(evb)
      .thenValue([this](std::string &&short_url) mutable {
        if (short_url.empty()) {
          DLOG(INFO) << "did not create new short url";
          proxygen::ResponseBuilder(downstream_)
              .status(400, "Bad Request")
              .header(proxygen::HTTPHeaderCode::
                          HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN,
                      "*")
              .sendWithEOM();
        } else {
          DLOG(INFO) << "created short url slug: " << short_url;
          proxygen::ResponseBuilder(downstream_)
              .status(200, "OK")
              .header(proxygen::HTTPHeaderCode::
                          HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN,
                      "*")
              .body(std::move(short_url))
              .sendWithEOM();
        }
      })
      .thenError([this](folly::exception_wrapper &&e) mutable {
        DLOG(ERROR) << "error in captcha service: "
                    << e.get_exception()->what();
        proxygen::ResponseBuilder(downstream_)
            .status(500, "Internal Server Error")
            .header(proxygen::HTTPHeaderCode::
                        HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN,
                    "*")
            .sendWithEOM();
      });
}

void MakeUrlRequestHandler::requestComplete() noexcept {
  client_terminated_ = true;
  delete this;
}

void MakeUrlRequestHandler::onError(proxygen::ProxygenError err) noexcept {
  DLOG(INFO) << "proxygen error: " << err;
  client_terminated_ = true;
  captcha_service_result_promise_.setException(
      std::runtime_error{"proxygenError"});
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

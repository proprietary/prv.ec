#include "make_url_request_handler.h"

#include <chrono>
#include <folly/Try.h>
#include <folly/Range.h>
#include <folly/String.h>
#include <folly/Format.h>
#include <folly/executors/GlobalExecutor.h>
#include <folly/executors/ThreadedExecutor.h>
#include <folly/futures/Future.h>
#include <proxygen/httpserver/ResponseBuilder.h>
#include <proxygen/httpserver/RequestHandler.h>
#include <iostream>
#include <glog/logging.h>
#include <folly/json.h>
#include <folly/Range.h>
#include <folly/io/async/EventBaseManager.h>
#include <folly/io/SocketOptionMap.h>
#include <folly/io/async/SSLContext.h>
#include <folly/io/async/SSLOptions.h>
#include <proxygen/lib/http/HTTPCommonHeaders.h>
#include <proxygen/lib/http/session/HTTPUpstreamSession.h>
#include <proxygen/lib/utils/URL.h>

#include "url_shortening.h"

namespace ec_prv {
namespace url_shortener {
namespace web {

MakeUrlRequestHandler::MakeUrlRequestHandler(db::ShortenedUrlsDatabase *db,
                                             folly::HHWheelTimer *timer,
					     const app_config::ReadOnlyAppConfig *const ro_app_config)
  :db_(db), ro_app_config_(ro_app_config), txn_handler_(*this), connector_(this, timer) {
  DLOG(INFO) << "created new request handler for make url";
}

auto MakeUrlRequestHandler::query_captcha_service(const std::string &captcha_user_response) -> folly::Future<std::string> {
  // connect
  folly::SocketAddress addr;
  // proxygen::URL external_service_url{"https://www.google.com/recaptcha/api/siteverify"};
  proxygen::URL external_service_url{"https://api.ipify.org/?format=json"};
   // TODO(zds): this is a synchronous DNS lookup! bad in an event loop!
  addr.setFromHostPort(external_service_url.getHost(), external_service_url.getPort());
  request_headers_to_captcha_service_ = std::make_unique<proxygen::HTTPMessage>();
  request_headers_to_captcha_service_->setURL(external_service_url.getPath());
  request_headers_to_captcha_service_->setQueryString(external_service_url.getQuery());
  request_headers_to_captcha_service_->setMethod(proxygen::HTTPMethod::GET);
  request_headers_to_captcha_service_->getHeaders().add(proxygen::HTTPHeaderCode::HTTP_HEADER_ORIGIN, ro_app_config_->url_shortener_service_base_url);
  request_headers_to_captcha_service_->getHeaders().add(proxygen::HTTPHeaderCode::HTTP_HEADER_HOST, external_service_url.getHost());
  request_headers_to_captcha_service_->getHeaders().add(proxygen::HTTPHeaderCode::HTTP_HEADER_ACCEPT, "application/json");
  request_headers_to_captcha_service_->getHeaders().add(proxygen::HTTPHeaderCode::HTTP_HEADER_USER_AGENT, ro_app_config_->server_user_agent);
  request_headers_to_captcha_service_->getHeaders().add(proxygen::HTTPHeaderCode::HTTP_HEADER_CONTENT_TYPE, "application/x-www-form-urlencoded");
  request_headers_to_captcha_service_->setHTTPVersion(1, 1);
  // See: https://developers.google.com/recaptcha/docs/verify
  // std::string captcha_service_request_body = folly::sformat("secret={}&response={}",
  // 							    ro_app_config_->captcha_service_api_key,
  // 							    captcha_user_response);
  // request_body_to_captcha_service_ = folly::IOBuf::copyBuffer(captcha_service_request_body.data(), captcha_service_request_body.size());
  auto evb = folly::EventBaseManager::get()->getEventBase();
  // TODO(zds): use a connection pool here
  const folly::SocketOptionMap opts { {{SOL_SOCKET, SO_REUSEADDR}, 1 }};
  if (external_service_url.isSecure()) {
    ssl_context_ = std::make_shared<folly::SSLContext>();
    ssl_context_->setOptions(SSL_OP_NO_COMPRESSION);
    folly::ssl::setCipherSuites<folly::ssl::SSLCommonOptions>(*ssl_context_.get());
    ssl_context_->loadTrustedCertificates(ro_app_config_->trusted_certificates_path.c_str());
    // ssl_context_->loadCertKeyPairFromFiles(cert_path, key_path);
    std::list<std::string> next_proto_list;
    folly::splitTo<std::string>(',', "h2,h2-14,spdy/3.1,spdy/3,http/1.1", std::inserter(next_proto_list, next_proto_list.begin()));
    ssl_context_->setAdvertisedNextProtocols(next_proto_list);
    downstream_->pauseIngress();
    connector_.connectSSL(evb, addr, ssl_context_, nullptr, std::chrono::milliseconds(1000), opts, folly::AsyncSocket::anyAddress(), external_service_url.getHost());
  } else {
    downstream_->pauseIngress();
    connector_.connect(evb, addr, std::chrono::milliseconds(1000), opts);
  }
  auto f = captcha_service_result_promise_.getFuture();
  return f;
}

void MakeUrlRequestHandler::connectSuccess(proxygen::HTTPUpstreamSession *session) {
  txn_ = session->newTransaction(&txn_handler_);
  DLOG(INFO) << "Sending http client request to: " << request_headers_to_captcha_service_->getURL();
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
    captcha_service_result_promise_.setValue(std::string(response_text));
    // use_result_(response_json);
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
  DLOG(INFO) << "make url request: onRequest";
  headers_ = std::move(request);
}

void MakeUrlRequestHandler::onBody(std::unique_ptr<folly::IOBuf> body) noexcept {
  if (body_) {
    body_->appendToChain(std::move(body));
  } else {
    body_ = std::move(body);
  }
}

auto MakeUrlRequestHandler::do_shorten_url(const std::string& long_url) -> std::string {
  auto generated_short_url = url_shortening::generate_shortened_url(long_url, ro_app_config_->highwayhash_key);
  // TODO(zds): check and handle eerrors from database
  db_->put(generated_short_url, long_url);
  return generated_short_url;
}

void MakeUrlRequestHandler::onEOM() noexcept {
  CHECK(!client_terminated_);
  // parse input
  std::string user_captcha_response = headers_->getQueryParam("user_captcha_response");
  std::string long_url = headers_->getQueryParam("long_url");
  if (user_captcha_response.length() == 0) {
    DLOG(INFO) << "`user_captcha_response` parameter missing";
    proxygen::ResponseBuilder(downstream_).status(400, "Bad Request").sendWithEOM();
    return;
  }
  if (long_url.length() == 0) {
    DLOG(INFO) << "`long_url` parameter missing";
    proxygen::ResponseBuilder(downstream_).status(400, "Bad Request").sendWithEOM();
    return;
  }
  folly::EventBase *evb = folly::EventBaseManager::get()->getEventBase();
  query_captcha_service(user_captcha_response).thenValue([this, evb, long_url](std::string result) mutable {
    DLOG(INFO) << "in callback with result";
    DLOG(INFO) << "result: " << result;
    // verify captcha
    auto json = folly::parseJson(result);
    if (json["ip"].asString().length() > 0 /* json["success"].asBool() */) {
      // pass
      // now insert input into database
      return folly::via(folly::getGlobalCPUExecutor(), std::bind(&MakeUrlRequestHandler::do_shorten_url, this, long_url));
    } else {
      evb->runInEventBaseThread([this]() mutable {
	proxygen::ResponseBuilder(downstream_).status(400, "Bad Request").sendWithEOM();
      });
      throw std::runtime_error{"bad request"};
    }
  }).thenTry([this, evb](folly::Try<std::string> result) mutable {
    if (result.hasValue()) {
      auto short_url = result.value();
      evb->runInEventBaseThread([this, short_url]() mutable {
	proxygen::ResponseBuilder(downstream_).status(200, "OK").body(std::move(short_url)).sendWithEOM();
      });
      return;
    }
    if (result.hasException()) {
      DLOG(INFO) << result.exception().what();
    }
    evb->runInEventBaseThread([this]() mutable {
      proxygen::ResponseBuilder(downstream_).status(500, "Internal Server Error").sendWithEOM();
    });
  });
  // // TODO: make request to captcha service
  // folly::SocketAddress addr;
  // proxygen::URL external_service_url{"https://www.google.com/recaptcha/api/siteverify"};
  //  // TODO(zds): this is a synchronous DNS lookup! bad in an event loop!
  // addr.setFromHostPort(external_service_url.getHost(), external_service_url.getPort());
  // request_headers_to_captcha_service_ = std::make_unique<proxygen::HTTPMessage>();
  // request_headers_to_captcha_service_->setURL(external_service_url.getPath());
  // request_headers_to_captcha_service_->setQueryString(external_service_url.getQuery());
  // request_headers_to_captcha_service_->setMethod(proxygen::HTTPMethod::GET);
  // request_headers_to_captcha_service_->getHeaders().add(proxygen::HTTPHeaderCode::HTTP_HEADER_ORIGIN, ro_app_config_->url_shortener_service_base_url);
  // request_headers_to_captcha_service_->getHeaders().add(proxygen::HTTPHeaderCode::HTTP_HEADER_HOST, external_service_url.getHost());
  // request_headers_to_captcha_service_->getHeaders().add(proxygen::HTTPHeaderCode::HTTP_HEADER_ACCEPT, "application/json");
  // request_headers_to_captcha_service_->getHeaders().add(proxygen::HTTPHeaderCode::HTTP_HEADER_USER_AGENT, ro_app_config_->server_user_agent);
  // request_headers_to_captcha_service_->getHeaders().add(proxygen::HTTPHeaderCode::HTTP_HEADER_CONTENT_TYPE, "application/x-www-form-urlencoded");
  // request_headers_to_captcha_service_->setHTTPVersion(1, 1);
  // // See: https://developers.google.com/recaptcha/docs/verify
  // std::string captcha_service_request_body = folly::sformat("secret={}&response={}",
  // 							    folly::uriEscape(ro_app_config_->captcha_service_api_key),
  // 							    folly::uriEscape(user_captcha_response));
  // request_body_to_captcha_service_ = folly::IOBuf::copyBuffer(captcha_service_request_body.data(), captcha_service_request_body.size());
  // auto evb = folly::EventBaseManager::get()->getEventBase();
  // // TODO(zds): use a connection pool here
  // const folly::SocketOptionMap opts { {{SOL_SOCKET, SO_REUSEADDR}, 1 }};
  // // downstream_->pauseIngress();
  // if (external_service_url.isSecure()) {
  //   ssl_context_ = std::make_shared<folly::SSLContext>();
  //   ssl_context_->setOptions(SSL_OP_NO_COMPRESSION);
  //   folly::ssl::setCipherSuites<folly::ssl::SSLCommonOptions>(*ssl_context_.get());
  //   ssl_context_->loadTrustedCertificates(ro_app_config_->trusted_certificates_path);
  //   // ssl_context_->loadCertKeyPairFromFiles(cert_path, key_path);
  //   std::list<std::string> next_proto_list;
  //   folly::splitTo<std::string>(',', "h2,h2-14,spdy/3.1,spdy/3,http/1.1", std::inserter(next_proto_list, next_proto_list.begin()));
  //   ssl_context_->setAdvertisedNextProtocols(next_proto_list);
  //   connector_.connectSSL(evb, addr, ssl_context_, nullptr, std::chrono::milliseconds(1000), opts, folly::AsyncSocket::anyAddress(), external_service_url.getHost());
  // } else {
  //   connector_.connect(evb, addr, std::chrono::milliseconds(1000), opts);
  // }
  // use_result_ = [this, long_url](folly::dynamic &result) mutable {
  //   DLOG(INFO) << "in callback with result";
  //   DLOG(INFO) << "result: " << folly::toPrettyJson(result);
  //   //downstream_->resumeIngress();

  //   // verify captcha
  //   if (result["success"] && result["success"].asBool()) {
  //     // pass
  //     // now insert input into database
  //     // shorten input `long_url`
  //     folly::EventBase *evb = folly::EventBaseManager::get()->getEventBase();
  //     folly::Executor::KeepAlive<> executor = folly::getGlobalCPUExecutor();
  //     auto [p, f] = folly::makePromiseContract(executor);
  //     folly::Promise<std::string> db_put_result;
  //     folly::SemiFuture<std::string> fut = db_put_result.getSemiFuture().via(executor);
  //     std::move(f).thenValue([this, evb](std::string &&_short_url) mutable {
  // 	evb->runInEventBaseThread([this, short_url = std::move(_short_url)]() mutable {
  // 	  proxygen::ResponseBuilder(downstream_).status(200, "OK").body(std::move(short_url)).sendWithEOM();
  // 	});
  //     });
  //     // put shortened url into database in thread pool, so as to not block the event loop
  //     executor->add([this, long_url, db_put_promise = std::move(db_put_result)]() mutable {
  // 	auto generated_short_url = url_shortening::generate_shortened_url(long_url, ro_app_config_->highwayhash_key);
  //       db_->put(generated_short_url, long_url);
  // 	// TODO(zds): check and handle errors
  // 	db_put_promise.setValue(generated_short_url);
  //     });
  //   } else {
  //     proxygen::ResponseBuilder(downstream_).status(400, "Bad Request").sendWithEOM();
  //   }
  // };
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

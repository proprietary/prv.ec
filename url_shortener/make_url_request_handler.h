#ifndef _INCLUDE_EC_PRV_URL_SHORTENER_WEB_MAKE_URL_REQUEST_HANDLER_H
#define _INCLUDE_EC_PRV_URL_SHORTENER_WEB_MAKE_URL_REQUEST_HANDLER_H

#include <folly/Memory.h>
#include <folly/dynamic.h>
#include <folly/futures/Future.h>
#include <folly/io/IOBuf.h>
#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/DelayedDestruction.h>
#include <folly/io/async/HHWheelTimer.h>
#include <folly/io/async/SSLContext.h>
#include <folly/json.h>
#include <memory>
#include <proxygen/httpserver/RequestHandler.h>
#include <proxygen/httpserver/ResponseHandler.h>
#include <proxygen/lib/http/HTTPConnector.h>
#include <string>
#include <string_view>

#include "app_config.h"
#include "db.h"
#include "url_shortening.h"

namespace ec_prv {
namespace url_shortener {
namespace web {
class MakeUrlRequestHandler : public proxygen::RequestHandler,
                              public proxygen::HTTPConnector::Callback {
public:
  explicit MakeUrlRequestHandler(
      ::ec_prv::url_shortener::db::ShortenedUrlsDatabase *db,
      folly::HHWheelTimer *timer,
      const app_config::ReadOnlyAppConfig *const ro_app_config,
      const ::ec_prv::url_shortener::url_shortening::UrlShorteningConfig
          *const url_shortening_svc_);

  // RequestHandler methods
  void
  onRequest(std::unique_ptr<proxygen::HTTPMessage> request) noexcept override;

  void onBody(std::unique_ptr<folly::IOBuf> body) noexcept override;

  void onEOM() noexcept override;

  void onUpgrade(proxygen::UpgradeProtocol proto) noexcept override {}

  void requestComplete() noexcept override;

  void onError(proxygen::ProxygenError err) noexcept override;

  // HTTPConnector::Callback methods

  void connectSuccess(proxygen::HTTPUpstreamSession *session);
  void connectError(const folly::AsyncSocketException &ex);

private:
  // high level
  auto query_captcha_service(const std::string &captcha_user_response)
      -> folly::Future<std::string>;

  auto do_shorten_url(const std::string &long_url) -> std::string;

  // Internal communication with captcha service

  class CaptchaServiceHTTPConnectorCallback
      : public proxygen::HTTPConnector::Callback {
  public:
    explicit CaptchaServiceHTTPConnectorCallback(
        std::unique_ptr<proxygen::HTTPMessage> headers,
        std::unique_ptr<folly::IOBuf> body);
    void connectSuccess(proxygen::HTTPUpstreamSession *);
    void connectError(const folly::AsyncSocketException &);

  private:
    std::unique_ptr<proxygen::HTTPMessage> headers_;
    std::unique_ptr<folly::IOBuf> body_;
  };

  void on_captcha_service_detach_transaction() noexcept;

  void on_captcha_service_EOM(std::unique_ptr<proxygen::HTTPMessage> headers,
                              std::unique_ptr<folly::IOBuf> body);

  void on_captcha_service_error(const proxygen::HTTPException &error) noexcept;

  // Used for embedding an http client calls in the request
  // handlers. During handling of an API route, the server needs to
  // make a web request to reCAPTCHA.
  class CaptchaServiceHTTPTransactionHandler
      : public proxygen::HTTPTransactionHandler {
  public:
    explicit CaptchaServiceHTTPTransactionHandler(MakeUrlRequestHandler &parent)
        : parent_{parent} {}
    void setTransaction(proxygen::HTTPTransaction *txn) noexcept override {
      txn_ = txn;
      cancelled_ = false;
    }
    void detachTransaction() noexcept override {
      DLOG(INFO) << "captcha service transaction cancelled";
      cancelled_ = true;
      txn_ = nullptr;
      parent_.on_captcha_service_detach_transaction();
    }
    void onHeadersComplete(
        std::unique_ptr<proxygen::HTTPMessage> msg) noexcept override {
      DLOG(INFO) << "captcha service onHeadersComplete";
      DLOG(INFO) << "captcha service headers responded with status code "
                 << msg->getStatusCode();
      // parent_.onCaptchaServiceHeadersComplete(std::move(msg));
      headers_ = std::move(msg);
    }
    void onBody(std::unique_ptr<folly::IOBuf> chain) noexcept override {
      CHECK(!cancelled_);
      DLOG(INFO) << "captcha service onBody: "
                 << std::string_view{
                        reinterpret_cast<const char *>(chain->data()),
                        chain->length()};
      // parent_.onCaptchaServiceBody(std::move(chain));
      if (body_) {
        body_->appendToChain(std::move(chain));
      } else {
        body_ = std::move(chain);
      }
    }
    void onTrailers(std::unique_ptr<proxygen::HTTPHeaders>) noexcept {}
    void onEOM() noexcept override {
      DLOG(INFO) << "captcha service EOM";
      if (!cancelled_) {
        parent_.on_captcha_service_EOM(std::move(headers_), std::move(body_));
      }
    }
    void onUpgrade(proxygen::UpgradeProtocol) noexcept override {}
    void onError(const proxygen::HTTPException &err) noexcept override {
      LOG(ERROR) << "captcha service errored: " << err;
      cancelled_ = true;
      parent_.on_captcha_service_error(err);
    }

    /**
     * If the remote side's receive buffer fills up, this callback will be
     * invoked so you can attempt to stop sending to the remote side.
     */
    void onEgressPaused() noexcept override {
      DLOG(INFO) << "captcha service egress paused";
    }

    /**
     * This callback lets you know that the remote side has resumed reading
     * and you can now continue to send data.
     */
    void onEgressResumed() noexcept override {
      DLOG(INFO) << "captcha service egress resumed";
    }

  private:
    MakeUrlRequestHandler &parent_;
    bool cancelled_{false};
    std::unique_ptr<proxygen::HTTPMessage> headers_{nullptr};
    std::unique_ptr<folly::IOBuf> body_{nullptr};
    proxygen::HTTPTransaction *txn_;
  };

  // void sendError(const std::string &what) noexcept;
  // void sendErrorBadRequest(const std::string &what) noexcept;
  bool check_for_shutdown() noexcept;
  void abort_downstream() noexcept;

  ::ec_prv::url_shortener::db::ShortenedUrlsDatabase *const db_;
  const ::ec_prv::url_shortener::app_config::ReadOnlyAppConfig
      *const ro_app_config_;
  const ::ec_prv::url_shortener::url_shortening::UrlShorteningConfig
      *const url_shortening_svc_;
  std::string_view captcha_service_api_key_;
  std::unique_ptr<proxygen::HTTPMessage> request_headers_to_captcha_service_;
  std::unique_ptr<folly::IOBuf>
      request_body_to_captcha_service_; // POST body to reCaptcha API
  std::unique_ptr<proxygen::HTTPMessage> captcha_service_response_headers_;
  proxygen::HTTPConnector connector_;
  CaptchaServiceHTTPTransactionHandler txn_handler_;
  folly::HHWheelTimer *timer_;
  proxygen::HTTPTransaction *txn_{nullptr};
  bool client_terminated_{false};
  folly::Promise<std::string> captcha_service_result_promise_;
  std::function<void(folly::dynamic &)> use_result_;
  std::shared_ptr<folly::SSLContext> ssl_context_;

  std::unique_ptr<proxygen::HTTPMessage> headers_;
  std::unique_ptr<folly::IOBuf> body_{nullptr};
};
} // namespace web
} // namespace url_shortener
} // namespace ec_prv
#endif // _INCLUDE_EC_PRV_URL_SHORTENER_WEB_MAKE_URL_REQUEST_HANDLER_H

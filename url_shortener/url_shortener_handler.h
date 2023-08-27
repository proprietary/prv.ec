#ifndef _INCLUDE_EC_PRV_URL_SHORTENER_WEB_URL_REDIRECT_HANDLER_H
#define _INCLUDE_EC_PRV_URL_SHORTENER_WEB_URL_REDIRECT_HANDLER_H

#include <folly/Memory.h>
#include <proxygen/httpserver/RequestHandler.h>
#include <string>

#include "db.h"
#include "app_config.h"


namespace ec_prv {
namespace url_shortener {
namespace web {

static constexpr size_t max_long_url_length = 1000;

// `UrlRedirectHandler` is an optimized request handler for serving
// the URL redirection service. Looks up a stored mapping of short
// slugs to long URLs, then serves a 301 HTTP response.
class UrlRedirectHandler : public proxygen::RequestHandler {
public:
  explicit UrlRedirectHandler(std::string&& short_url,
      ::ec_prv::url_shortener::db::ShortenedUrlsDatabase *db,
      const ::ec_prv::url_shortener::app_config::ReadOnlyAppConfig *const ro_app_config);

  void
  onRequest(std::unique_ptr<proxygen::HTTPMessage> request) noexcept override;

  void onBody(std::unique_ptr<folly::IOBuf> body) noexcept override;

  void onEOM() noexcept override;

  void onUpgrade(proxygen::UpgradeProtocol proto) noexcept override;

  void requestComplete() noexcept override;

  void onError(proxygen::ProxygenError err) noexcept override;

private:
  ::ec_prv::url_shortener::db::ShortenedUrlsDatabase *const db_;
  const ::ec_prv::url_shortener::app_config::ReadOnlyAppConfig *const ro_app_config_;

  std::string buf_;
  std::string short_url_;
};
} // namespace web
} // namespace url_shortener
} // namespace ec_prv
#endif // _INCLUDE_EC_PRV_URL_SHORTENER_WEB_URL_REDIRECT_HANDLER_H

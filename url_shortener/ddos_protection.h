#ifndef _INCLUDE_EC_PRV_URL_SHORTENER_WEB_DDOS_PROTECTION_H
#define _INCLUDE_EC_PRV_URL_SHORTENER_WEB_DDOS_PROTECTION_H

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <folly/Expected.h>
#include <folly/GLog.h>
#include <folly/IPAddress.h>
#include <folly/container/F14Map.h>
#include <folly/futures/Future.h>
#include <folly/io/async/HHWheelTimer.h>
#include <memory>
#include <proxygen/httpserver/Filters.h>
#include <proxygen/httpserver/RequestHandler.h>
#include <proxygen/httpserver/RequestHandlerFactory.h>
#include <proxygen/httpserver/ResponseBuilder.h>

#include "app_config.h"

namespace ec_prv {
namespace url_shortener {
namespace web {

class IPAccessCountService {
public:
  explicit IPAccessCountService(std::chrono::seconds ttl);

  void log_access(const folly::IPAddress &ip) noexcept;
  uint32_t hits_per_minute(const folly::IPAddress &ip) const noexcept;

private:
  void recycle() noexcept;
  void check_recycle() noexcept;
  folly::F14FastMap<folly::IPAddress, uint32_t> ip_access_count_;
  std::chrono::time_point<std::chrono::system_clock> time_last_recycled_{
      std::chrono::system_clock::now()};
  std::chrono::time_point<std::chrono::system_clock> time_last_added_{
      std::chrono::system_clock::time_point::min()};
  std::chrono::seconds ttl_;
};

class RateLimitFilter : public proxygen::Filter {
public:
  explicit RateLimitFilter(proxygen::RequestHandler *upstream);

  void onRequest(std::unique_ptr<proxygen::HTTPMessage> msg) noexcept override;
  void requestComplete() noexcept override;
  void onError(proxygen::ProxygenError) noexcept override;

  void onEOM() noexcept override {}
  void onBody(std::unique_ptr<folly::IOBuf>) noexcept override {}
  void onEgressPaused() noexcept override {}
  void onEgressResumed() noexcept override {}
};

class AntiAbuseProtection : public proxygen::RequestHandlerFactory {
public:
  explicit AntiAbuseProtection(
      const ::ec_prv::url_shortener::app_config::ReadOnlyAppConfig
          *ro_app_config);

  void onServerStart(folly::EventBase *evb) noexcept override;
  void onServerStop() noexcept override;
  proxygen::RequestHandler *
  onRequest(proxygen::RequestHandler *rh,
            proxygen::HTTPMessage *msg) noexcept override;

private:
  // Check if this message was proxied by a trusted server which forwards the
  // client's IP in the 'X-Forwarded-For' header.
  bool can_trust_x_forwarded_for(const proxygen::HTTPMessage *) const;

  // Check if this message was indeed proxied by Cloudflare.
  bool can_trust_cf_connecting_ip(const proxygen::HTTPMessage *) const;

  IPAccessCountService *ip_access_count_svc_{nullptr};
  const ::ec_prv::url_shortener::app_config::ReadOnlyAppConfig
      *const ro_app_config_;
};

} // namespace web
} // namespace url_shortener
} // namespace ec_prv
#endif // _INCLUDE_EC_PRV_URL_SHORTENER_WEB_DDOS_PROTECTION_Hi

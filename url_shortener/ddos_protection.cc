#include "ddos_protection.h"
#include "app_config.h"

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
#include <proxygen/lib/http/ProxygenErrorEnum.h>

namespace ec_prv {
namespace url_shortener {
namespace web {

IPAccessCountService::IPAccessCountService(std::chrono::seconds ttl)
    : ttl_(ttl) {}

void IPAccessCountService::log_access(const folly::IPAddress &ip) noexcept {
  auto [it, ok] = ip_access_count_.try_emplace(ip, 1);
  if (!ok) {
    it->second = it->second + 1;
  }
  time_last_added_ = std::chrono::system_clock::now();
  check_recycle();
  DLOG(INFO) << "rate limiter: logged IP " << ip.str()
             << " count=" << it->second;
}
uint32_t IPAccessCountService::hits_per_minute(
    const folly::IPAddress &ip) const noexcept {
  if (time_last_added_ < time_last_recycled_) {
    return 0;
  }
  auto iter = ip_access_count_.find(ip);
  if (iter == ip_access_count_.end()) {
    return 0;
  }
  auto hits = iter->second;
  if (hits == 0) {
    return 0;
  }
  auto over_seconds = std::chrono::duration_cast<std::chrono::seconds>(
                          time_last_added_ - time_last_recycled_)
                          .count();
  if (over_seconds == 0) {
    return 0;
  }
  return hits / ((over_seconds / 60) > 0 ? over_seconds / 60 : 1);
}
void IPAccessCountService::recycle() noexcept {
  ip_access_count_.clear();
  time_last_recycled_ = std::chrono::system_clock::now();
}
void IPAccessCountService::check_recycle() noexcept {
  auto age = std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::system_clock::now() - time_last_recycled_);
  if (age > ttl_) {
    recycle();
  }
}

RateLimitFilter::RateLimitFilter(proxygen::RequestHandler *upstream)
    : proxygen::Filter(upstream) {}

void RateLimitFilter::onRequest(
    std::unique_ptr<proxygen::HTTPMessage> msg) noexcept {
  upstream_->onError(proxygen::ProxygenError::kErrorUnknown);
  upstream_ = nullptr;
  proxygen::ResponseBuilder(downstream_)
      .status(429, "Too Many Requests")
      .sendWithEOM();
}

void RateLimitFilter::onError(proxygen::ProxygenError err) noexcept {
  if (upstream_) {
    upstream_->onError(err);
    upstream_ = nullptr;
  }
  delete this;
}

void RateLimitFilter::requestComplete() noexcept {
  DLOG_IF(ERROR, upstream_) << "upstream_ should be null here";
  delete this;
}

// Check if this message was proxied by a trusted server which forwards the
// client's IP in the 'X-Forwarded-For' header.
bool AntiAbuseProtection::can_trust_x_forwarded_for(
    const proxygen::HTTPMessage *headers) const {
  const folly::IPAddress &this_ip = headers->getClientAddress().getIPAddress();
  for (const auto &cidr : ro_app_config_->reverse_proxy_cidrs) {
    if (this_ip.inSubnet(cidr.first, cidr.second)) {
      return true;
    }
  }
  DLOG(WARNING) << "Cannot trust potential reverse proxy server: "
                << this_ip.str();
  return false;
}

// Check if this message was indeed proxied by Cloudflare.
bool AntiAbuseProtection::can_trust_cf_connecting_ip(
    const proxygen::HTTPMessage *headers) const {
  const folly::IPAddress &this_ip = headers->getClientAddress().getIPAddress();
  for (const auto &cidr : ro_app_config_->cf_cidrs) {
    if (this_ip.inSubnet(cidr.first, cidr.second)) {
      DLOG(INFO) << "Found a Cloudflare IP that we can trust: "
                 << this_ip.str();
      return true;
    }
  }
  DLOG(WARNING) << "Cannot trust potential Cloudflare server: "
                << this_ip.str();
  return false;
}

AntiAbuseProtection::AntiAbuseProtection(
    const ::ec_prv::url_shortener::app_config::ReadOnlyAppConfig *ro_app_config)
    : ro_app_config_(ro_app_config) {}

void AntiAbuseProtection::onServerStart(folly::EventBase *evb) noexcept {
  ip_access_count_svc_ = new IPAccessCountService{
      std::chrono::seconds{ro_app_config_->ip_rate_limiter_seconds_ttl}};
}

void AntiAbuseProtection::onServerStop() noexcept {
  CHECK(ip_access_count_svc_ != nullptr) << "lifetime error";
  delete ip_access_count_svc_;
}

proxygen::RequestHandler *
AntiAbuseProtection::onRequest(proxygen::RequestHandler *rh,
                               proxygen::HTTPMessage *msg) noexcept {
  // Should this route be protected?
  auto method = msg->getMethod();
  std::string_view path{msg->getPathAsStringPiece().begin(),
                        msg->getPathAsStringPiece().end()};
  if (method == proxygen::HTTPMethod::POST ||
      method == proxygen::HTTPMethod::PUT ||
      method == proxygen::HTTPMethod::DELETE || path.starts_with("/api/")) {
    DLOG(INFO) << "in rate limit filter";

    folly::IPAddress client_ip = msg->getClientAddress().getIPAddress();
    DLOG(INFO) << "determining whether the following IP: " << client_ip.str()
               << " is really the client's real IP";
    // if this service is behind Cloudflare, the real client's IP will be behind
    // a header 'CF-Connecting-IP'
    if (msg->getHeaders().exists("CF-Connecting-IP")) {
      if (!can_trust_cf_connecting_ip(msg)) {
        VLOG(1) << "Found potential malicious, non-Cloudflare client ("
                << client_ip.str() << ") injecting a CF-Connecting-IP header";
        return new RateLimitFilter{rh};
      }
      auto cloudflare_connecting_ip =
          msg->getHeaders().getSingleOrEmpty("CF-Connecting-IP");
      auto r = folly::IPAddress::tryFromString(cloudflare_connecting_ip);
      if (!r) {
        VLOG(2) << "Could not parse IP in header CF-Connecting-IP: "
                << cloudflare_connecting_ip;
        return new RateLimitFilter{rh};
      }
      client_ip = *r;
    } else if (msg->getHeaders().exists("X-Forwarded-For")) {
      if (!can_trust_x_forwarded_for(msg)) {
        VLOG(1) << "Found potential malicious client pretending to be a proxy";
        return new RateLimitFilter{rh};
      }
      auto x_forwarded_for =
          msg->getHeaders().getSingleOrEmpty("X-Forwarded-For");
      DLOG_IF(ERROR, !x_forwarded_for.empty())
          << "headers implementation error";
      auto r = folly::IPAddress::tryFromString(x_forwarded_for);
      if (!r) {
        VLOG(2) << "Could not parse IP in header X-Forwarded-For: "
                << x_forwarded_for;
        return new RateLimitFilter{rh};
      }
      client_ip = *r;
    }
    DLOG(INFO) << "resolved client IP as: " << client_ip.str();
    ip_access_count_svc_->log_access(client_ip);
    // Check rate limiting policy from app configuration
    DLOG(INFO) << "running at "
               << ip_access_count_svc_->hits_per_minute(client_ip)
               << " hits per minute, and the max configured is "
               << ro_app_config_->rate_limit_per_minute;
    if (ip_access_count_svc_->hits_per_minute(client_ip) >
        ro_app_config_->rate_limit_per_minute) {
      return new RateLimitFilter{rh};
    }
    // otherwise fall through to next `RequestHandler`
    // upstream_->onRequest(std::move(msg));
  }
  return rh;
}

} // namespace web
} // namespace url_shortener
} // namespace ec_prv

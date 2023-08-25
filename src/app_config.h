#ifndef _INCLUDE_EC_PRV_URL_SHORTENER_APP_CONFIG_H
#define _INCLUDE_EC_PRV_URL_SHORTENER_APP_CONFIG_H

#include <cstdint>
#include <memory>

namespace ec_prv {
namespace url_shortener {
namespace app_config {

struct ReadOnlyAppConfig {
  // Random 256-bit key with which to hash input long URLs into short slugs.
  const uint64_t *highwayhash_key{nullptr};

  uint16_t grpc_service_port{50051};

  uint16_t web_server_port{60022};

  // This is the base URL for your URL shortening service, after which
  // the shortened URL slug is appended. For example,
  // "https://prv.ec/" or "https://bit.ly/"
  const char *url_shortener_service_base_url{"https://prv.ec/"};

  struct ReadOnlyAppConfigDeleter {
    void operator()(ReadOnlyAppConfig *that) noexcept;
  };

  [[nodiscard]] static auto new_from_env()
      -> std::unique_ptr<ReadOnlyAppConfig, ReadOnlyAppConfigDeleter>;
};

} // namespace app_config
} // namespace url_shortener
} // namespace ec_prv

#endif // _INCLUDE_EC_PRV_URL_SHORTENER_APP_CONFIG_H

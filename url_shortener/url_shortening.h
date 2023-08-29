#ifndef _INCLUDE_EC_PRV_URL_SHORTENER__URL_SHORTENING_H
#define _INCLUDE_EC_PRV_URL_SHORTENER__URL_SHORTENING_H

#include <cstdint>
#include <highwayhash/highwayhash.h>
#include <optional>
#include <string>
#include <string_view>

namespace ec_prv {
namespace url_shortener {
namespace url_shortening {
auto parse_out_request_str(std::string_view) -> std::string_view;
auto generate_shortened_url(std::string_view, const uint64_t *) -> std::string;
auto create_highwayhash_key(const std::string &src) -> std::uint64_t *;
auto is_ok_request_path(std::string_view src) -> bool;

// fully configurable URL shortening policy
class UrlShorteningConfig {
private:
  const uint8_t slug_length_;
  const std::string alphabet_;
  alignas(32) highwayhash::HHKey highwayhash_key_;

public:
  explicit UrlShorteningConfig(const std::string &alphabet,
                               const uint8_t slug_length,
                               const uint64_t *highwayhash_key_input);

  // Generates a slug for a long URL, with an option to get another one if there
  // is a collision.
  auto generate_slug(std::string &dst, std::string_view long_url,
                     uint8_t nth_try = 1U) const -> bool;
};
} // namespace url_shortening
} // namespace url_shortener
} // namespace ec_prv

#endif // _INCLUDE_EC_PRV_URL_SHORTENER__URL_SHORTENING_H

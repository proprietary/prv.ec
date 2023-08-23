#ifndef _EC_PRV_URL_SHORTENER__URL_SHORTENING_H
#define _EC_PRV_URL_SHORTENER__URL_SHORTENING_H

#include <string>
#include <string_view>
#include <optional>
#include <cstdint>

namespace ec_prv {
  namespace url_shortening {
    auto parse_out_request_str(std::string_view) -> std::optional<std::string_view>;
    auto generate_shortened_url(std::string_view, const uint64_t*) -> std::string;
    auto create_highwayhash_key(const std::string& src) -> std::uint64_t*;
  } // namespace url_shortening
} // namespace ec_prv


#endif // _EC_PRV_URL_SHORTENER__URL_SHORTENING_H

#ifndef _INCLUDE_EC_PRV_URL_SHORTENER__URL_SHORTENING_H
#define _INCLUDE_EC_PRV_URL_SHORTENER__URL_SHORTENING_H

#include <cstdint>
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
} // namespace url_shortening
} // namespace url_shortener
} // namespace ec_prv

#endif // _INCLUDE_EC_PRV_URL_SHORTENER__URL_SHORTENING_H

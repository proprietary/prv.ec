#ifndef _EC_PRV_URL_SHORTENER__URL_SHORTENING_H
#define _EC_PRV_URL_SHORTENER__URL_SHORTENING_H

#include <string>
#include <string_view>
#include <optional>

namespace ec_prv {
  namespace url_shortening {
    auto parse_out_request_str(std::string_view) -> std::optional<std::string_view>;
  } // namespace url_shortening
} // namespace ec_prv


#endif // _EC_PRV_URL_SHORTENER__URL_SHORTENING_H

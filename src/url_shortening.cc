#include "url_shortening.h"

namespace ec_prv {
  namespace url_shortening {
    const static std::string_view alphabet = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    static constexpr std::size_t max_url_slug = 1000;

    auto parse_out_request_str(std::string_view src) -> std::optional<std::string_view> {
      if (src.size() > max_url_slug) {
	// too long
	return {};
      }
      auto it = src.begin();
      if (*it != '/') {
	return {};
      }
      auto b = it;
      ++it;
      for (; it != src.end(); ++it) {
	bool included = false;
	for (auto j = 0; j < alphabet.size(); ++j) {
	  if (alphabet[j] == *it) {
	    included = true;
	    break;
	  }
	}
	if (included == false) {
	  if (*it == '?') {
	    break;
	  }
	  // illegal symbol
	  return {};
	}
      }
      auto e = it;
      return std::string_view(b, e);
    }
  } // namespace url_shortening
} // namespace ec_prv


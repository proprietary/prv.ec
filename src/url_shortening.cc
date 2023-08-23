#include "url_shortening.h"
#include <cstdint>
#include <cstring>
#include <cassert>
#include <iostream>
#include <algorithm>
#include <string>
#include "highwayhash/highwayhash.h"


namespace ec_prv {
  namespace url_shortening {
    const static std::string_view alphabet = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    static constexpr std::size_t max_url_slug = 1000;
    static constexpr auto shortened_url_len = 7;

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

    auto create_highwayhash_key(const std::string& src) -> std::uint64_t* {
      // parse 256-bit hex string as a little-endian 256-bit integer, returning the integer in an array of 4 `uint64_t`s in big-endian order
      auto len = src.size();
      assert(len == 64);
      if (64 != len) {
	throw std::invalid_argument{"invalid string size"};
      }
      uint64_t* out = new uint64_t[4];
      for (int i = 0; i < 4; ++i) {
	std::string bs = src.substr(i * 16, 16);
	out[i] = std::stoull(bs, nullptr, 16);
      }
      return out;
    }

    auto generate_shortened_url(std::string_view src, const uint64_t* highwayhash_key) -> std::string {
      using namespace highwayhash;

      const char* in = src.data();
      const std::size_t size = std::strlen(in);
      // TODO(zds): initialize this with specified bytes at compile time rather than at runtime
      alignas(32) const HHKey key = {highwayhash_key[0], highwayhash_key[1], highwayhash_key[2], highwayhash_key[3]};
      HHResult64 result;
      HHStateT<HH_TARGET_PREFERRED> state(key);
      HighwayHashT(&state, in, size, &result);
      std::string out;
      out.reserve(shortened_url_len);
      for (auto i = 0; i < shortened_url_len; ++i) {
	std::size_t idx = result % alphabet.length();
	out.append(1, alphabet[idx]);
	result /= alphabet.length();
      }
      return out;
    }
  } // namespace url_shortening
} // namespace ec_prv


#include "url_shortening.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <highwayhash/highwayhash.h>
#include <iostream>
#include <string>

namespace ec_prv {
namespace url_shortener {
namespace url_shortening {
const static std::string_view alphabet =
    "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
static constexpr std::size_t max_request_str_len = 1000;
static constexpr auto shortened_url_len =
    7; // char length of the slug returned that keys a shortened URL

auto is_ok_request_path(std::string_view src) -> bool {
  if (src.length() > max_request_str_len) {
    return false;
  }
  for (int i = 0; i < src.length(); ++i) {
    int j = 0;
    while (j < alphabet.length() && src[i] != alphabet[j]) {
      ++j;
    }
    if (j == alphabet.length()) {
      return false;
    }
  }
  return true;
}

auto parse_out_request_str(std::string_view src) -> std::string_view {
  if (src.size() > max_request_str_len) {
    // too long
    return {};
  }
  auto it = src.begin();
  if (*it != '/') {
    return {};
  }
  ++it;
  auto b = it;
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

auto create_highwayhash_key(const std::string &src) -> std::uint64_t * {
  // parse 256-bit hex string as a little-endian 256-bit integer, returning the
  // integer in an array of 4 `uint64_t`s in big-endian order
  auto len = src.size();
  assert(len == 64);
  if (64 != len) {
    throw std::invalid_argument{"invalid string size"};
  }
  uint64_t *out = new uint64_t[4];
  for (int i = 0; i < 4; ++i) {
    std::string bs = src.substr(i * 16, 16);
    out[i] = std::stoull(bs, nullptr, 16);
  }
  return out;
}

auto generate_shortened_url(std::string_view src,
                            const uint64_t *highwayhash_key) -> std::string {
  using namespace highwayhash;

  const char *in = src.data();
  const std::size_t size = std::strlen(in);
  // TODO(zds): initialize this with specified bytes at compile time rather than
  // at runtime
  alignas(32) const HHKey key = {highwayhash_key[0], highwayhash_key[1],
                                 highwayhash_key[2], highwayhash_key[3]};
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

UrlShorteningConfig::UrlShorteningConfig(const std::string &alphabet,
                                         const uint8_t slug_length,
                                         const uint64_t *highwayhash_key_input)
    : alphabet_(alphabet), slug_length_(slug_length) {
  highwayhash_key_[0] = highwayhash_key_input[0];
  highwayhash_key_[1] = highwayhash_key_input[1];
  highwayhash_key_[2] = highwayhash_key_input[2];
  highwayhash_key_[3] = highwayhash_key_input[3];
}

auto UrlShorteningConfig::generate_slug(std::string &dst,
                                        std::string_view long_url,
                                        uint8_t nth_try) const -> bool {
  dst.reserve(slug_length_);
  highwayhash::HHResult256 result;
  highwayhash::HHStateT<HH_TARGET_PREFERRED> state(highwayhash_key_);
  highwayhash::HighwayHashT(&state, long_url.data(), long_url.size(), &result);
  const std::size_t alphabet_len = alphabet_.length();
  std::size_t result_idx = 0;
  while (nth_try-- > 0) {
    // collisions should be very rare, so should never need to try more than
    // once
    dst.clear();
    for (auto i = 0; i < slug_length_; ++i) {
      if (result[result_idx] <= 0) {
        result_idx = result_idx + 1;
        assert(4 == sizeof(result) / sizeof(result[0]));
        if (result_idx > sizeof(result) / sizeof(result[0])) {
          // very rare error: ran out of hashes; probably alphabet is too short
          return false;
        }
      }
      std::size_t alphabet_idx = result[result_idx] % alphabet_len;
      dst.append(1, alphabet[alphabet_idx]);
      result[result_idx] /= alphabet_len;
    }
  }
  return true;
}
} // namespace url_shortening
} // namespace url_shortener
} // namespace ec_prv

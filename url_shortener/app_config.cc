#include "app_config.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <folly/Expected.h>
#include <folly/IPAddress.h>
#include <glog/logging.h>
#include <stdexcept>
#include <string>

namespace ec_prv {
namespace url_shortener {
namespace app_config {

namespace {

using namespace std::literals;

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

auto split_csv_string(std::string_view csv_string,
                      std::string_view delim = ","sv)
    -> std::vector<std::string> {
  std::vector<std::string> dst;
  auto i = 0;
  auto j = 0;
  while ((j = csv_string.find(delim, i)) != csv_string.npos) {
    dst.emplace_back(csv_string.substr(i, j - i));
    i = j + 1;
  }
  if (i < csv_string.length()) {
    dst.emplace_back(csv_string.substr(i));
  }
  return dst;
}

} // namespace

void ReadOnlyAppConfig::ReadOnlyAppConfigDeleter::operator()(
    ReadOnlyAppConfig *that) noexcept {
  if (that->highwayhash_key != nullptr) {
    delete that->highwayhash_key;
  }
}

auto ReadOnlyAppConfig::new_from_env()
    -> std::unique_ptr<ReadOnlyAppConfig, ReadOnlyAppConfigDeleter> {
  std::unique_ptr<ReadOnlyAppConfig, ReadOnlyAppConfigDeleter> dst{
      new ReadOnlyAppConfig, ReadOnlyAppConfigDeleter{}};

  const char *highwayhash_key_inp =
      std::getenv("EC_PRV_URL_SHORTENER__HIGHWAYHASH_KEY");
  if (nullptr == highwayhash_key_inp) {
    throw std::runtime_error{
        "Missing environment variable for highwayhash key\n"};
  }
  const uint64_t *hhkey = create_highwayhash_key(highwayhash_key_inp);
  dst->highwayhash_key = hhkey;

  const char *rpc_port_s = std::getenv("EC_PRV_URL_SHORTENER__RPC_PORT");
  if (rpc_port_s != nullptr) {
    dst->grpc_service_port = static_cast<uint16_t>(std::atoi(rpc_port_s));
  }

  const char *web_server_port_s =
      std::getenv("EC_PRV_URL_SHORTENER__WEB_SERVER_PORT");
  if (web_server_port_s != nullptr) {
    dst->web_server_port = static_cast<uint16_t>(std::atoi(web_server_port_s));
  }

  const char *web_server_bind_host_inp =
      std::getenv("EC_PRV_URL_SHORTENER__WEB_SERVER_BIND_HOST");
  if (web_server_bind_host_inp != nullptr &&
      strlen(web_server_bind_host_inp) > 0) {
    dst->web_server_bind_host.assign(web_server_bind_host_inp);
  }

  const char *static_file_doc_root_inp =
      std::getenv("EC_PRV_URL_SHORTENER__STATIC_FILE_DOC_ROOT");
  if (static_file_doc_root_inp != nullptr) {
    dst->static_file_doc_root = std::filesystem::path{static_file_doc_root_inp};
    CHECK(std::filesystem::exists(dst->static_file_doc_root))
        << "Static file doc root provided via environment variables does not "
           "exist";
  }

  CHECK(std::getenv("EC_PRV_URL_SHORTENER__FRONTEND_DOC_ROOT") != nullptr);
  dst->frontend_doc_root = std::filesystem::path{
      std::getenv("EC_PRV_URL_SHORTENER__FRONTEND_DOC_ROOT")};
  CHECK(std::filesystem::exists(dst->frontend_doc_root))
      << "Missing environment variable for frontend doc root";

  const char *static_file_request_path_prefix_inp =
      std::getenv("EC_PRV_URL_SHORTENER__STATIC_FILE_REQUEST_PATH_PREFIX");
  if (static_file_request_path_prefix_inp != nullptr) {
    dst->static_file_request_path_prefix = static_file_request_path_prefix_inp;
  }

  const char *url_shortener_service_base_url_inp =
      std::getenv("EC_PRV_URL_SHORTENER__URL_SHORTENER_SERVICE_BASE_URL");
  if (url_shortener_service_base_url_inp != nullptr) {
    dst->url_shortener_service_base_url = url_shortener_service_base_url_inp;
  }

  const char *trusted_certificates_path_inp =
      std::getenv("EC_PRV_URL_SHORTENER__CA_CERTS_PATH");
  if (trusted_certificates_path_inp != nullptr) {
    dst->trusted_certificates_path.assign(trusted_certificates_path_inp);
    CHECK(std::filesystem::exists(dst->trusted_certificates_path));
  }

  dst->captcha_service_api_key =
      std::getenv("EC_PRV_URL_SHORTENER__CAPTCHA_SERVICE_API_KEY");
  CHECK(dst->captcha_service_api_key.length() > 0)
      << "Missing environment variable for reCAPTCHA API key";

  CHECK(std::getenv("EC_PRV_URL_SHORTENER__KNOWN_CLOUDFLARE_CIDRS") != nullptr);
  dst->known_cloudflare_cidrs = split_csv_string(
      std::getenv("EC_PRV_URL_SHORTENER__KNOWN_CLOUDFLARE_CIDRS"));
  LOG_IF(WARNING, dst->known_cloudflare_cidrs.size() == 0)
      << "Known Cloudflare proxy CIDRs were not specified in configuration. "
         "Without this, this web service will not work if behind a Cloudflare "
         "reverse proxy.";

  for (const auto &cf_cidr_str : dst->known_cloudflare_cidrs) {
    auto r = folly::IPAddress::tryCreateNetwork(cf_cidr_str);
    if (r.hasValue()) {
      dst->cf_cidrs.push_back(r.value());
    } else {
      LOG(ERROR) << "Fail to parse Cloudflare CIDR from app configuration (\""
                 << cf_cidr_str << "\")";
    }
  }

  CHECK(std::getenv("EC_PRV_URL_SHORTENER__ALLOWED_REVERSE_PROXY_CIDRS") !=
        nullptr);
  dst->allowed_reverse_proxy_cidrs = split_csv_string(
      std::getenv("EC_PRV_URL_SHORTENER__ALLOWED_REVERSE_PROXY_CIDRS"));
  LOG_IF(WARNING, dst->allowed_reverse_proxy_cidrs.empty())
      << "No reverse proxy IPs set in configuration. If you set up this web "
         "service before a reverse proxy that sets the true client IP in HTTP "
         "headers, this web service may not work. The app configuration does "
         "not have the CIDR(s) of this reverse proxy (to know who to trust).";
  for (const auto &rp_cidr_str : dst->allowed_reverse_proxy_cidrs) {
    auto r = folly::IPAddress::tryCreateNetwork(rp_cidr_str);
    if (r.hasValue()) {
      dst->reverse_proxy_cidrs.push_back(r.value());
    } else {
      LOG(ERROR)
          << "Fail to parse reverse proxy CIDR from app configuration (\""
          << rp_cidr_str << "\")";
    }
  }

  const char *slug_length_inp =
      std::getenv("EC_PRV_URL_SHORTENER__SLUG_LENGTH");
  if (slug_length_inp != nullptr)
    dst->slug_length = std::atoi(slug_length_inp);
  CHECK(dst->slug_length > 0)
      << "\"slug_length\" configuration parameter must be greater than 0 -- "
         "this is the number of characters in the slug for the shortened URL";

  const char *alphabet_inp = std::getenv("EC_PRV_URL_SHORTENER__ALPHABET");
  if (alphabet_inp != nullptr && strlen(alphabet_inp) > 1) {
    dst->alphabet = alphabet_inp;
  } else {
    LOG(ERROR) << "did not use \"alphabet\" configuration parameter from "
                  "environment; invalid input";
  }
  CHECK(dst->alphabet.length() > 0)
      << "Invalid \"alphabet\" app configuration parameter";

  return dst;
}

} // namespace app_config
} // namespace url_shortener
} // namespace ec_prv

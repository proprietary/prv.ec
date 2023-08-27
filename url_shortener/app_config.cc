#include "app_config.h"

#include <glog/logging.h>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <string>
#include <stdexcept>

namespace ec_prv {
namespace url_shortener {
namespace app_config {

namespace {
  
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
  
} // namespace

void ReadOnlyAppConfig::ReadOnlyAppConfigDeleter::operator()(ReadOnlyAppConfig *that) noexcept {
  if (that->highwayhash_key != nullptr) {
    delete that->highwayhash_key;
  }
}
  
auto ReadOnlyAppConfig::new_from_env()
    -> std::unique_ptr<ReadOnlyAppConfig, ReadOnlyAppConfigDeleter> {
  std::unique_ptr<ReadOnlyAppConfig, ReadOnlyAppConfigDeleter> dst {new ReadOnlyAppConfig, ReadOnlyAppConfigDeleter{}};
  
  const char* highwayhash_key_inp =
      std::getenv("EC_PRV_URL_SHORTENER__HIGHWAYHASH_KEY");
  if (nullptr == highwayhash_key_inp) {
    throw std::runtime_error{"Missing environment variable for highwayhash key\n"};
  }
  const uint64_t* hhkey = create_highwayhash_key(highwayhash_key_inp);
  dst->highwayhash_key = hhkey;

  const char *rpc_port_s = std::getenv("EC_PRV_URL_SHORTENER__RPC_PORT");
  if (rpc_port_s != nullptr) {
    dst->grpc_service_port = static_cast<uint16_t>(std::atoi(rpc_port_s));
  }

  const char* web_server_port_s = std::getenv("EC_PRV_URL_SHORTENER__WEB_SERVER_PORT");
  if (web_server_port_s != nullptr) {
    dst->web_server_port = static_cast<uint16_t>(std::atoi(web_server_port_s));
  }

  const char *web_server_bind_host_inp = std::getenv("EC_PRV_URL_SHORTENER__WEB_SERVER_BIND_HOST");
  if (web_server_bind_host_inp != nullptr && strlen(web_server_bind_host_inp) > 0) {
    dst->web_server_bind_host.assign(web_server_bind_host_inp);
  }

  const char *static_file_doc_root_inp = std::getenv("EC_PRV_URL_SHORTENER__STATIC_FILE_DOC_ROOT");
  if (static_file_doc_root_inp != nullptr) {
    dst->static_file_doc_root = std::filesystem::path{static_file_doc_root_inp};
  }

  const char *static_file_request_path_prefix_inp = std::getenv("EC_PRV_URL_SHORTENER__STATIC_FILE_REQUEST_PATH_PREFIX");
  if (static_file_request_path_prefix_inp != nullptr) {
    dst->static_file_request_path_prefix = static_file_request_path_prefix_inp;
  }

  const char *url_shortener_service_base_url_inp = std::getenv("EC_PRV_URL_SHORTENER__URL_SHORTENER_SERVICE_BASE_URL");
  if (url_shortener_service_base_url_inp != nullptr) {
    dst->url_shortener_service_base_url = url_shortener_service_base_url_inp;
  }

  const char *trusted_certificates_path_inp = std::getenv("EC_PRV_URL_SHORTENER__CA_CERTS_PATH");
  if (trusted_certificates_path_inp != nullptr) {
    dst->trusted_certificates_path.assign(trusted_certificates_path_inp);
    CHECK(std::filesystem::exists(dst->trusted_certificates_path));
  }

  dst->captcha_service_api_key = std::getenv("EC_PRV_URL_SHORTENER__CAPTCHA_SERVICE_API_KEY");
  CHECK(dst->captcha_service_api_key.length() > 0) << "Missing environment variable for reCAPTCHA API key";

  return dst;
}
  
} // namespace app_config
} // namespace url_shortener
} // namespace ec_prv

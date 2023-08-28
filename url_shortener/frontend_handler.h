#ifndef _INCLUDE_EC_PRV_URL_SHORTENER_WEB_FRONTEND_HANDLER_H
#define _INCLUDE_EC_PRV_URL_SHORTENER_WEB_FRONTEND_HANDLER_H

#include <folly/Range.h>
#include <vector>
#include <filesystem>
#include <folly/File.h>
#include <folly/Memory.h>
#include <folly/container/F14Map.h>
#include <folly/io/IOBuf.h>
#include <folly/io/IOBufQueue.h>
#include <memory>
#include <proxygen/httpserver/RequestHandler.h>
#include <proxygen/httpserver/ResponseHandler.h>
#include <string>
#include <string_view>
#include <stdint.h>


namespace ec_prv {
namespace url_shortener {
namespace web {

// Put the entire directory of the frontend in memory for fast GET access.
std::unique_ptr<folly::F14NodeMap<std::string, std::vector<uint8_t>>>
build_frontend_dir_cache(const std::filesystem::path& frontend_doc_root);

// TODO(zds): serve files with appropriate mime type

// Specifically designed to serve the static HTML/JS/CSS assets of the
// frontend for this web service. Specifically designed for serving Next.js
// static exports:
// https://nextjs.org/docs/app/building-your-application/deploying/static-exports
class FrontendHandler : public proxygen::RequestHandler {
public:
  explicit FrontendHandler(const folly::F14NodeMap<std::string, std::vector<uint8_t>>* const frontend_dir_cache) : frontend_dir_cache_(frontend_dir_cache) {}

  void
  onRequest(std::unique_ptr<proxygen::HTTPMessage> request) noexcept override;

  void onBody(std::unique_ptr<folly::IOBuf> body) noexcept override;

  void onUpgrade(proxygen::UpgradeProtocol) noexcept override;

  void onEOM() noexcept override;

  void requestComplete() noexcept override;

  void onError(proxygen::ProxygenError) noexcept override;

  void onEgressPaused() noexcept override;

  void onEgressResumed() noexcept override;

private:
  const folly::F14NodeMap<std::string, std::vector<uint8_t>> *const frontend_dir_cache_;
};
} // namespace web
} // namespace url_shortener
} // namespace ec_prv
#endif // _INCLUDE_EC_PRV_URL_SHORTENER_WEB_FRONTEND_HANDLER_H

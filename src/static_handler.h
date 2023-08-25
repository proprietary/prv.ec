#ifndef _INCLUDE_EC_PRV_URL_SHORTENER_WEB_STATIC_HANDLER_H
#define _INCLUDE_EC_PRV_URL_SHORTENER_WEB_STATIC_HANDLER_H

#include <folly/File.h>
#include <folly/io/IOBufQueue.h>
#include <folly/io/IOBuf.h>
#include <folly/Memory.h>
#include <folly/container/F14Map.h>
#include <memory>
#include <proxygen/httpserver/RequestHandler.h>
#include <string>
#include <string_view>
#include <filesystem>

namespace proxygen {
class ResponseHandler;
}

namespace ec_prv {
namespace url_shortener {
namespace web {
static constexpr int max_file_path_length = 1000;
static constexpr std::string_view static_files_url_prefix = "/static/";

// Cache static files in memory to serve them quickly.
class StaticFileCache {
public:
  // TODO(zds): reject files from cache if they are too large
  // TODO(zds): test this with non-textual, binary data files

  auto exists(const std::filesystem::path&) const -> bool;
  auto get(const std::filesystem::path&) const -> std::string_view;
  auto set(const std::filesystem::path &file_path, std::string_view body) -> void;
  auto append_data(const std::filesystem::path &file_path, std::unique_ptr<folly::IOBuf> buf) -> void;

private:
  folly::F14NodeMap<std::string, std::string> cache_;
};

// TODO(zds): serve files with appropriate mime type

class StaticHandler : public proxygen::RequestHandler {
public:
  explicit StaticHandler(StaticFileCache* cache, const std::filesystem::path& doc_root, std::string_view which_file_to_serve);
  
  void
  onRequest(std::unique_ptr<proxygen::HTTPMessage> request) noexcept override;

  void onBody(std::unique_ptr<folly::IOBuf> body) noexcept override;

  void onUpgrade(proxygen::UpgradeProtocol) noexcept override;

  void onEOM() noexcept override;

  void requestComplete() noexcept override;

  void onError(proxygen::ProxygenError) noexcept override;

  void onEgressPaused() noexcept override;

  void onEgressResumed() noexcept override;

  static auto expected_file_path(const proxygen::HTTPMessage* request, const std::filesystem::path& doc_root) noexcept -> std::filesystem::path;
private:
  void read_file(folly::EventBase *evb);
  bool check_for_completion();
  void sendBadRequestError(const std::string &what) noexcept;
  void sendError(const std::string &what) noexcept;

  std::filesystem::path requested_file_path_; // this is for inserting file bodies into cache
  std::unique_ptr<folly::File> file_;
  bool read_file_scheduled_{false};
  std::atomic<bool> paused_{false};
  bool finished_{false};
  const std::filesystem::path& doc_root_;
  const std::string_view which_file_to_serve_;
  // TODO(zds): integrate cache
  StaticFileCache *cache_{nullptr};
};
} // namespace web
} // namespace url_shortener
} // namespace ec_prv
#endif // _INCLUDE_EC_PRV_URL_SHORTENER_WEB_STATIC_HANDLER_H

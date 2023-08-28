#include "frontend_handler.h"
#include <filesystem>
#include <folly/GLog.h>
#include <folly/io/IOBuf.h>
#include <proxygen/httpserver/RequestHandler.h>
#include <proxygen/httpserver/ResponseBuilder.h>
#include <stdint.h>
#include <stdlib.h>
#include <string>
#include <sys/stat.h>

namespace {

auto pop_front_dir(const std::filesystem::path &p) -> std::string {
  auto path_it = p.begin();
  ++path_it;
  std::filesystem::path stripped_path;
  for (; path_it != p.end(); ++path_it) {
    stripped_path /= *path_it;
  }
  return stripped_path.string();
}

} // namespace

namespace ec_prv {
namespace url_shortener {
namespace web {

std::unique_ptr<folly::F14NodeMap<std::string, std::vector<uint8_t>>>
build_frontend_dir_cache(const std::filesystem::path &frontend_doc_root) {
  using cache_t = folly::F14NodeMap<std::string, std::vector<uint8_t>>;
  std::unique_ptr<cache_t> dst{new cache_t{}};
  DLOG(INFO) << "building frontend dir cache";
  for (const std::filesystem::directory_entry &dir_entry :
       std::filesystem::recursive_directory_iterator{frontend_doc_root}) {
    if (std::filesystem::is_regular_file(dir_entry.path())) {
      DLOG(INFO) << "in recursive directory crawl, visting file: "
                 << dir_entry.path();
      // read full file into memory
      auto abspath = std::filesystem::absolute(dir_entry.path());
      struct stat statbuf;
      stat(abspath.c_str(), &statbuf);
      DLOG(INFO) << "Reading \"" << abspath.c_str() << "\" (" << statbuf.st_size
                 << " bytes) into memory";
      std::vector<uint8_t> bytes(statbuf.st_size, 0U);
      std::unique_ptr<FILE, decltype(&fclose)> fh{fopen(abspath.c_str(), "rb"),
                                                  &fclose};
      fread(bytes.data(), statbuf.st_size, sizeof(uint8_t), fh.get());
      auto file_identifier =
          std::filesystem::relative(abspath, frontend_doc_root).string();
      DLOG(INFO) << "Storing frontend file in cache as \"" << file_identifier
                 << "\"";
      dst->emplace(file_identifier, std::move(bytes));
    }
  }
  return std::move(dst);
}

void FrontendHandler::onRequest(
    std::unique_ptr<proxygen::HTTPMessage> request) noexcept {
  // assuming everything was checked upstream
  DLOG_IF(ERROR, request->getMethod() != proxygen::HTTPMethod::GET)
      << "FrontendHandler only accepts GET requests. Must be checked before "
         "invoking FrontendHandler.";
  if (request->getMethod() != proxygen::HTTPMethod::GET) {
    proxygen::ResponseBuilder(downstream_)
        .status(405, "Method Not Allowed")
        .sendWithEOM();
    return;
  }
  constexpr static size_t max_request_path_len = 10000;
  std::string_view path(request->getPathAsStringPiece().begin(),
                        request->getPathAsStringPiece().end());
  if (path[0] != '/' || path.length() == 0 ||
      path.length() > max_request_path_len) {
    DLOG(WARNING) << "No path provided or path malformed: " << path;
    proxygen::ResponseBuilder(downstream_)
        .status(400, "Bad Request")
        .sendWithEOM();
    return;
  }
  auto path_identifier = path.substr(1);
  auto cache_req = frontend_dir_cache_->find(path_identifier);
  if (cache_req == frontend_dir_cache_->end()) {
    DLOG(ERROR) << "Did not find path \"" << path_identifier
                << "\" in cache for frontend files";
    proxygen::ResponseBuilder(downstream_)
        .status(404, "Not Found")
        .sendWithEOM();
    return;
  }
  const std::vector<uint8_t> *buf = &cache_req->second;
  proxygen::ResponseBuilder(downstream_)
      .status(200, "OK")
      .body(folly::IOBuf::wrapBuffer(buf->data(), buf->size()))
      .sendWithEOM();
}

void FrontendHandler::onBody(std::unique_ptr<folly::IOBuf> body) noexcept {}

void FrontendHandler::onUpgrade(proxygen::UpgradeProtocol) noexcept {}

void FrontendHandler::onEOM() noexcept {};

void FrontendHandler::requestComplete() noexcept { delete this; }

void FrontendHandler::onError(proxygen::ProxygenError err) noexcept {
  DLOG(ERROR) << err;
  delete this;
}

void FrontendHandler::onEgressPaused() noexcept {}

void FrontendHandler::onEgressResumed() noexcept {}

} // namespace web
} // namespace url_shortener
} // namespace ec_prv

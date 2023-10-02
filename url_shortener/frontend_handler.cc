#include "frontend_handler.h"
#include <filesystem>
#include <folly/GLog.h>
#include <folly/io/IOBuf.h>
#include <optional>
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

FrontendHandler *FrontendHandler::lookup(
    const folly::F14NodeMap<std::string, std::vector<uint8_t>> *const c,
    std::string_view path) {
  if (path.front() != '/') {
    return nullptr;
  }
  folly::F14NodeMap<std::string, std::vector<uint8_t>>::const_iterator
      cache_req;
  if (path.back() == '/') {
    cache_req = c->find(std::string{path.substr(1)} + "index.html");
  } else {
    cache_req = c->find(path.substr(1));
  }
  if (cache_req == c->end()) {
    return nullptr;
  }
  DLOG(INFO) << "found frontend file " << path;
  const std::vector<uint8_t> *data = &cache_req->second;
  CHECK(data != nullptr);
  const std::string *cached_filename = &cache_req->first;
  const auto mime_type = ::ec_prv::mime_type::infer_mime_type(
      std::string_view{cached_filename->begin(), cached_filename->end()});
  return new FrontendHandler(c, data, mime_type);
}

FrontendHandler::FrontendHandler(
    const folly::F14NodeMap<std::string, std::vector<uint8_t>>
        *const frontend_dir_cache,
    const std::vector<uint8_t> *const prefound_data,
    ::ec_prv::mime_type::MimeType mime_type)
    : frontend_dir_cache_(frontend_dir_cache), prefound_data_(prefound_data),
      mime_type_(mime_type) {}

void FrontendHandler::onRequest(
    std::unique_ptr<proxygen::HTTPMessage> request) noexcept {
  // assuming everything was checked upstream through a shortcut routine
  // `lookup`
  if (prefound_data_ != nullptr) {
    auto mime_type_str = ::ec_prv::mime_type::string(mime_type_);
    proxygen::ResponseBuilder(downstream_)
        .status(200, "OK")
        .body(folly::IOBuf::wrapBuffer(prefound_data_->data(),
                                       prefound_data_->size()))
        .header(proxygen::HTTPHeaderCode::HTTP_HEADER_CONTENT_TYPE,
                mime_type_str)
        .sendWithEOM();
    return;
  } else {
    proxygen::ResponseBuilder(downstream_)
        .status(404, "Not Found")
        .sendWithEOM();
    return;
  }
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

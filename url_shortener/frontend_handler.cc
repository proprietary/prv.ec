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

enum struct MimeType {
  OCTET_STREAM,
  HTML,
  CSS,
  JS,
  JPEG,
  PNG,
  WEBP,
  TEXT,
  WOFF2,
  WOFF,
  ICO,
};

auto string(MimeType src) -> std::string_view {
  switch (src) {
  case MimeType::HTML:
    return "text/html";
  case MimeType::JS:
    return "text/javascript";
  case MimeType::CSS:
    return "text/css";
  case MimeType::JPEG:
    return "image/jpeg";
  case MimeType::PNG:
    return "image/png";
  case MimeType::ICO:
    return "image/vnd.microsoft.icon";
  case MimeType::TEXT:
    return "text/plain";
  case MimeType::WOFF2:
    return "font/woff2";
  case MimeType::WOFF:
    return "font/woff";
  case MimeType::WEBP:
    return "image/webp";
  case MimeType::OCTET_STREAM:
  default:
    return "application/octet-stream";
  }
}

auto infer_mime_type(std::string_view filename) -> MimeType {
  std::filesystem::path p{filename};
  auto ext = p.extension();
  if (ext == ".html" || ext == ".htm") {
    return MimeType::HTML;
  } else if (ext == ".js") {
    return MimeType::JS;
  } else if (ext == ".css") {
    return MimeType::CSS;
  } else if (ext == ".woff2") {
    return MimeType::WOFF2;
  } else if (ext == ".woff") {
    return MimeType::WOFF;
  } else if (ext == ".jpeg") {
    return MimeType::JPEG;
  } else if (ext == ".png") {
    return MimeType::PNG;
  } else if (ext == ".ico") {
    return MimeType::ICO;
  }else if (ext == ".txt" || ext == ".md" || ext == ".rst") {
    return MimeType::TEXT;
  }
  return MimeType::OCTET_STREAM;
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
  const auto mime_type = infer_mime_type(std::string_view{cached_filename->begin(), cached_filename->end()});
  std::string_view mime_type_str = string(mime_type);
  return new FrontendHandler(c, data, mime_type_str);
}

FrontendHandler::FrontendHandler(
    const folly::F14NodeMap<std::string, std::vector<uint8_t>>
        *const frontend_dir_cache,
    const std::vector<uint8_t> *const prefound_data, std::string_view prefound_mime_type_str)
  : frontend_dir_cache_(frontend_dir_cache), prefound_data_(prefound_data), prefound_mime_type_str_(prefound_mime_type_str) {}

void FrontendHandler::onRequest(
    std::unique_ptr<proxygen::HTTPMessage> request) noexcept {
  // assuming everything was checked upstream through a shortcut routine
  // `lookup`
  if (prefound_data_ != nullptr) {
    proxygen::ResponseBuilder(downstream_)
        .status(200, "OK")
        .body(folly::IOBuf::wrapBuffer(prefound_data_->data(),
                                       prefound_data_->size()))
      .header(proxygen::HTTPHeaderCode::HTTP_HEADER_CONTENT_TYPE, prefound_mime_type_str_)
        .sendWithEOM();
    return;
  }
  // check preconditions normally
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
  folly::F14NodeMap<std::string, std::vector<uint8_t>>::const_iterator
      cache_req;
  auto path_identifier = path.substr(1);
  // special case: trailing slash means look for index.html
  if (path_identifier.back() == '/') {
    VLOG(4) << "path requested has a trailing slash: " << path_identifier;
    cache_req =
        frontend_dir_cache_->find(std::string{path_identifier} + "index.html");
  } else {
    // otherwise, just a regular file
    cache_req = frontend_dir_cache_->find(path_identifier);
  }
  if (cache_req == frontend_dir_cache_->end()) {
    DLOG(ERROR) << "Did not find path \"" << path_identifier
                << "\" in cache for frontend files";
    proxygen::ResponseBuilder(downstream_)
        .status(404, "Not Found")
        .sendWithEOM();
    return;
  }
  const std::vector<uint8_t> *buf = &cache_req->second;
  const std::string& fname = *(&cache_req->first);
  const auto inferred_mime = infer_mime_type(fname);
  const auto mime_type_str = string(inferred_mime);
  DLOG(INFO) << "writing content-type header: " << mime_type_str << " for filename: " << fname;
  proxygen::ResponseBuilder(downstream_)
      .status(200, "OK")
      .body(folly::IOBuf::wrapBuffer(buf->data(), buf->size()))
    .header(proxygen::HTTPHeaderCode::HTTP_HEADER_CONTENT_TYPE, mime_type_str)
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

#include "static_handler.h"

#include <algorithm>
#include <folly/FileUtil.h>
#include <folly/Range.h>
#include <folly/executors/GlobalExecutor.h>
#include <folly/io/async/EventBaseManager.h>
#include <glog/logging.h>
#include <proxygen/httpserver/RequestHandler.h>
#include <proxygen/httpserver/ResponseBuilder.h>
#include <string>
#include <string_view>
#include <vector>

namespace ec_prv {
namespace url_shortener {
namespace web {
namespace {
// Validates path of a requested static file. Protects against malicious input.
bool validate_static_file_request_path(std::string_view input) {
  return input.length() < max_file_path_length &&
         input.find("//") == std::string_view::npos &&
         input.find("..") == std::string_view::npos;
}

auto file_is_under_dir(const std::filesystem::path &this_file,
                       const std::filesystem::path &is_under_this_dir) -> bool {
  auto parent_it = is_under_this_dir.begin();
  auto this_file_it = this_file.begin();
  for (;
       parent_it != is_under_this_dir.end() && this_file_it != this_file.end();
       ++parent_it, ++this_file_it) {
    if (*parent_it != *this_file_it && *parent_it != "") {
      return false;
    }
  }
  return parent_it == is_under_this_dir.end();
}
} // namespace

StaticHandler::StaticHandler(std::weak_ptr<StaticFileCache> cache,
                             const std::filesystem::path &doc_root)
    : cache_(cache), doc_root_(doc_root) {}

auto StaticHandler::expected_file_path(
    const proxygen::HTTPMessage *request,
    const std::filesystem::path &doc_root) noexcept -> std::filesystem::path {
  // remove "/static/" prefix from request path
  // auto req_path = request->getPathAsStringPiece();
  auto req_path = request->getPath();
  auto p = req_path.rfind(static_files_url_prefix, 0);
  // "/static/" is not found as a prefix or at all
  if (p != 0) {
    return {};
  }
  std::filesystem::path dst =
      doc_root /
      std::filesystem::path{req_path.begin() + static_files_url_prefix.size(),
                            req_path.end()};
  DLOG(INFO) << "Resolving URL request \"" << request->getPath()
             << "\" as file path \"" << dst.string() << "\"";
  return dst;
}

void StaticHandler::onRequest(
    std::unique_ptr<proxygen::HTTPMessage> request) noexcept {
  if (request->getMethod() != proxygen::HTTPMethod::GET) {
    proxygen::ResponseBuilder(downstream_)
        .status(405, "Method Not Allowed")
        .sendWithEOM();
    return;
  }
  if (!validate_static_file_request_path(request->getPath())) {
    DLOG(INFO) << "malicious input detected";
    sendBadRequestError("malicious input detected");
    return;
  }
  // Check a reconstructed path from the request headers is beneath the
  // specified doc root. Otherwise, request is in error or malicious.
  CHECK(request.get() != nullptr);
  auto file_path_should_be =
      StaticHandler::expected_file_path(request.get(), doc_root_);
  if (!file_is_under_dir(file_path_should_be, doc_root_)) {
    DLOG(INFO) << "potential malicious input";
    sendBadRequestError("malicious input detected in request for static asset");
    return;
  }
  // cache lookup here; early exit possible
  auto this_cache = cache_.lock();
  if (this_cache->exists(file_path_should_be)) {
    DLOG(INFO) << "Found " << file_path_should_be
               << " in the cache. Exiting early!";
    auto cached_file = this_cache->get(file_path_should_be);
    proxygen::ResponseBuilder(downstream_)
        .status(200, "OK")
        .body(std::move(cached_file))
        .sendWithEOM();
    return;
  }
  try {
    file_ = std::make_unique<folly::File>(file_path_should_be);
  } catch (const std::system_error &err) {
    proxygen::ResponseBuilder(downstream_)
        .status(404, "Not Found")
        .sendWithEOM();
    return;
  }
  requested_file_path_ = file_path_should_be;
  proxygen::ResponseBuilder(downstream_).status(200, "OK").send();
  read_file_scheduled_ = true;
  folly::getGlobalCPUExecutor()->add(
      std::bind(&StaticHandler::read_file, this,
                folly::EventBaseManager::get()->getEventBase()));
}

void StaticHandler::read_file(folly::EventBase *evb) {
  folly::IOBufQueue buf;
  // TODO(zds): implement timeout on reading files?
  while (file_ && !paused_) {
    auto data = buf.preallocate(4000, 4000);
    auto rc = folly::readNoInt(file_->fd(), data.first, data.second);
    if (rc < 0) {
      VLOG(4) << "file read error: " << rc;
      file_.reset();
      evb->runInEventBaseThread([this] { downstream_->sendAbort(); });
      break;
    } else if (rc == 0) {
      // found file EOM
      // done
      file_.reset();
      VLOG(4) << "File EOF found. Sending HTTP response.";
      evb->runInEventBaseThread(
          [this] { proxygen::ResponseBuilder(downstream_).sendWithEOM(); });
    } else {
      buf.postallocate(rc);
      evb->runInEventBaseThread([this, body = buf.move()]() mutable {
        // saving file body to cache
        CHECK(!requested_file_path_.empty());
        auto this_cache = cache_.lock();
        this_cache->append_data(requested_file_path_, body->coalesce());
        ;
        // stream response
        proxygen::ResponseBuilder(downstream_).body(std::move(body)).send();
      });
    }
  }
  // notify the thread processing a HTTP request that reading file is done
  evb->runInEventBaseThread([this] {
    read_file_scheduled_ = false;
    if (!check_for_completion() && !paused_) {
      VLOG(4) << "Resuming deferred read_file";
      onEgressResumed();
    }
  });
}

void StaticHandler::onEgressPaused() noexcept {
  VLOG(4) << "StaticHandler paused";
  paused_ = true;
}

void StaticHandler::onEgressResumed() noexcept {
  VLOG(4) << "StaticHandler resumed";
  paused_ = false;
  if (read_file_scheduled_ == false && file_) {
    // still need to read file
    folly::getGlobalCPUExecutor()->add(
        std::bind(&StaticHandler::read_file, this,
                  folly::EventBaseManager::get()->getEventBase()));
  } else {
    VLOG(4) << "deferred scheduling of StaticHandler::read_file";
  }
}

void StaticHandler::onBody(std::unique_ptr<folly::IOBuf> body) noexcept {
  // not applicable because `StaticHandler` only supports GET
}

void StaticHandler::onUpgrade(proxygen::UpgradeProtocol /* prot */) noexcept {
  // not applicable
}

void StaticHandler::onEOM() noexcept {
  // don't care; nothing to do because this sends data, not receives
}

void StaticHandler::requestComplete() noexcept {
  VLOG(3) << "proxygen static handler request complete";
  finished_ = true;
  paused_ = true;
  check_for_completion();
}

void StaticHandler::onError(proxygen::ProxygenError e) noexcept {
  VLOG(3) << "proxygen static handler onError: " << e;
  finished_ = true;
  paused_ = true;
  check_for_completion();
}

bool StaticHandler::check_for_completion() {
  if (finished_ && !read_file_scheduled_) {
    VLOG(4) << "deleting StaticHandler";
    delete this;
    return true;
  }
  return false;
}

void StaticHandler::sendError(const std::string &what) noexcept {
  proxygen::ResponseBuilder(downstream_).status(500, what).sendWithEOM();
}

void StaticHandler::sendBadRequestError(const std::string &what) noexcept {
  proxygen::ResponseBuilder(downstream_).status(400, what).sendWithEOM();
}

auto StaticFileCache::get_copy(const std::filesystem::path &file_path) const
    -> std::vector<unsigned char> {
  auto it = cache_.find(file_path.string());
  if (it == cache_.end()) {
    return {};
  }
  return it->second;
}

auto StaticFileCache::exists(const std::filesystem::path &file_path) const
    -> bool {
  return cache_.contains(file_path.string());
}

auto StaticFileCache::append_data(const std::filesystem::path &file_path,
                                  folly::ByteRange buf) -> void {
  auto it = cache_.find(file_path.string());
  if (it == cache_.end()) {
    DLOG(INFO) << "Attempt to stream file data into the cache for a file that "
                  "is not in the cache. "
               << "It should be in the cache for this function to be called, "
                  "so something is wrong.";
    std::vector<unsigned char> data;
    data.reserve(buf.size());
    data.insert(data.end(), buf.begin(), buf.end());
    cache_.emplace(std::piecewise_construct,
                   std::forward_as_tuple(file_path.string()),
                   std::forward_as_tuple(std::move(data)));
    return;
  }
  std::vector<unsigned char> &data = it->second;
  data.insert(data.end(), buf.begin(), buf.end());
}

auto StaticFileCache::set(const std::filesystem::path &file_path,
                          std::string_view body) -> void {
  std::vector<unsigned char> data;
  data.reserve(body.size());
  for (auto ch : body) {
    data.push_back(static_cast<unsigned char>(ch));
  }
  auto res = cache_.emplace(std::piecewise_construct,
                            std::forward_as_tuple(file_path.string()),
                            std::forward_as_tuple(std::move(data)));
  if (!res.second) {
    DLOG(INFO) << "no insertion happend when inserting into static file cache "
                  "the path \""
               << file_path << "\" with body: \"" << body << "\"";
  }
}

auto StaticFileCache::get(const std::filesystem::path &file_path) const
    -> std::unique_ptr<folly::IOBuf> {
  auto it = cache_.find(file_path.string());
  if (it == cache_.end()) {
    return {};
  }
  const std::vector<unsigned char> &underlying = it->second;
  return folly::IOBuf::wrapBuffer(underlying.data(), underlying.size());
}

} // namespace web
} // namespace url_shortener
} // namespace ec_prv

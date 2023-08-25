#include "static_handler.h"

#include <folly/FileUtil.h>
#include <folly/executors/GlobalExecutor.h>
#include <folly/io/async/EventBaseManager.h>
#include <glog/logging.h>
#include <proxygen/httpserver/RequestHandler.h>
#include <proxygen/httpserver/ResponseBuilder.h>
#include <string>
#include <string_view>

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
} // namespace

void StaticHandler::onRequest(
    std::unique_ptr<proxygen::HTTPMessage> request) noexcept {
  if (request->getMethod() != proxygen::HTTPMethod::GET) {
    sendBadRequestError("bad method");
    return;
  }
  if (!validate_static_file_request_path(request->getPath())) {
    sendBadRequestError("malicious input detected");
    return;
  }
  try {
    file_ = std::make_unique<folly::File>(
        request->getPathAsStringPiece().subpiece(1));
  } catch (const std::system_error &err) {
    proxygen::ResponseBuilder(downstream_)
        .status(404, "Not Found")
        .body("Could not find this static file")
        .sendWithEOM();
    return;
  }
  proxygen::ResponseBuilder(downstream_).status(200, "OK").send();
  read_file_scheduled_ = true;
  folly::getUnsafeMutableGlobalCPUExecutor()->add(
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
    folly::getUnsafeMutableGlobalCPUExecutor()->add(
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
} // namespace web
} // namespace url_shortener
} // namespace ec_prv

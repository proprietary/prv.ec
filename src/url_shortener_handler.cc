#include "url_shortener_handler.h"

#include <boost/algorithm/string.hpp>
#include <folly/portability/GFlags.h>
#include <proxygen/httpserver/RequestHandler.h>
#include <proxygen/httpserver/ResponseBuilder.h>
#include <memory>
#include <string>

#include "echo_stats.h"
#include "url_shortening.h"

namespace ec_prv {
  namespace url_shortener {
    namespace web {

      // TODO(zds): Read directly from rocksdb PinnableSlice and write the same buffer to the wire.
      // TODO(zds): Consider adding functionality to make this a pastebin as well.

      // TODO(zds): Put RocksDB operations in its own threadpool

      using namespace ::ec_prv::url_shortener;
      
      UrlShortenerApiRequestHandler::UrlShortenerApiRequestHandler(db::ShortenedUrlsDatabase* db, const uint64_t* highwayhash_key)
	:db_(db), highwayhash_key_(highwayhash_key) {}
      
      void UrlShortenerApiRequestHandler::onRequest(std::unique_ptr<proxygen::HTTPMessage> req) noexcept {
	DLOG(INFO) << "Request from " << req->getClientIP() << " for path: " << req->getPath();
	request_ = std::move(req);
      }

      void UrlShortenerApiRequestHandler::onEOM() noexcept {
	// meat of the business logic is here...

	if (request_->getMethod() == proxygen::HTTPMethod::GET) {
	  // Retrieve the full URL associated with a short slug and redirect
	  auto short_url = ec_prv::url_shortener::url_shortening::parse_out_request_str(request_->getPath());
	  if (short_url.length() == 0) {
	    sendError("empty");
	    return;
	  } else {
	    DLOG(INFO) << "looking up short url: " << short_url;
	    auto res = db_->get(short_url);
	    if (std::holds_alternative<db::UrlShorteningDbError>(res)) {
	      auto& err = *std::get_if<db::UrlShorteningDbError>(&res);
	      if (err == db::UrlShorteningDbError::NotFound) {
		proxygen::ResponseBuilder(downstream_).status(404, "Not Found").sendWithEOM();
		return;
	      } else {
		proxygen::ResponseBuilder(downstream_).status(500, "Internal Server Error").sendWithEOM();
		return;
	      }
	    }
	    const std::string* long_url = std::get_if<std::string>(&res);
	    CHECK(long_url != nullptr);
	    proxygen::ResponseBuilder(downstream_)
	      .status(301, "Moved Permanently")
	      .header(proxygen::HTTPHeaderCode::HTTP_HEADER_LOCATION, *long_url)
	      .sendWithEOM();
	  }
	} else if (request_->getMethod() == proxygen::HTTPMethod::POST || request_->getMethod() == proxygen::HTTPMethod::PUT) {
	  // Shorten a new URL
	  // auto long_url = request_->getQueryParam("url");
	  auto body_bytes = body_->coalesce();
	  auto request_body = std::string(body_bytes.begin(), body_bytes.end());
	  boost::algorithm::trim(request_body);
	  VLOG(1) << "request from " << request_->getClientIP() << " to shorten this long URL: " << request_body;
	  if (request_body.length() == 0) {
	    DLOG(INFO) << "Empty request body";
	    sendErrorBadRequest("empty");
	  } else if (request_body.length() > 10000) {
	    DLOG(INFO) << "malicious input";
	    sendErrorBadRequest("malformed");
	  } else {
	    DLOG(INFO) << "shortening url";
	    auto generated_short_url = ec_prv::url_shortener::url_shortening::generate_shortened_url(request_body, highwayhash_key_);
	    db_->put(generated_short_url, request_body);
	    // auto full_url = std::getenv("EC_PRV_URL_SHORTENER__BASE_URL") + generated_short_url;
	    proxygen::ResponseBuilder(downstream_).status(200, "OK").body(generated_short_url).sendWithEOM();
	  }
	} else {
	  sendErrorBadRequest("wrong method");
	}
      }

      void UrlShortenerApiRequestHandler::onBody(std::unique_ptr<folly::IOBuf> body) noexcept {
	if (body_) {
	  body_->prependChain(std::move(body));
	} else {
	  body_ = std::move(body);
	}
      }

      void UrlShortenerApiRequestHandler::onUpgrade(proxygen::UpgradeProtocol /* proto */) noexcept {
	// not applicable
      }

      void UrlShortenerApiRequestHandler::requestComplete() noexcept {
	delete this;
      }

      void UrlShortenerApiRequestHandler::onError(proxygen::ProxygenError err) noexcept {
	DLOG(INFO) << "proxygen onError";
	delete this;
      }

      void UrlShortenerApiRequestHandler::sendError(const std::string& what) noexcept {
	VLOG(3) << "Send error: " << what;
	proxygen::ResponseBuilder(downstream_).status(500, "Internal Server Error").sendWithEOM();
      }

      void UrlShortenerApiRequestHandler::sendErrorBadRequest(const std::string& what) noexcept {
	VLOG(3) << "bad request: " << what;
	proxygen::ResponseBuilder(downstream_).status(400, "Bad Request").sendWithEOM();
      }
    } // namespace web
  } // namespace url_shortener
} // namespace ec_prv

#pragma once

#include <folly/Memory.h>
#include <proxygen/httpserver/RequestHandler.h>
#include <string>

#include "db.h"

namespace proxygen {
class ResponseHandler;
}

namespace ec_prv {
  namespace url_shortener {
    namespace web {
      class UrlShortenerApiRequestHandler : public proxygen::RequestHandler {
      public:
	explicit UrlShortenerApiRequestHandler(::ec_prv::url_shortener::db::ShortenedUrlsDatabase* db, const uint64_t* highwayhash_key);

	void onRequest(std::unique_ptr<proxygen::HTTPMessage> request) noexcept override;

	void onBody(std::unique_ptr<folly::IOBuf> body) noexcept override;

	void onEOM() noexcept override;

	void onUpgrade(proxygen::UpgradeProtocol proto) noexcept override;

	void requestComplete() noexcept override;

	void onError(proxygen::ProxygenError err) noexcept override;
      private:
	void sendError(const std::string& what) noexcept;
	void sendErrorBadRequest(const std::string& what) noexcept;
	
	ec_prv::url_shortener::db::ShortenedUrlsDatabase* const db_;
	const uint64_t* highwayhash_key_;

	std::unique_ptr<proxygen::HTTPMessage> request_;
	std::unique_ptr<folly::IOBuf> body_;
      };
    } // namespace web
  } // namespace url_shortener
} // namesapce ec_prv

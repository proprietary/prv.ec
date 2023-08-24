#ifndef _INCLUDE_EC_PRV_URL_SHORTENER_WEB_STATIC_HANDLER_H
#define _INCLUDE_EC_PRV_URL_SHORTENER_WEB_STATIC_HANDLER_H

#include <proxygen/httpserver/RequestHandler.h>
#include <string>
#include <memory>
#include <string_view>
#include <folly/File.h>
#include <folly/Memory.h>
#include <folly/container/F14Map.h>

namespace proxygen {
class ResponseHandler;
}

namespace ec_prv {
  namespace url_shortener {
    namespace web {
      static constexpr int max_file_path_length = 1000;

      // Cache static files in memory to serve them quickly.
      class StaticFileCache {
      public:
	auto exists(std::string_view) const -> bool;
	auto get(std::string_view) -> const std::string;
	auto set(const std::string& file_path, const std::string& body) -> void;
      private:
	folly::F14FastMap<std::string, std::string> cache_;
      };
      
      class StaticHandler : public proxygen::RequestHandler {
      public:
	void onRequest(std::unique_ptr<proxygen::HTTPMessage> request) noexcept override;

	void onBody(std::unique_ptr<folly::IOBuf> body) noexcept override;

	void onUpgrade(proxygen::UpgradeProtocol) noexcept override;

	void onEOM() noexcept override;

	void requestComplete() noexcept override;

	void onError(proxygen::ProxygenError) noexcept override;

	void onEgressPaused() noexcept override;

	void onEgressResumed() noexcept override;

      private:
	void read_file(folly::EventBase* evb);
	bool check_for_completion();
	void sendBadRequestError(const std::string& what) noexcept;
	void sendError(const std::string& what) noexcept;

	std::unique_ptr<folly::File> file_;
	bool read_file_scheduled_{false};
	std::atomic<bool> paused_{false};
	bool finished_{false};

	// TODO(zds): integrate cache
	const StaticFileCache* cache_{nullptr};
      };
    } // namespace web
  } // namespace url_shortener
} // namesapce ec_prv
#endif // _INCLUDE_EC_PRV_URL_SHORTENER_WEB_STATIC_HANDLER_H

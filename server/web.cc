#include "server/web.h"
#include "b64/b64.h"
#include "b66/marshal_int.h"
#include "url_index/url_index.h"
#include "server/index.html_generated.h"
#include "server/bundle.js_generated.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <uWebSockets/HttpContext.h>
#include <uWebSockets/HttpParser.h>
#include <uWebSockets/HttpResponse.h>
#include <utility>
#include <vector>

using namespace std::literals;

namespace {

static const char BASE_66_CHARS[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.~";

auto parse_auth_header(std::string_view authorization_header,
		       std::string_view strip_prefix = "Basic "sv) -> std::string {
	using namespace std::literals;

	if (authorization_header.size() < strip_prefix.size()) {
		return {};
	}
	auto const* a = authorization_header.begin() + strip_prefix.size();
	std::string_view token{a, authorization_header.end()};
	auto decoded = ::ec_prv::b64::dec(token);
	return std::string(reinterpret_cast<char const*>(decoded.data()), decoded.size());
}

auto parse_http_basic_auth_header(std::string_view authorization_header)
    -> std::pair<std::string, std::string> {
	auto token = parse_auth_header(authorization_header, "Basic "sv);
	if (token.length() == 0) {
		return {};
	}
	auto idx = token.find(':');
	if (idx == std::string::npos) {
		return {};
	}
	auto user = token.substr(0, idx);
	if (idx + 1 > token.size()) {
		return {};
	}
	auto pass = token.substr(idx + 1);
	return std::make_pair(user, pass);
}

// auto parse_shorturl(std::string_view url) -> std::string_view {
// 	auto it = url.begin();
// 	if (*(it++) != '/') {
// 		// unexpected
// 		assert(false);
// 		return {};
// 	}
// 	auto shorturl_start = it;
// 	while (it != url.end()) {
// 		// validate that all characters exist in base64 alphabet
// 		auto i = sizeof(BASE_66_CHARS);
// 		while (i > 0) {
// 			if (BASE_66_CHARS[i] == *it) {
// 				break;
// 			}
// 			i--;
// 		}
// 		if (i == 0) {
// 			// fail
// 			// character not found in base64 alphabet
// 			return {};
// 		}
// 		it++;
// 	}
// 	auto shorturl_end = it;
// 	return std::string_view(shorturl_start, shorturl_end);
// }

} // namespace

namespace ec_prv {
namespace web {
using ::ec_prv::b64::enc;
using ::flatbuffers::FlatBufferBuilder;

Server::Server(int const port)
    : port_{port}, store_{::ec_prv::db::KVStore::open_default()}, svc_{&store_},
      rpc_user_{::getenv("EC_PRV_RPC_USER")}, rpc_pass_{::getenv("EC_PRV_RPC_PASS")} {
	if (rpc_user_.length() == 0) {
		throw std::runtime_error{"EC_PRV_RPC_USER environment variable not set"};
	}
	if (rpc_pass_.length() == 0) {
		throw std::runtime_error{"EC_PRV_RPC_PASS environment variable not set"};
	}
}

void Server::run() {
	using namespace std::literals;
	uWS::App()
		.get("/dist/bundle.js", [](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) -> void {
			res->onAborted([]() -> void {});
			res->writeHeader("Content-Type", "text/html");
			res->end(std::string_view{reinterpret_cast<const char*>(&bundle_js[0]), bundle_js_len});
		})
	    .get("/",
			 [](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) -> void {
				 res->onAborted([]() -> void {});
				 res->writeHeader("Content-Type", "text/html");
				 res->end(std::string_view{reinterpret_cast<const char*>(&index_html[0]), index_html_len});
			 })
	    .post("/accept",
		  [this](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) -> void {
			  // TODO: cork sockets
			  res->writeHeader("Access-Control-Allow-Origin", "*");
			  res->onAborted([]() -> void {});
			  auto buf = std::make_shared<std::vector<uint8_t>>();
			  buf->reserve(1 << 20);

			  auto rpc_method_name = req->getHeader("x-rpc-method");

			  if (rpc_method_name == "lookup_request_web") {
				  accept_rpc<fbs::LookupRequestWeb, false>(res, req, buf);
			  } else if (rpc_method_name == "shortening_request") {
				  accept_rpc<fbs::ShorteningRequest, false>(res, req, buf);
			  } else if (rpc_method_name == "lookup_request") {
				  accept_rpc<fbs::LookupRequest, false>(res, req, buf);
			  } else {
				  res->writeStatus("400 Bad Request");
				  res->end();
				  return;
			  }
		  })
	    .post("/shortening_request",
		  [this](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
	// clang-format off
#if DEBUG == 1
			  res->writeHeader("Access-Control-Allow-Origin", "https://prv.ec");
#endif // DEBUG == 1
       // clang-format on

			  res->onAborted([]() -> void {});

			  // Request a new shortened URL
			  auto buf = std::make_shared<std::vector<uint8_t>>();
			  buf->reserve(1 << 20);
			  accept_rpc<::ec_prv::fbs::ShorteningRequest, false>(res, req, buf);
		  })
	    .post("/lookup_request",
		  [this](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
			  res->onAborted([]() -> void {});
		// clang-format off
#if DEBUG == 1
			  res->writeHeader("Access-Control-Allow-Origin", "https://prv.ec");
#endif // DEBUG == 1
			  //clang-format on

			  auto buf = std::make_shared<std::vector<uint8_t>>();
			  buf->reserve(1 << 20);
			  accept_rpc<::ec_prv::fbs::LookupRequest, false>(res, req, buf);
		  })
		.post("/lookup_web", [this](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) -> void {
			// Shortcut method for web clients to do lookups.
			// Client provides a base-66 encoded string that
			// identifies the url index, server sends the lookup
			// response; no need for the client to generate a
			// LookupRequest flatbuffer on the client.
			
			res->onAborted([]() -> void {});
			auto buf = std::make_shared<std::vector<uint8_t>>();
			accept_rpc<::ec_prv::fbs::LookupRequestWeb, false>(res, req, buf);
			// res->onData([this, buf, res](std::string_view chunk, bool is_end) -> void {
			// 	// TODO: cork socket calls
			// 	buf->insert(buf->end(), chunk.begin(), chunk.end());
			// 	if (is_end) {
			// 		constexpr size_t MAX_LEN_OF_STRINGIFIED_URL_INDEX = 10;
			// 		if (buf.size() > MAX_LEN_OF_STRINGIFIED_URL_INDEX) {
			// 			res->end();
			// 			return;
			// 		}
			// 		res->writeHeader("Content-Type",
			// 						 "application/octet-stream");
			// 		res->writeStatus("200 OK");
			// 		::flatbuffers::FlatBufferBuilder fbb;
			// 		// deserialize base-66 encoded url index
			// 		auto ui = ::ec_prv::url_index::URLIndex::from_base_66_string(buf);
			// 		::ec_prv::fbs::LookupRequestBuilder lrb{fbb};
			// 		lrb.add_version(1);
			// 		lrb.add_url_index(ui.as_integer());
			// 		auto lr = lrb.Finish();
			// 		fbb.Finish(lr);
			// 		auto req = ::ec_prv::fbs::UnPackLookupRequest(fbb.GetBufferPointer());
			// 		auto lookup_response = svc_->handle(req);
			// 		std::string_view output{reinterpret_cast<char const*>(lookup_response.data()), lookup_response.size()};
			// 		res->end(output);
			// 	}
			// });
		})
	    .post("/trusted_shortening_request",
		[this](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
			res->onAborted([]() -> void {});
			// Authenticated endpoint for trusted clients performing all crypto
			// server-side.
			res->writeHeader("Access-Control-Allow-Origin", "*");
			auto authorization = req->getHeader("authorization");
			auto [user, pass] = parse_http_basic_auth_header(authorization);
			if (user != this->rpc_user_ && pass != this->rpc_pass_) {
				res->writeStatus("403 Forbidden");
				res->writeHeader("Content-Type", "text/plain");
				res->end();
				return;
			}
			auto buf = std::make_shared<std::vector<uint8_t>>();
			buf->reserve(1 << 20);
			accept_rpc<::ec_prv::fbs::TrustedShorteningRequest, false>(res, req, buf);
		})
	    .post(
		"/trusted_lookup_request",
		[this](auto* res, auto* req) {
			res->onAborted([]() -> void {});
			res->writeHeader("Access-Control-Allow-Origin", "*");
			auto authorization = req->getHeader("authorization");
			auto [user, pass] = parse_http_basic_auth_header(authorization);
			if (user != "user" && pass != "dummy") {
				res->writeStatus("403 Forbidden");
				res->writeHeader("Content-Type", "text/plain");
				res->end();
				return;
			}
			auto buf = std::make_shared<std::vector<uint8_t>>();
			buf->reserve(1 << 20);
			accept_rpc<::ec_prv::fbs::TrustedLookupRequest, false>(res, req, buf);
		})
		.get("/*", [](auto* res, auto* req) -> void {
			res->onAborted([]() -> void {});
			// serve index.html always, SPA-style
			res->writeHeader("Content-Type", "text/html");
			res->end(std::string_view{reinterpret_cast<const char*>(&index_html[0]), index_html_len});
			// auto url = req->getUrl();
			// auto identifier = parse_shorturl(url);
			// uint32_t identifier_parsed = ec_prv::b66::unmarshal(identifier);
			// if (identifier_parsed == 0) {
			// 	res->cork([res]() -> void {
			// 		res->writeStatus("302 Found");
			// 		res->writeHeader("location", "https://prv.ec");
			// 		res->end();
			// 	});
			// 	return;
			// }
			// auto ui = url_index::URLIndex::from_integer(identifier_parsed);
			// // lookup in rocksdb
			// rocksdb::PinnableSlice o;
			// auto status = this->store_.get(o, ui);
			// if (!status.ok() || o.empty()) {
			// 	res->cork([res]() -> void {
			// 		res->writeStatus("302 Found");
			// 		res->writeHeader("location", "https://prv.ec");
			// 		res->end();
			// 	});
			// 	return;
			// }
			// FlatBufferBuilder resp_fbb;
			// ::ec_prv::fbs::LookupResponseBuilder lrb{resp_fbb};
			// lrb.add_version(1);
			// lrb.add_error(false);
			// lrb.add_data(resp_fbb.CreateVector(reinterpret_cast<uint8_t const*>(o.data()), o.size()));
			// resp_fbb.Finish(lrb.Finish());
			// auto const encoded = enc(std::span{reinterpret_cast<uint8_t*>(resp_fbb.GetBufferPointer()), resp_fbb.GetSize()});
			// res->cork([res, &encoded]() -> void {
			// 	res->writeStatus("200 OK");
			// 	res->writeHeader("content-type", "text/html");
			// 	res->write(R"(<!doctype html><html><body><script>var a=")");
			// 	res->write(encoded);
			// 	// TODO(zds): write template abstraction
			// 	res->write(R"("; window.addEventListener('DOMContentLoaded', function() { window.location.replace(decryptLookupResponse(a)); });</script></body></html>)");
			// 	res->end();
			// });
		})
	    .listen(8000,
		    [](auto* token) {
			    if (token) {
				    std::printf("Listening on port %d\n", 8000);
			    }
		    })
	    .run();
}


} // namespace web
} // namespace ec_prv

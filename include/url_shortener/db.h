#ifndef _INCLUDE_EC_PRV_URL_SHORTENER_DB_H
#define _INCLUDE_EC_PRV_URL_SHORTENER_DB_H

#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <rocksdb/slice.h>

#include <iostream>
#include <algorithm>
#include <cstdlib>
#include <string>
#include <string_view>
#include <memory>
#include <filesystem>

namespace ec_prv {
  namespace url_shortener {
    namespace db {
      class ShortenedUrlsDatabase {
      private:
	rocksdb::DB* rocksdb_;
	std::filesystem::path path_;
	explicit ShortenedUrlsDatabase(rocksdb::DB* rocksdb, std::filesystem::path path) :path_(path), rocksdb_(rocksdb) {}
	ShortenedUrlsDatabase() = default;
      public:
	~ShortenedUrlsDatabase();
	[[nodiscard]] static auto open() -> std::shared_ptr<ShortenedUrlsDatabase>;
	auto put(std::string_view shortened_url, std::string_view full_url) -> void;
	auto get(std::string_view shortened_url) -> std::string;
      };
    } // namespace db
  } // namespace url_shortener
} // namespace ec_prv

#endif // _INCLUDE_EC_PRV_URL_SHORTENER_DB_H

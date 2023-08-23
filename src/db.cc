#include "db.h"

//#include "absl/base/log_severity.h"
//#include "absl/flags/flag.h"
//#include "absl/log/globals.h"
//#include "absl/log/log.h"
#include <stdexcept>
#include <cstdlib>
#include <iostream>
#include <algorithm>
#include <cstdlib>
#include <string>
#include <string_view>
#include <filesystem>
#include <memory>

namespace ec_prv {
  namespace url_shortener {
    namespace db {

      namespace {
	auto shortened_urls_database_path() -> std::filesystem::path {
	  const char* s = std::getenv("EC_PRV_URL_SHORTENER__SHORTENED_URLS_DATABASE_PATH");
	  if (s == nullptr) {
	    throw std::runtime_error("Missing environment variable for `shortened URLs database path`");
	  }
	  std::filesystem::path path{s};
	  return path;
	}
      } // namespace

      
      auto ShortenedUrlsDatabase::open() -> std::shared_ptr<ShortenedUrlsDatabase> {
	auto db_path = shortened_urls_database_path();
	rocksdb::DB* db;
	rocksdb::Options options;
	options.create_if_missing = true;
	options.IncreaseParallelism();
	options.OptimizeLevelStyleCompaction();
	rocksdb::Status s = rocksdb::DB::Open(options, db_path.c_str(), &db);
	if (!s.ok()) {
	  //LOG(ERROR) << "Unable to open RocksDB database at \""
	  //<< db_path << "\" : " << s.ToString();
	  throw std::runtime_error{s.ToString()};
	}
	return std::shared_ptr<ShortenedUrlsDatabase>(new ShortenedUrlsDatabase{db, db_path});
      }

      ShortenedUrlsDatabase::~ShortenedUrlsDatabase() {
	if (rocksdb_ == nullptr) {
	  //LOG(ERROR) << "Attempt to delete RocksDB instance which is a nullptr";
	  return;
	}
	rocksdb::Status s = rocksdb_->Close();
	if (!s.ok()) {
	  //LOG(ERROR) << s.ToString();
	}
	delete rocksdb_;
      }

      auto ShortenedUrlsDatabase::put(std::string_view shortened_url, std::string_view full_url) -> void {
	auto write_opts = rocksdb::WriteOptions();
	rocksdb::Status s = rocksdb_->Put(write_opts, shortened_url, full_url);
	if (!s.ok()) {
	  //LOG(ERROR) << s.ToString();
	  throw std::runtime_error{s.ToString()};
	}
      }

      auto ShortenedUrlsDatabase::get(std::string_view shortened_url) -> std::string {
	std::string dst;
	auto read_opts = rocksdb::ReadOptions();
	rocksdb::Status s = rocksdb_->Get(read_opts, shortened_url, &dst);
	if (!s.ok()) {
	  //LOG(ERROR) << s.ToString();
	  throw std::runtime_error{s.ToString()};
	}
	return dst;
      }
    } // namespace db
  } // namespace url_shortener
} // namespace ec_prv

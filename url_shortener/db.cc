#include "db.h"

// #include "absl/base/log_severity.h"
// #include "absl/flags/flag.h"
// #include "absl/log/globals.h"
// #include "absl/log/log.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <glog/logging.h>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace ec_prv {
namespace url_shortener {
namespace db {

namespace {
auto shortened_urls_database_path_env() -> std::filesystem::path {
  const char *s =
      std::getenv("EC_PRV_URL_SHORTENER__SHORTENED_URLS_DATABASE_PATH");
  if (s == nullptr) {
    throw std::runtime_error(
        "Missing environment variable for `shortened URLs database path`");
  }
  LOG(INFO) << "Configured path for shortened urls database, a RocksDB "
               "database at path "
            << s;
  std::filesystem::path path{s};
  LOG(INFO) << "Full path of shortened urls RocksDB database: "
            << std::filesystem::absolute(path);
  return path;
}
} // namespace

auto ShortenedUrlsDatabase::open(std::filesystem::path db_path)
    -> std::shared_ptr<ShortenedUrlsDatabase> {
  rocksdb::DB *db;
  rocksdb::Options options;
  options.create_if_missing = true;
  options.IncreaseParallelism();
  options.OptimizeLevelStyleCompaction();
  rocksdb::Status s = rocksdb::DB::Open(options, db_path.c_str(), &db);
  if (!s.ok()) {
    DLOG(INFO) << s.ToString();
    LOG(ERROR) << "Unable to open RocksDB database at \"" << db_path
               << "\" : " << s.ToString();
    throw std::runtime_error{s.ToString()};
  }
  return std::shared_ptr<ShortenedUrlsDatabase>(
      new ShortenedUrlsDatabase{db, db_path});
}

ShortenedUrlsDatabase::~ShortenedUrlsDatabase() noexcept {
  if (rocksdb_ == nullptr) {
    // LOG(ERROR) << "Attempt to delete RocksDB instance which is a nullptr";
    return;
  }
  rocksdb::Status s = rocksdb_->Close();
  if (!s.ok()) {
    LOG(ERROR) << s.ToString();
  }
  delete rocksdb_;
}

auto ShortenedUrlsDatabase::put(std::string_view shortened_url,
                                std::string_view full_url) noexcept
    -> std::optional<UrlShorteningDbError> {
  auto write_opts = rocksdb::WriteOptions();
  DLOG(INFO) << "Putting shortened URL into RocksDB \"" << shortened_url
             << "\" -> \"" << full_url << "\"";
  rocksdb::Status s = rocksdb_->Put(write_opts, shortened_url, full_url);
  DLOG(INFO) << "RocksDB status after trying to put \"" << shortened_url
             << "\" into database: " << s.ToString();
  if (!s.ok()) {
    DLOG(INFO) << s.ToString();
    if (s.IsTryAgain()) {
      return UrlShorteningDbError::TryAgain;
    }
    if (s.IsIOError()) {
      return UrlShorteningDbError::IOError;
    }
    return UrlShorteningDbError::InternalRocksDbError;
  }
  return {};
}

auto ShortenedUrlsDatabase::get(std::string_view shortened_url) noexcept
    -> std::variant<std::string, UrlShorteningDbError> {
  DLOG(INFO) << "Getting from RocksDB database this slug: \"" << shortened_url
             << "\"";
  std::string dst;
  auto read_opts = rocksdb::ReadOptions();
  rocksdb::Status s = rocksdb_->Get(read_opts, shortened_url, &dst);
  DLOG(INFO) << "Got \"" << dst << "\" from RocksDB database using slug: \""
             << shortened_url << "\"";
  if (!s.ok()) {
    DLOG(INFO) << s.ToString();
    if (s.IsNotFound()) {
      return UrlShorteningDbError::NotFound;
    }
    if (s.IsIOError()) {
      return UrlShorteningDbError::IOError;
    }
    if (s.IsTryAgain()) {
      return UrlShorteningDbError::TryAgain;
    }
    return UrlShorteningDbError::InternalRocksDbError;
  }
  return dst;
  // TODO(zds): pass slice directly from memory to web server
}

auto ShortenedUrlsDatabase::get_fast(std::string *buf,
                                     std::string_view short_url) noexcept
    -> bool {
  rocksdb::Status s = rocksdb_->Get(
      read_options_, rocksdb_->DefaultColumnFamily(), short_url, buf);
  return s.ok();
}

} // namespace db
} // namespace url_shortener
} // namespace ec_prv

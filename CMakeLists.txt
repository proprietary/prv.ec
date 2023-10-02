cmake_minimum_required(VERSION 3.27)

project(url_shortener VERSION 1.0.0 DESCRIPTION "prv.ec url shortener" LANGUAGES CXX)

list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

find_package(Threads)
find_package(glog REQUIRED)
find_package(Gflags REQUIRED)
find_package(RocksDB REQUIRED)
#find_package(proxygen REQUIRED)
find_package(Folly REQUIRED)

add_subdirectory(third_party)

add_library(app_config url_shortener/app_config.h url_shortener/app_config.cc)
target_sources(app_config PUBLIC FILE_SET hdrs TYPE HEADERS FILES url_shortener/app_config.h)
target_compile_features(app_config PUBLIC cxx_std_20)
target_link_libraries(app_config PUBLIC Folly::folly yaml-cpp::yaml-cpp)

add_library(url_shortening)
target_sources(url_shortening PUBLIC url_shortener/url_shortening.cc url_shortener/db.cc url_shortener/db.h)
target_compile_features(url_shortening PUBLIC cxx_std_20)
target_link_libraries(url_shortening PUBLIC app_config highwayhash Folly::folly RocksDB::rocksdb)

add_executable(web_server)
target_sources(web_server PUBLIC url_shortener/web_server.cc url_shortener/url_shortener_handler.h url_shortener/url_shortener_handler.cc url_shortener/static_handler.h url_shortener/static_handler.cc url_shortener/make_url_request_handler.h url_shortener/make_url_request_handler.cc url_shortener/frontend_handler.h url_shortener/frontend_handler.cc url_shortener/ddos_protection.h url_shortener/ddos_protection.cc)
target_link_libraries(web_server PUBLIC proxygen proxygenhttpserver Folly::folly url_shortening app_config)

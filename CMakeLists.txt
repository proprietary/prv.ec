cmake_minimum_required(VERSION 3.27)

project(url_shortener VERSION 1.0.0 DESCRIPTION "prv.ec url shortener" LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(Boost_USE_STATIC_LIBS OFF)
set(Boost_USE_MULTITHREADED ON)
set(Boost_USE_STATIC_RUNTIME OFF)
find_package(Boost 1.83 REQUIRED COMPONENTS system)
message(STATUS "Boost version: ${Boost_VERSION}")

find_package(glog REQUIRED)

add_subdirectory(third_party)

#find_package(protobuf CONFIG REQUIRED)
#find_package(gRPC CONFIG REQUIRED)
find_package(Threads)

add_library(url_shortening)
target_sources(url_shortening PUBLIC src/url_shortening.cc src/db.cc src/db.h)
target_include_directories(url_shortening PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include/url_shortener" "${PROJECT_SOURCE_DIR}/third_party/rocksdb/include")
target_compile_features(url_shortening PUBLIC cxx_std_20)
target_link_libraries(url_shortening PUBLIC highwayhash absl::base absl::strings glog::glog)
target_link_libraries(url_shortening INTERFACE rocksdb)

#add_executable(server src/beast.cc)
#target_compile_features(server PUBLIC cxx_std_20)
#target_link_libraries(server PUBLIC url_shortening Boost::system Boost::boost pthread)

add_library(url_shortener_proto src/url_shortener.pb.cc src/url_shortener.pb.h src/url_shortener.grpc.pb.cc src/url_shortener.grpc.pb.h)
target_link_libraries(url_shortener_proto PUBLIC grpc++ absl::base absl::absl_log libprotobuf grpc++_reflection)

add_executable(rpc_server src/rpc_server.cc)
target_compile_features(rpc_server PUBLIC cxx_std_20)
target_link_libraries(rpc_server PUBLIC grpc++ absl::base absl::config absl::log absl::absl_log absl::log_streamer absl::check absl::absl_check absl::log_flags absl::log_globals absl::log_initialize absl::log_entry absl::synchronization absl::strings glog::glog url_shortening url_shortener_proto)

# TODO(zds): put logic to generate gRPC/protobufs in CMake
# https://github.com/grpc/grpc/blob/v1.57.0/examples/cpp/route_guide/CMakeLists.txt


include("third_party/proxygen/build/fbcode_builder/CMake/FindSodium.cmake")
find_package(proxygen REQUIRED)
find_package(Folly REQUIRED)
find_package(Gflags REQUIRED)
add_executable(web_server src/web_server.cc src/url_shortener_handler.h src/url_shortener_handler.cc src/static_handler.h src/static_handler.cc)
target_link_libraries(web_server PUBLIC proxygen::proxygen proxygen::proxygenhttpserver Folly::folly glog::glog url_shortening)

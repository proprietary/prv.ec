cmake_minimum_required(VERSION 3.27)

project(url_shortener VERSION 1.0.0 DESCRIPTION "prv.ec url shortener" LANGUAGES CXX)

set(Boost_USE_STATIC_LIBS OFF)
set(Boost_USE_MULTITHREADED ON)
set(Boost_USE_STATIC_RUNTIME OFF)
find_package(Boost 1.83 REQUIRED COMPONENTS system)
message(STATUS "Boost version: ${Boost_VERSION}")

add_subdirectory(third_party)

add_library(url_shortening)
target_sources(url_shortening PUBLIC include/url_shortener/url_shortening.h src/url_shortening.cc)
target_include_directories(url_shortening PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include/url_shortener)
target_compile_features(url_shortening PUBLIC cxx_std_20)
target_link_libraries(url_shortening PUBLIC highwayhash)

add_executable(server src/beast.cc)
target_compile_features(server PUBLIC cxx_std_20)
target_link_libraries(server PUBLIC url_shortening Boost::system Boost::boost pthread)

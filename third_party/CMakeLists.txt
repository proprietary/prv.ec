cmake_minimum_required(VERSION 3.27)

include(FetchContent)
# include(ExternalProject)

add_subdirectory(highwayhash)

FetchContent_Declare(
  proxygen
  URL https://github.com/facebook/proxygen/archive/4c5883da3c7c7940da6a0513ac7271d78534466e.zip
  URL_HASH SHA256=d0155e4c7d90cd4b50e0797dc183c33d2efff6411188876985cd2112aa400702
)
FetchContent_MakeAvailable(proxygen)

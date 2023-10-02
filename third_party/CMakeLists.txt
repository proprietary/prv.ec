cmake_minimum_required(VERSION 3.27)

include(FetchContent)

add_subdirectory(highwayhash)

FetchContent_Declare(
  proxygen
  URL https://github.com/facebook/proxygen/archive/4c5883da3c7c7940da6a0513ac7271d78534466e.zip
  URL_HASH SHA256=d0155e4c7d90cd4b50e0797dc183c33d2efff6411188876985cd2112aa400702
)
FetchContent_MakeAvailable(proxygen)

# FetchContent_Declare(folly
#   URL https://github.com/facebook/folly/releases/download/v2023.10.02.00/folly-v2023.10.02.00.zip
#   URL_HASH SHA256=94ae19de64f7d74f7f3ecff40b7a2cb70d074d95089e2bb4790ca95fb1c8610b
# )
# FetchContent_MakeAvailable(folly)

FetchContent_Declare(
  yaml-cpp
  GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
  GIT_TAG 0.8.0
)
FetchContent_MakeAvailable(yaml-cpp)
  

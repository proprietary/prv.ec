cmake_minimum_required(VERSION 3.27)

include(FetchContent)
include(ExternalProject)

add_subdirectory(highwayhash)
#file(highwayhash_headers GLOB ${CMAKE_CURRENT_LIST_DIR}/highwayhash/**/*.h)
#target_include_directories(highwayhash PUBLIC ${CMAKE_CURRENT_LIST_DIR}/highwayhash/highwayhash.h)


# ExternalProject_Add(absl
#   SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/abseil-cpp"
#   CMAKE_CACHE_ARGS
#     -DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=TRUE
#     -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_CURRENT_BINARY_DIR}/absl
# )
#FetchContent_MakeAvailable(absl)

#set(WITH_GFLAGS OFF)
#add_subdirectory(rocksdb EXCLUDE_FROM_ALL)

# https://github.com/apple/foundationdb/blob/main/cmake/CompileRocksDB.cmake
ExternalProject_Add(rocksdb_src
  PREFIX rocksdb_prefix
  SOURCE_DIR "${PROJECT_SOURCE_DIR}/third_party/rocksdb"
  CMAKE_CACHE_ARGS -DWITH_GFLAGS:BOOL=FALSE
  BUILD_BYPRODUCTS <BINARY_DIR>/librocksdb.a
  INSTALL_COMMAND ""
)
ExternalProject_Get_Property(rocksdb_src BINARY_DIR)
set(ROCKSDB_LIBRARIES "${BINARY_DIR}/librocksdb.a")
ExternalProject_Get_Property(rocksdb_src SOURCE_DIR)
set(ROCKSDB_INCLUDE_DIR "${SOURCE_DIR}/include")
set(ROCKSDB_FOUND TRUE)
mark_as_advanced(ROCKSDB_LIBRARIES ROCKSDB_INCLUDE_DIR)
add_library(rocksdb STATIC IMPORTED GLOBAL)
target_include_directories(rocksdb INTERFACE "${PROJECT_SOURCE_DIR}/third_party/rocksdb/include" "${ROCKSDB_INCLUDE_DIR}")
target_link_libraries(rocksdb INTERFACE "${ROCKSDB_LIBRARIES}")
set_target_properties(rocksdb PROPERTIES
			      IMPORTED_LOCATION "${ROCKSDB_LIBRARIES}"
			      INTERFACE_INCLUDE_DIRECTORIES "${ROCKSDB_INCLUDE_DIR}")
add_dependencies(rocksdb rocksdb_src)

# FetchContent_Declare(rocksdb
#   GIT_REPOSITORY https://github.com/facebook/rocksdb.git
#   GIT_TAG v8.3.2
#   GIT_SHALLOW TRUE
# )
# FetchContent_GetProperties(rocksdb)
# if(NOT rocksdb_POPULATED)
#   set(WITH_GFLAGS OFF CACHE INTERNAL "")
#   set(CMAKE_ENABLE_SHARED OFF CACHE INTERNAL "")
#   FetchContent_Populate(rocksdb)
#   add_subdirectory(${rocksdb_SOURCE_DIR} ${rocksdb_BINARY_DIR} EXCLUDE_FROM_ALL)
# endif()
# include_directories(${rocksdb_SOURCE_DIR}/include)

#add_subdirectory(grpc)

# ExternalProject_Add(gRPC
#   SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/grpc"
# )

#FetchContent_MakeAvailable(absl gRPC rocksdb)

set(ABSL_ENABLE_INSTALL ON)
#FetchContent_Declare(absl
#  GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
#  GIT_TAG 7aef7808d6dbe46ab95b37e6c67d1350c1da016b
#)
# FetchContent_Declare(
#   gRPC
#   GIT_REPOSITORY https://github.com/grpc/grpc
#   GIT_TAG        v1.57.0
# )
# set(FETCHCONTENT_QUIET OFF)
# FetchContent_MakeAvailable(gRPC)
# FetchContent_MakeAvailable(absl)
add_subdirectory(grpc)
#add_subdirectory(abseil-cpp)

# # Note: For all external projects, instead of using checked-out code, one could
# # specify GIT_REPOSITORY and GIT_TAG to have cmake download the dependency directly,
# # without needing to add a submodule to your project.

# # Builds absl project from the git submodule.
# ExternalProject_Add(absl
#   PREFIX absl
#   SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/grpc/third_party/abseil-cpp"
#   CMAKE_CACHE_ARGS
#         -DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=TRUE
#         -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_CURRENT_BINARY_DIR}/absl
# )

# # Builds absl project from the git submodule.
# ExternalProject_Add(utf8_range
#   PREFIX utf8_range
#   SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/grpc/third_party/utf8_range"
#   CMAKE_CACHE_ARGS
#         -DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=TRUE
#         -Dutf8_range_ENABLE_TESTS:BOOL=OFF
#         -Dabsl_DIR:STRING=${CMAKE_CURRENT_BINARY_DIR}/absl/lib/cmake/absl
#         -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_CURRENT_BINARY_DIR}/utf8_range
#   DEPENDS absl
# )

# # Builds c-ares project from the git submodule.
# ExternalProject_Add(c-ares
#   PREFIX c-ares
#   SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/grpc/third_party/cares/cares"
#   CMAKE_CACHE_ARGS
#         -DCARES_SHARED:BOOL=OFF
#         -DCARES_STATIC:BOOL=ON
#         -DCARES_STATIC_PIC:BOOL=ON
#         -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_CURRENT_BINARY_DIR}/c-ares
# )

# # Builds protobuf project from the git submodule.
# ExternalProject_Add(protobuf
#   PREFIX protobuf
#   SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/grpc/third_party/protobuf"
#   CMAKE_CACHE_ARGS
#         -Dprotobuf_BUILD_TESTS:BOOL=OFF
#         -Dprotobuf_WITH_ZLIB:BOOL=OFF
#         -Dprotobuf_ABSL_PROVIDER:STRING=package
#         -Dabsl_DIR:STRING=${CMAKE_CURRENT_BINARY_DIR}/absl/lib/cmake/absl
#         -Dutf8_range_DIR:STRING=${CMAKE_CURRENT_BINARY_DIR}/utf8_range/lib/cmake/utf8_range
#         -Dprotobuf_MSVC_STATIC_RUNTIME:BOOL=OFF
#         -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_CURRENT_BINARY_DIR}/protobuf
#   DEPENDS absl utf8_range
# )

# # Builds re2 project from the git submodule.
# ExternalProject_Add(re2
#   PREFIX re2
#   SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/grpc/third_party/re2"
#   CMAKE_CACHE_ARGS
#         -DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=TRUE
#         -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_CURRENT_BINARY_DIR}/re2
# )

# # Builds zlib project from the git submodule.
# ExternalProject_Add(zlib
#   PREFIX zlib
#   SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/grpc/third_party/zlib"
#   CMAKE_CACHE_ARGS
#         -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_CURRENT_BINARY_DIR}/zlib
# )

# # the location where protobuf-config.cmake will be installed varies by platform
# if (WIN32)
#   set(_FINDPACKAGE_PROTOBUF_CONFIG_DIR "${CMAKE_CURRENT_BINARY_DIR}/protobuf/cmake")
# else()
#   set(_FINDPACKAGE_PROTOBUF_CONFIG_DIR "${CMAKE_CURRENT_BINARY_DIR}/protobuf/lib/cmake/protobuf")
# endif()

# # if OPENSSL_ROOT_DIR is set, propagate that hint path to the external projects with OpenSSL dependency.
# set(_CMAKE_ARGS_OPENSSL_ROOT_DIR "")
# if (OPENSSL_ROOT_DIR)
#   set(_CMAKE_ARGS_OPENSSL_ROOT_DIR "-DOPENSSL_ROOT_DIR:PATH=${OPENSSL_ROOT_DIR}")
# endif()

# # Builds gRPC based on locally checked-out sources and set arguments so that all the dependencies
# # are correctly located.
# ExternalProject_Add(grpc
#   PREFIX grpc
#   SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/grpc"
#   CMAKE_CACHE_ARGS
#         -DgRPC_INSTALL:BOOL=ON
#         -DgRPC_BUILD_TESTS:BOOL=OFF
#         -DgRPC_BUILD_MSVC_MP_COUNT:STRING=-1
#         -Dutf8_range_DIR:STRING=${CMAKE_CURRENT_BINARY_DIR}/utf8_range/lib/cmake/utf8_range
#         -DgRPC_PROTOBUF_PROVIDER:STRING=package
#         -DProtobuf_DIR:PATH=${_FINDPACKAGE_PROTOBUF_CONFIG_DIR}
#         -DgRPC_RE2_PROVIDER:STRING=package
#         -Dre2_DIR:STRING=${CMAKE_CURRENT_BINARY_DIR}/re2/lib/cmake/re2
#         -DgRPC_ZLIB_PROVIDER:STRING=package
#         -DZLIB_ROOT:STRING=${CMAKE_CURRENT_BINARY_DIR}/zlib
#         -DgRPC_ABSL_PROVIDER:STRING=package
#         -Dabsl_DIR:STRING=${CMAKE_CURRENT_BINARY_DIR}/absl/lib/cmake/absl
#         -DgRPC_CARES_PROVIDER:STRING=package
#         -Dc-ares_DIR:PATH=${CMAKE_CURRENT_BINARY_DIR}/c-ares/lib/cmake/c-ares
#         -DgRPC_SSL_PROVIDER:STRING=package
#         ${_CMAKE_ARGS_OPENSSL_ROOT_DIR}
#         -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_CURRENT_BINARY_DIR}/grpc
#   DEPENDS c-ares protobuf re2 zlib absl
# )

cmake_minimum_required(VERSION 3.2)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/cmake/")

add_compile_options(-Wall -Wextra -pedantic -Werror -Wno-format)

project (adbd)

find_package(uv REQUIRED)

option(SYSTEM_ADB_AUTHENTICATION "adb authentication"    OFF)
option(SYSTEM_ADB_AUTH_PUBKEY    "adb auth public key"   OFF)
option(SYSTEM_ADB_FILE_SERVICE   "adb file sync service" OFF)
option(SYSTEM_ADB_SOCKET_SERVICE "adb socket service"    OFF)
option(SYSTEM_ADB_SHELL_SERVICE  "adb shell service"     OFF)

set (ADB_SRCS
	adb_main.c
	adb_client.c
	adb_frame.c
	dumphex.c
	hal/hal_uv.c
	hal/hal_uv_packet.c
	hal/hal_uv_client_tcp.c)

if(SYSTEM_ADB_AUTHENTICATION)
  add_definitions(-DCONFIG_SYSTEM_ADB_AUTHENTICATION=1)

  if (SYSTEM_ADB_AUTH_PUBKEY)
  	add_definitions(-DCONFIG_SYSTEM_ADB_AUTH_PUBKEY=1)
  endif()
  set(ADB_SRCS ${ADB_SRCS} adb_auth_key.c)
  set(ADB_SRCS ${ADB_SRCS} mincrypt/rsa.c mincrypt/sha.c mincrypt/sha256.c)
  # adb_auth.c)
endif()

if(SYSTEM_ADB_FILE_SERVICE)
  add_definitions(-DCONFIG_SYSTEM_ADB_FILE_SERVICE=1)
  set(ADB_SRCS ${ADB_SRCS} file_sync_service.c)
endif()

if(SYSTEM_ADB_SOCKET_SERVICE)
  add_definitions(-DCONFIG_SYSTEM_ADB_SOCKET_SERVICE=1)
  set(ADB_SRCS ${ADB_SRCS} tcp_service.c hal/hal_uv_socket.c)
endif()

if(SYSTEM_ADB_SHELL_SERVICE)
  add_definitions(-DCONFIG_SYSTEM_ADB_SHELL_SERVICE=1)
  set(ADB_SRCS ${ADB_SRCS} shell_service.c hal/hal_uv_pipe.c)
endif()

add_executable(adbd ${ADB_SRCS})

# SET_TARGET_PROPERTIES(adbd PROPERTIES COMPILE_FLAGS
# 	"-DFLAG_1")

target_include_directories(adbd PRIVATE "${CMAKE_SOURCE_DIR}")
target_link_libraries(adbd uv)

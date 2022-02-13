cmake_minimum_required(VERSION 3.2)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/cmake/")

add_compile_options(-Wall -Wextra -Werror -Wno-format)

project (adbd)

find_package(uv REQUIRED)

option(ADBD_AUTHENTICATION "adb authentication"    OFF)
option(ADBD_AUTH_PUBKEY    "adb auth public key"   OFF)
option(ADBD_FILE_SERVICE   "adb file sync service" ON)

option(ADBD_SHELL_SERVICE   "adb shell service" ON)
set(ADBD_SHELL_SERVICE_PATH "/bin/bash" CACHE STRING "")
set(ADBD_SHELL_SERVICE_CMD  "sh" CACHE STRING "")

set(ADBD_CNXN_PAYLOAD_SIZE "1024" CACHE STRING "")
set(ADBD_PAYLOAD_SIZE      "1024" CACHE STRING "")
set(ADBD_FRAME_MAX            "4" CACHE STRING "")
set(ADBD_TOKEN_SIZE          "20" CACHE STRING "")

set(ADBD_DEVICE_ID      "\"abcd\""         CACHE STRING "")
set(ADBD_PRODUCT_NAME   "\"adb_dev\""      CACHE STRING "")
set(ADBD_PRODUCT_MODEL  "\"adb_board\""    CACHE STRING "")
set(ADBD_PRODUCT_DEVICE "\"NuttX_device\"" CACHE STRING "")
set(ADBD_FEATURES       "\"cmd\""          CACHE STRING "")

set(ADBD_TCP_SERVER_PORT "5555" CACHE STRING "")

set (ADB_SRCS
  adb_main.c
  adb_banner.c
  adb_client.c
  adb_frame.c
  hal/hal_uv.c
  hal/hal_uv_packet.c
  hal/hal_uv_client_tcp.c)

if(ADBD_AUTHENTICATION)
  set(ADB_SRCS ${ADB_SRCS} adb_auth_key.c)
  # Add RSA library here
endif()

if(ADBD_FILE_SERVICE)
  set(ADB_SRCS ${ADB_SRCS} file_sync_service.c)
endif()

if(ADBD_SHELL_SERVICE)
  set(ADB_SRCS ${ADB_SRCS} hal/shell_service_uv.c)
endif()

add_executable(adbd ${ADB_SRCS})

target_compile_definitions(adbd PUBLIC -DCONFIG_ADBD_CNXN_PAYLOAD_SIZE=${ADBD_CNXN_PAYLOAD_SIZE})
target_compile_definitions(adbd PUBLIC -DCONFIG_ADBD_PAYLOAD_SIZE=${ADBD_PAYLOAD_SIZE})
target_compile_definitions(adbd PUBLIC -DCONFIG_ADBD_FRAME_MAX=${ADBD_FRAME_MAX})

target_compile_definitions(adbd PUBLIC -DCONFIG_ADBD_DEVICE_ID=${ADBD_DEVICE_ID})
target_compile_definitions(adbd PUBLIC -DCONFIG_ADBD_PRODUCT_NAME=${ADBD_PRODUCT_NAME})
target_compile_definitions(adbd PUBLIC -DCONFIG_ADBD_PRODUCT_MODEL=${ADBD_PRODUCT_MODEL})
target_compile_definitions(adbd PUBLIC -DCONFIG_ADBD_PRODUCT_DEVICE=${ADBD_PRODUCT_DEVICE})
target_compile_definitions(adbd PUBLIC -DCONFIG_ADBD_FEATURES=${ADBD_FEATURES})
target_compile_definitions(adbd PUBLIC -DCONFIG_ADBD_TCP_SERVER="y")
target_compile_definitions(adbd PUBLIC -DCONFIG_ADBD_TCP_SERVER_PORT=${ADBD_TCP_SERVER_PORT})

if(ADBD_AUTHENTICATION)
  target_compile_definitions(adbd PUBLIC -DCONFIG_ADBD_AUTHENTICATION=1)
  target_compile_definitions(adbd PUBLIC -DCONFIG_ADBD_TOKEN_SIZE=${ADBD_TOKEN_SIZE})

  if (ADBD_AUTH_PUBKEY)
  	target_compile_definitions(adbd PUBLIC -DCONFIG_ADBD_AUTH_PUBKEY=1)
  endif()
endif()

if(ADBD_FILE_SERVICE)
  target_compile_definitions(adbd PUBLIC -DCONFIG_ADBD_FILE_SERVICE=1)
endif()

if(ADBD_SHELL_SERVICE)
  target_compile_definitions(adbd PUBLIC -DCONFIG_ADBD_SHELL_SERVICE=1)
  target_compile_definitions(adbd PUBLIC
    -DCONFIG_ADBD_SHELL_SERVICE_PATH="${ADBD_SHELL_SERVICE_PATH}")
  target_compile_definitions(adbd PUBLIC
    -DCONFIG_ADBD_SHELL_SERVICE_CMD="${ADBD_SHELL_SERVICE_CMD}")
endif()

target_include_directories(adbd PRIVATE "${CMAKE_SOURCE_DIR}")
target_link_libraries(adbd uv)

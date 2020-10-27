# microADB

## Overview

microADB is a Android Debug Bridge daemon implementation focus on low memory footprint for tiny embedded applications and IOT.

https://developer.android.com/studio/command-line/adb?authuser=1

## Feature highlights

 * Single-thread based implementation

 * Event driven and asynchronous i/o

 * Both USB and TCP transports supported

## Licensing

microADB is licensed under the Apache license. Check the [LICENSE file](LICENSE).

## Build Instructions

To build with [CMake][]:

```bash
$ mkdir -p build

$ (cd build && cmake .. \
-DADBD_TCP_SERVER_PORT=5555 \
\
-DADBD_AUTHENTICATION=ON \
-DADBD_AUTH_PUBKEY=ON \
-DADBD_FILE_SERVICE=ON \
 \
-DADBD_CNXN_PAYLOAD_SIZE="1024" \
-DADBD_PAYLOAD_SIZE="64" \
-DADBD_FRAME_MAX="1" \
-DADBD_TOKEN_SIZE="20" \
 \
-DADBD_DEVICE_ID="\"abcd\"" \
-DADBD_PRODUCT_NAME="\"adb_dev\"" \
-DADBD_PRODUCT_MODEL="\"adb_board\"" \
-DADBD_PRODUCT_DEVICE="\"NuttX_device\"" \
-DADBD_FEATURES="\"cmd\"")

$ cmake --build build # add `-j <n>` with cmake >= 3.12

# Start adb deamon:
$ ./build/adbd
```

[CMake]: https://cmake.org/

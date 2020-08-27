############################################################################
# system/adb/Makefile
#
# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.  The
# ASF licenses this file to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance with the
# License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
# License for the specific language governing permissions and limitations
# under the License.
#
############################################################################

include $(APPDIR)/Make.defs

# adb server app

CONFIG_SYSTEM_ADB_PRIORITY ?= SCHED_PRIORITY_DEFAULT
CONFIG_SYSTEM_ADB_STACKSIZE ?= 2048
CONFIG_SYSTEM_ADB_PROGNAME ?= adbd$(EXEEXT)
PROGNAME = $(CONFIG_SYSTEM_ADB_PROGNAME)

PROGNAME  := $(CONFIG_SYSTEM_ADB_PROGNAME)
PRIORITY  := $(CONFIG_SYSTEM_ADB_PRIORITY)
STACKSIZE := $(CONFIG_SYSTEM_ADB_STACKSIZE)
MODULE    := $(CONFIG_SYSTEM_ADB)

# Files

MAINSRC := src/adb_main.c
CSRCS += src/adb_client.c
CSRCS += src/adb_frame.c

CSRCS += src/hal/hal_uv.c
CSRCS += src/dumphex.c

CFLAGS += -D__NUTTX__=1
CFLAGS += -Isrc
# CFLAGS += -DADB_HOST=0 -DADB_HOST_ON_TARGET=0

ifeq ($(CONFIG_SYSTEM_ADB_FILE_SERVICE),y)
CSRCS += src/file_sync_service.c
endif

ifeq ($(CONFIG_SYSTEM_ADB_SOCKET_SERVICE),y)
CSRCS += src/tcp_service.c
CSRCS += src/hal/hal_uv_socket.c
endif

ifeq ($(CONFIG_SYSTEM_ADB_SHELL_SERVICE),y)
CSRCS += src/shell_service.c
CSRCS += src/hal/hal_uv_pipe.c
endif

ifeq ($(CONFIG_SYSTEM_ADB_AUTHENTICATION),y)
CSRCS += src/adb_auth_key.c
CSRCS += src/mincrypt/rsa.c src/mincrypt/sha.c src/mincrypt/sha256.c
endif

include $(APPDIR)/Application.mk

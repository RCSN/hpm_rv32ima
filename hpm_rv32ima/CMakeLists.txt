# Copyright (c) 2023 HPMicro
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.13)


set(CONFIG_SDMMC 1)
set(CONFIG_FATFS 1)

find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE})

project(hpm_rv32ima)

sdk_compile_definitions(-DSD_FATFS_ENABLE=1)
sdk_compile_definitions(-DFF_CODE_PAGE=437)

sdk_compile_options(-Os)

sdk_inc(src)

# sdk_app_src(src/cache.c)
# sdk_app_src(src/dlm_ram.c)
sdk_app_src(src/hpm_rv32ima.c)
generate_ses_project()

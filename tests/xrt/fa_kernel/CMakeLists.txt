# SPDX-License-Identifier: Apache-2.0
# Copyright (C) 2019-2022 Xilinx, Inc. All rights reserved.
#
CMAKE_MINIMUM_REQUIRED(VERSION 3.0.0)
PROJECT(fa_kernel)
set(TESTNAME "fa_kernel")

include(../../CMake/utils.cmake)

add_executable(fa_xrt xrt.cpp)
target_link_libraries(fa_xrt PRIVATE ${xrt_coreutil_LIBRARY})

add_executable(fa_ocl ocl.cpp)
target_link_libraries(fa_ocl PRIVATE ${xrt_xilinxopencl_LIBRARY})
if (WIN32)
  set(OCL_ROOT c:/Xilinx/XRT/ext)
  set(OpenCL_INCLUDE_DIR ${OCL_ROOT}/include)
  find_library(OpenCL_LIBRARY
    NAMES OpenCL
    HINTS "${OCL_ROOT}/lib")

  target_include_directories(fa_ocl PUBLIC ${OpenCL_INCLUDE_DIR})
endif (WIN32)
target_compile_options(fa_ocl PUBLIC
  "-DCL_TARGET_OPENCL_VERSION=120"
  "-DCL_HPP_MINIMUM_OPENCL_VERSION=120"
  "-DCL_HPP_TARGET_OPENCL_VERSION=120"
  "-DCL_USE_DEPRECATED_OPENCL_1_2_APIS"
  )

if (NOT WIN32)
  target_link_libraries(fa_xrt PRIVATE ${uuid_LIBRARY} pthread)
  target_link_libraries(fa_ocl PRIVATE pthread)
endif(NOT WIN32)

install(TARGETS fa_xrt fa_ocl
  RUNTIME DESTINATION ${INSTALL_DIR}/${TESTNAME})

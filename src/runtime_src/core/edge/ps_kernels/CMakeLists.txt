# SPDX-License-Identifier: Apache-2.0
# Copyright (C) 2019-2022 Xilinx, Inc. All rights reserved.
# Copyright (C) 2022 Advanced Micro Devices, Inc. All rights reserved.
if (DEFINED XRT_AIE_BUILD)
  add_subdirectory(sample)
  add_subdirectory(profiling)
endif()

add_subdirectory(xrt)

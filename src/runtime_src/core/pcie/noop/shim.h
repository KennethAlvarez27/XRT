// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2021-2022 Xilinx, Inc. All rights reserved.
// Copyright (C) 2022 Advanced Micro Devices, Inc. All rights reserved.
#ifndef XRT_CORE_PCIE_NOOP_SHIM_H
#define XRT_CORE_PCIE_NOOP_SHIM_H

#include "core/pcie/noop/config.h"
#include "core/common/device.h"
#include "core/common/query_requests.h"
#include "xrt.h"

namespace userpf {

xrt_core::query::kds_cu_info::result_type
kds_cu_info(const xrt_core::device* device);

xrt_core::query::xclbin_slots::result_type
xclbin_slots(const xrt_core::device* device);

uint32_t // slotidx
create_hw_context(const xrt_core::device* device, const xuid_t xclbin_uuid, uint32_t qos);

void
destroy_hw_context(const xrt_core::device* device, uint32_t slotidx);

void
register_xclbin(const xrt_core::device* device, const xrt::xclbin& xclbn);

} // userpf


#endif

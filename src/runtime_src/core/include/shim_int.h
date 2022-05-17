// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2021-2022 Xilinx, Inc. All rights reserved.
// Copyright (C) 2022 Advanced Micro Devices, Inc. All rights reserved.
#ifndef SHIM_INT_H_
#define SHIM_INT_H_

#include "core/include/xrt.h"

#include <string>

namespace xrt {

class xclbin;
class uuid;

namespace shim_int {

// This file defines internal shim APIs, which is not end user visible.
// This header file should not be published to xrt release include/ folder.

// open_by_bdf() - Open a device and obtain its handle by PCI BDF
//
// @bdf:           Deice PCE BDF
// Return:         Device handle
//
// Throws on error
XCL_DRIVER_DLLESPEC
xclDeviceHandle
open_by_bdf(const std::string& bdf);

// open_context() - Open a shared/exclusive context on a named compute unit
//
// @handle:        Device handle
// @slot:          Slot index of xclbin to service this context requiest
// @xclbin_uuid:   UUID of the xclbin image with the CU to open a context on
// @cuname:        Name of compute unit to open
// @shared:        Shared access or exclusive access
//
// Throws on error
XCL_DRIVER_DLLESPEC
void
open_context(xclDeviceHandle handle, uint32_t slot, const xrt::uuid& xclbin_uuid, const std::string& cuname, bool shared);

// create_hw_context() -
uint32_t // ctxhdl aka slotidx
create_hw_context(xclDeviceHandle handle, const xrt::uuid& xclbin_uuid, uint32_t qos);

// dsstroy_hw_context() -
void
destroy_hw_context(xclDeviceHandle handle, uint32_t ctxhdl);

// register_xclbin() -
void
register_xclbin(xclDeviceHandle handle, const xrt::xclbin& xclbin);

}} // shim_int, xrt

#endif

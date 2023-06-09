// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2021 Xilinx, Inc. All rights reserved.
// Copyright (C) 2022 Advanced Micro Devices, Inc. All rights reserved.
#include <iostream>
#include <stdexcept>
#include <string>

#include "xrt/xrt_device.h"

// Exercise some xrt::info::device query parameters as defined in
// xrt_device.h
//
// % g++ -g -std=c++17 -I$XILINX_XRT/include -L$XILINX_XRT/lib -o query.exe main.cpp -lxrt_coreutil -luuid -pthread

static void
usage()
{
    std::cout << "usage: %s [options] -k <bitstream>\n\n";
    std::cout << "  -k <bitstream>\n";
    std::cout << "  -d <bdf | device_index>\n";
    std::cout << "  [-j all] # dump all json queries\n";
    std::cout << "  -h\n\n";
    std::cout << "";
    std::cout << "* Bitstream is required\n";
}

static int
run(int argc, char** argv)
{
  if (argc < 3) {
    usage();
    return 1;
  }

  std::string xclbin_fnm;
  std::string device_index = "0";
  bool json_queries = false;

  std::vector<std::string> args(argv+1,argv+argc);
  std::string cur;
  for (auto& arg : args) {
    if (arg == "-h") {
      usage();
      return 1;
    }

    if (arg[0] == '-') {
      cur = arg;

      // No argument switches
      if (cur == "-j")
        json_queries = true;

      continue;
    }

    // Switch arguments
    if (cur == "-k")
      xclbin_fnm = arg;
    else if (cur == "-d")
      device_index = arg;
    else
      throw std::runtime_error("Unknown option value " + cur + " " + arg);
  }

  if (xclbin_fnm.empty())
    throw std::runtime_error("No xclbin specified");

  auto device = xrt::device(device_index);
  auto xclbin = xrt::xclbin{xclbin_fnm};
  auto uuid = device.load_xclbin(xclbin);

  if (uuid != xclbin.get_uuid())
    throw std::runtime_error("Unexpected uuid error");

  std::cout << "device name:           " << device.get_info<xrt::info::device::name>() << "\n";
  std::cout << "device bdf:            " << device.get_info<xrt::info::device::bdf>() << "\n";
  std::cout << "device kdma:           " << device.get_info<xrt::info::device::kdma>() << "\n";
  std::cout << "device max freq:       " << device.get_info<xrt::info::device::max_clock_frequency_mhz>() << "\n";
  std::cout << "device m2m:            " << std::boolalpha << device.get_info<xrt::info::device::m2m>() << std::dec << "\n";
  std::cout << "device nodma:          " << std::boolalpha << device.get_info<xrt::info::device::nodma>() << std::dec << "\n";
  std::cout << "device interface uuid: " << device.get_info<xrt::info::device::interface_uuid>().to_string() << "\n";

  if (json_queries) {
    std::cout << "device electrical json info ====================================\n";
    std::cout << device.get_info<xrt::info::device::electrical>();
    std::cout << "device thermal json info =======================================\n";
    std::cout << device.get_info<xrt::info::device::thermal>();
    std::cout << "device mechanical json info ====================================\n";
    std::cout << device.get_info<xrt::info::device::mechanical>();
    std::cout << "device memory json info ========================================\n";
    std::cout << device.get_info<xrt::info::device::memory>();
    std::cout << "device platform json info ======================================\n";
    std::cout << device.get_info<xrt::info::device::platform>();
    std::cout << "device pcie json info ==========================================\n";
    std::cout << device.get_info<xrt::info::device::pcie_info>();
    std::cout << "device dynamic regions json info ===============================\n";
    std::cout << device.get_info<xrt::info::device::dynamic_regions>();
    std::cout << "device host json info ==========================================\n";
    std::cout << device.get_info<xrt::info::device::host>();
  }

  // Equality implemented in 2.14
  auto device2 = xrt::device{device_index};
  if (device2 != device) {
#if XRT_VERSION_CODE >= XRT_VERSION(2,14)
    throw std::runtime_error("Equality check failed");
#else
    std::cout << "device equality not implemented in XRT("
              << XRT_MAJOR(XRT_VERSION_CODE) << ","
              << XRT_MINOR(XRT_VERSION_CODE) << ")\n";
#endif
  }

  return 0;
}

int
main(int argc, char** argv)
{
  try {
    auto ret = run(argc, argv);
    std::cout << "PASSED TEST\n";
    return ret;
  }
  catch (std::exception const& e) {
    std::cout << "Exception: " << e.what() << "\n";
    std::cout << "FAILED TEST\n";
    return 1;
  }

  std::cout << "PASSED TEST\n";
  return 0;
}

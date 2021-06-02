/**
 * Copyright (C) 2021 Xilinx, Inc
 *
 * Licensed under the Apache License, Version 2.0 (the "License"). You may
 * not use this file except in compliance with the License. A copy of the
 * License is located at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 */

// ------ I N C L U D E   F I L E S -------------------------------------------
// System - Include Files
#include <map>

// Local - Include Files
#include "ReportPlatforms.h"
#include "XBUtilities.h"
#include "core/common/query_requests.h"
#include "core/common/device.h"
namespace qr = xrt_core::query;

static std::map<int, std::string> p2p_config_map = {
  { 0, "disabled" },
  { 1, "enabled" },
  { 2, "error" },
  { 3, "reboot" },
  { 4, "not supported" },
};

/**
 * New flow for exposing mac addresses
 * qr::mac_contiguous_num is the total number of mac addresses
 * avaliable contiguously starting from qr::mac_addr_first
 * 
 * Old flow: Query the four sysfs nodes we have and validate them
 * before adding them to the property tree
 */
static boost::property_tree::ptree
mac_addresses(const xrt_core::device * dev)
{
  boost::property_tree::ptree ptree;
  uint64_t mac_contiguous_num = 0;
  std::string mac_addr_first;
  try {
    mac_contiguous_num = xrt_core::device_query<qr::mac_contiguous_num>(dev);
    mac_addr_first = xrt_core::device_query<qr::mac_addr_first>(dev);
  }
  catch (const xrt_core::query::no_such_key&) {
    // Ignoring if not available: Edge Case 
  }

  //new flow
  if (mac_contiguous_num && !mac_addr_first.empty()) {
    std::string mac_prefix = mac_addr_first.substr(0, mac_addr_first.find_last_of(":"));
    std::string mac_base = mac_addr_first.substr(mac_addr_first.find_last_of(":") + 1);
    std::stringstream ss;
    uint32_t mac_base_val = 0;
    ss << std::hex << mac_base;
    ss >> mac_base_val;

    for (uint32_t i = 0; i < (uint32_t)mac_contiguous_num; i++) {
      boost::property_tree::ptree addr;
      auto base = boost::format("%02X") % (mac_base_val + i);
      addr.add("address", mac_prefix + ":" + base.str());
      ptree.push_back(std::make_pair("", addr));
    } 
  }
  else { //old flow
    std::vector<std::string> mac_addr;
    try {	  
      mac_addr = xrt_core::device_query<qr::mac_addr_list>(dev);
    }
    catch (const xrt_core::query::no_such_key&) {
      // Ignoring if not available: Edge Case 
    }
    for (const auto& a : mac_addr) {
      boost::property_tree::ptree addr;
      if (!a.empty() && a.compare("FF:FF:FF:FF:FF:FF") != 0) {
        addr.add("address", a);
        ptree.push_back(std::make_pair("", addr));
      }
    }
  }

  return ptree;
}

void
ReportPlatforms::getPropertyTreeInternal( const xrt_core::device * dev, 
                                              boost::property_tree::ptree &pt) const
{
  // Defer to the 20202 format.  If we ever need to update JSON data, 
  // Then update this method to do so.
  getPropertyTree20202(dev, pt);
}

void 
ReportPlatforms::getPropertyTree20202( const xrt_core::device * dev, 
                                           boost::property_tree::ptree &pt) const
{
  boost::property_tree::ptree ptree;
  boost::property_tree::ptree pt_platform;
  
  boost::property_tree::ptree static_region;
  static_region.add("vbnv", xrt_core::device_query<qr::rom_vbnv>(dev));
  try {
    static_region.add("jtag_idcode", qr::idcode::to_string(xrt_core::device_query<qr::idcode>(dev)));
  }
  catch (const xrt_core::query::no_such_key&) {
    // Ignoring if not available: Edge Case
    static_region.add("jtag_idcode", "N/A");
  }

  try {
    static_region.add("fpga_name", xrt_core::device_query<qr::rom_fpga_name>(dev));
  }
  catch (const xrt_core::query::no_such_key&) {
    // Ignoring if not available: Edge Case 
    static_region.add("fpga_name", "N/A");
  }
  
  pt_platform.put_child("static_region", static_region);

  boost::property_tree::ptree bd_info;
  auto ddr_size_bytes = [](uint64_t size_gb, uint64_t count) {
    auto bytes = size_gb * 1024 * 1024 * 1024;
    return bytes * count;
  };
  bd_info.add("ddr_size_bytes", ddr_size_bytes(xrt_core::device_query<qr::rom_ddr_bank_size_gb>(dev), xrt_core::device_query<qr::rom_ddr_bank_count_max>(dev)));
  bd_info.add("ddr_count", xrt_core::device_query<qr::rom_ddr_bank_count_max>(dev));
  pt_platform.put_child("off_chip_board_info", bd_info);

  boost::property_tree::ptree status;
  try {
    status.add("mig_calibrated", xrt_core::device_query<qr::status_mig_calibrated>(dev));
  }
  catch (const xrt_core::query::no_such_key&) {
    // Ignoring if not available: Edge Case 
    status.add("mig_calibrated", "N/A");
  }

  std::string msg;
  auto value = XBUtilities::check_p2p_config(dev, msg);
  status.add("p2p_status", p2p_config_map[value]);
  pt_platform.put_child("status", status);

  boost::property_tree::ptree controller;
  boost::property_tree::ptree sc;
  boost::property_tree::ptree cmc;
  try {
    sc.add("version", xrt_core::device_query<qr::xmc_sc_version>(dev));
    sc.add("expected_version", xrt_core::device_query<qr::expected_sc_version>(dev));
    cmc.add("version", xrt_core::device_query<qr::xmc_version>(dev));
    cmc.add("serial_number", xrt_core::device_query<qr::xmc_serial_num>(dev));
    cmc.add("oem_id", XBUtilities::parse_oem_id(xrt_core::device_query<qr::oem_id>(dev)));
  }
  catch (const xrt_core::query::no_such_key&) {
    // Ignoring if not available: Edge Case 
  }
  controller.put_child("satellite_controller", sc);
  controller.put_child("card_mgmt_controller", cmc);
  pt_platform.put_child("controller", controller);

  std::vector<char> raw; 
  try { 
    raw = xrt_core::device_query<qr::clock_freq_topology_raw>(dev);
  }
  catch (const xrt_core::query::no_such_key&) {
    // Ignoring if not available: Edge Case 
  }
  if(!raw.empty()) {
    boost::property_tree::ptree pt_clocks;
    auto clock_topology = reinterpret_cast<const clock_freq_topology*>(raw.data());
    for(int i = 0; i < clock_topology->m_count; i++) {
      boost::property_tree::ptree clock;
      clock.add("id", clock_topology->m_clock_freq[i].m_name);
      clock.add("description", XBUtilities::parse_clock_id(clock_topology->m_clock_freq[i].m_name));
      clock.add("freq_mhz", clock_topology->m_clock_freq[i].m_freq_Mhz);
      pt_clocks.push_back(std::make_pair("", clock));
    }
    pt_platform.put_child("clocks", pt_clocks);
  }

  auto macs = mac_addresses(dev);
  if(!macs.empty())
    pt_platform.put_child("macs", macs);
    
  ptree.push_back(std::make_pair("", pt_platform));
 
  // There can only be 1 root node
  pt.add_child("platforms", ptree);
}

void 
ReportPlatforms::writeReport( const xrt_core::device* /*_pDevice*/,
                              const boost::property_tree::ptree& _pt, 
                              const std::vector<std::string>& /*_elementsFilter*/,
                              std::ostream & _output) const
{
  boost::property_tree::ptree empty_ptree;

  _output << "Platform\n";
  const boost::property_tree::ptree& platforms = _pt.get_child("platforms", empty_ptree);
  for(auto& kp : platforms) {
    const boost::property_tree::ptree& pt_platform = kp.second;
    const boost::property_tree::ptree& pt_static_region = pt_platform.get_child("static_region", empty_ptree);
    _output << boost::format("  %-23s: %s \n") % "XSA Name" % pt_static_region.get<std::string>("vbnv");
    _output << boost::format("  %-23s: %s \n") % "FPGA Name" % pt_static_region.get<std::string>("fpga_name");
    _output << boost::format("  %-23s: %s \n") % "JTAG ID Code" % pt_static_region.get<std::string>("jtag_idcode");
    
    const boost::property_tree::ptree& pt_board_info = pt_platform.get_child("off_chip_board_info");
    _output << boost::format("  %-23s: %s Bytes\n") % "DDR Size" % pt_board_info.get<std::string>("ddr_size_bytes");
    _output << boost::format("  %-23s: %s \n") % "DDR Count" % pt_board_info.get<std::string>("ddr_count");
    
    const boost::property_tree::ptree& pt_status = pt_platform.get_child("status");
    _output << boost::format("  %-23s: %s \n") % "Mig Calibrated" % pt_status.get<std::string>("mig_calibrated");
    _output << boost::format("  %-23s: %s \n") % "P2P Status" % pt_status.get<std::string>("p2p_status");

    const boost::property_tree::ptree& clocks = pt_platform.get_child("clocks", empty_ptree);
    if(!clocks.empty()) {
      _output << std::endl << "Clocks" << std::endl;
      for(auto& kc : clocks) {
        const boost::property_tree::ptree& pt_clock = kc.second;
        _output << boost::format("  %-23s: %s MHz\n") % pt_clock.get<std::string>("description") % pt_clock.get<std::string>("freq_mhz");
      }
    }

    const boost::property_tree::ptree& macs = pt_platform.get_child("macs", empty_ptree);
    if(!macs.empty()) {
      _output << std::endl;
      unsigned int macCount = 0;

      for(auto& km : macs) {
        const boost::property_tree::ptree& pt_mac = km.second;
        if( macCount++ == 0) 
          _output << boost::format("%-25s: %s\n") % "Mac Addresses" % pt_mac.get<std::string>("address");
        else
          _output << boost::format("  %-23s: %s\n") % "" % pt_mac.get<std::string>("address");
      }
    }
  }
  
  _output << std::endl;
}

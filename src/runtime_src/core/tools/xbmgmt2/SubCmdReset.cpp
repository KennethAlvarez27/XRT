/**
 * Copyright (C) 2019-2020 Xilinx, Inc
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
// Local - Include Files
#include "SubCmdReset.h"
#include "tools/common/XBUtilities.h"
namespace XBU = XBUtilities;

#include "core/common/query_requests.h"

// 3rd Party Library - Include Files
#include <boost/format.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/program_options.hpp>
namespace po = boost::program_options;

// System - Include Files
#include <iostream>

// ----- C L A S S   M E T H O D S -------------------------------------------

static void
pretty_print_action_list(xrt_core::device_collection& deviceCollection, XBU::reset_type reset)
{
  if(reset == XBU::reset_type::hot)
    std::cout << "Performing 'hot' reset on " << std::endl;
  else if(reset == XBU::reset_type::kernel)
    std::cout << "Performing DFX region reset on " << std::endl;
  else if(reset == XBU::reset_type::ert)
    std::cout << "Performing PS ERT reset on" << std::endl;
  else if(reset == XBU::reset_type::ecc)
    std::cout << "Resetting all ECC counters on " << std::endl;
  else if(reset == XBU::reset_type::soft_kernel)
    std::cout << "Performing Soft Kernel reset on " << std::endl;
  
  for(const auto & device: deviceCollection) {
      std::cout << boost::format("  -[%s]\n") % 
        xrt_core::query::pcie_bdf::to_string(xrt_core::device_query<xrt_core::query::pcie_bdf>(device));
  }

  if(reset == XBU::reset_type::hot)
    std::cout << "WARNING: Please make sure xocl driver is unloaded." << std::endl;
  else if(reset == XBU::reset_type::kernel)
    std::cout << "WARNING: Please make sure no application is currently running." << std::endl;
  std::cout << std::endl; 
}

static void 
reset_ecc(std::shared_ptr<xrt_core::device> dev)
{
  auto raw_mem = xrt_core::device_query<xrt_core::query::mem_topology_raw>(dev);
  const mem_topology *map = (mem_topology *)raw_mem.data();
    if(raw_mem.empty() || map->m_count == 0) {
      std::cout << "WARNING: 'mem_topology' not found, "
        << "unable to query ECC info. Has the xclbin been loaded? "
        << "See 'xbmgmt status'." << std::endl;
      return;
    }

    for(int32_t i = 0; i < map->m_count; i++) {
      if(!map->m_mem_data[i].m_used)
        continue;
      dev->reset(reinterpret_cast<const char *>(map->m_mem_data[i].m_tag), "ecc_reset", "1");
    }
}

static void
reset_device(std::shared_ptr<xrt_core::device> dev, XBU::reset_type type)
{
  if(type == XBU::reset_type::hot)
    dev->reset("", "mgmt_reset", "1");
  else if(type == XBU::reset_type::kernel)
    dev->reset("", "mgmt_reset", "2");
  else if(type == XBU::reset_type::ert)
    dev->reset("", "mgmt_reset", "3");
  else if(type == XBU::reset_type::soft_kernel)
    dev->reset("", "mgmt_reset", "4");
  else if(type == XBU::reset_type::ecc)
    reset_ecc(dev);
  std::cout << boost::format("Successfully reset Device[%s]\n") 
      % xrt_core::query::pcie_bdf::to_string(xrt_core::device_query<xrt_core::query::pcie_bdf>(dev));
}

SubCmdReset::SubCmdReset(bool _isHidden, bool _isDepricated, bool _isPreliminary)
    : SubCmd("reset", 
             "Resets the given device")
{
  const std::string longDescription = "Resets the given device.";
  setLongDescription(longDescription);
  setExampleSyntax("");
  setIsHidden(_isHidden);
  setIsDeprecated(_isDepricated);
  setIsPreliminary(_isPreliminary);
}

void
SubCmdReset::execute(const SubCmdOptions& _options) const
// Reference Command:  reset [-d card]

{
  XBU::verbose("SubCommand: reset");
  // -- Retrieve and parse the subcommand options -----------------------------
  std::vector<std::string> devices;
  std::string resetType = "hot";
  bool help = false;
  bool force = false; //TO-DO: remove this when the global force is implemented

  po::options_description commonOptions("Common Options");
  commonOptions.add_options()
    ("device,d", boost::program_options::value<decltype(devices)>(&devices)->multitoken(), "The Bus:Device.Function (e.g., 0000:d8:00.0) device of interest.  A value of 'all' (default) indicates that every found device should be examined.")
    ("type,r", boost::program_options::value<decltype(resetType)>(&resetType)->implicit_value("hot"), "The type of reset to perform. Types resets available:\n"
                                                                        "  hot          - Hot reset (default)\n"
                                                                        "  kernel       - Kernel communication links\n" 
                                                                        "  ert          - Reset management processor\n"
                                                                        "  ecc          - Reset ecc memory\n"
                                                                        "  soft-kernel  - Reset soft kernel")
    ("help,h", boost::program_options::bool_switch(&help), "Help to use this sub-command")
  ;

  po::options_description hiddenOptions("Hidden Options");

  po::options_description allOptions("All Options");
  allOptions.add(commonOptions);
  allOptions.add(hiddenOptions);

  // Parse sub-command ...
  po::variables_map vm;

  try {
    po::store(po::command_line_parser(_options).options(allOptions).run(), vm);
    po::notify(vm); // Can throw
  } catch (po::error& e) {
    std::cerr << "ERROR: " << e.what() << std::endl << std::endl;
    printHelp(commonOptions, hiddenOptions);

    // Re-throw exception
    throw;
  }

  // Check to see if help was requested or no command was found
  if (help == true)  {
    printHelp(commonOptions, hiddenOptions);
    return;
  }

  // -- Now process the subcommand --------------------------------------------
  XBU::verbose(boost::str(boost::format("  Reset: %s") % resetType));

  // -- process "device" option -----------------------------------------------
  // enforce device specification
  if(devices.empty())
    throw xrt_core::error("Please specify a device using --device option");

  // Collect all of the devices of interest
  std::set<std::string> deviceNames;
  xrt_core::device_collection deviceCollection;  // The collection of devices to examine
  for (const auto & deviceName : devices) 
    deviceNames.insert(boost::algorithm::to_lower_copy(deviceName));

  XBU::collect_devices(deviceNames, false /*inUserDomain*/, deviceCollection);
  XBU::reset_type type = XBU::str_to_enum_reset(resetType);
  pretty_print_action_list(deviceCollection, type);

  // Ask user for permission
  if(!force && !XBU::can_proceed())
    return;

  //perform reset actions
  for (const auto & dev : deviceCollection) {
    reset_device(dev, type);
  }
}

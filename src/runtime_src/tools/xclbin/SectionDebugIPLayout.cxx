/**
 * Copyright (C) 2018 Xilinx, Inc
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

#include "SectionDebugIPLayout.h"

#include "XclBinUtilities.h"
namespace XUtil = XclBinUtilities;

#include <iostream>

// Static Variables / Classes
SectionDebugIPLayout::_init SectionDebugIPLayout::_initializer;

SectionDebugIPLayout::SectionDebugIPLayout() {
  // Empty
}

SectionDebugIPLayout::~SectionDebugIPLayout() {
  // Empty
}

const std::string
SectionDebugIPLayout::getDebugIPTypeStr(enum DEBUG_IP_TYPE _debugIpType) const {
  switch (_debugIpType) {
    case UNDEFINED:
      return "UNDEFINED";
    case LAPC:
      return "LAPC";
    case ILA:
      return "ILA";
    case AXI_MM_MONITOR:
      return "AXI_MM_MONITOR";
    case AXI_TRACE_FUNNEL:
      return "AXI_TRACE_FUNNEL";
    case AXI_MONITOR_FIFO_LITE:
      return "AXI_MONITOR_FIFO_LITE";
    case AXI_MONITOR_FIFO_FULL:
      return "AXI_MONITOR_FIFO_FULL";
    case ACCEL_MONITOR:
      return "ACCEL_MONITOR";
  }

  return XUtil::format("UNKNOWN (%d)", (unsigned int)_debugIpType);
}

enum DEBUG_IP_TYPE
SectionDebugIPLayout::getDebugIPType(std::string& _sDebugIPType) const {
  if (_sDebugIPType == "LAPC")
    return LAPC;

  if (_sDebugIPType == "ILA")
    return ILA;

  if (_sDebugIPType == "AXI_MM_MONITOR")
    return AXI_MM_MONITOR;

  if (_sDebugIPType == "AXI_TRACE_FUNNEL")
    return AXI_TRACE_FUNNEL;

  if (_sDebugIPType == "AXI_MONITOR_FIFO_LITE")
    return AXI_MONITOR_FIFO_LITE;

  if (_sDebugIPType == "AXI_MONITOR_FIFO_FULL")
    return AXI_MONITOR_FIFO_FULL;

  if (_sDebugIPType == "ACCEL_MONITOR")
    return ACCEL_MONITOR;

  if (_sDebugIPType == "UNDEFINED")
    return UNDEFINED;

  std::string errMsg = "ERROR: Unknown IP type: '" + _sDebugIPType + "'";
  throw std::runtime_error(errMsg);
}


void
SectionDebugIPLayout::marshalToJSON(char* _pDataSection,
                                    unsigned int _sectionSize,
                                    boost::property_tree::ptree& _ptree) const {
  XUtil::TRACE("");
  XUtil::TRACE("Extracting: DEBUG_IP_LAYOUT");
  XUtil::TRACE_BUF("Section Buffer", reinterpret_cast<const char*>(_pDataSection), _sectionSize);

  // Do we have enough room to overlay the header structure
  if (_sectionSize < sizeof(debug_ip_layout)) {
    throw std::runtime_error(XUtil::format("ERROR: Section size (%d) is smaller than the size of the debug_ip_layout structure (%d)",
                                           _sectionSize, sizeof(debug_ip_layout)));
  }

  debug_ip_layout* pHdr = (debug_ip_layout*)_pDataSection;
  boost::property_tree::ptree debug_ip_layout;

  XUtil::TRACE(XUtil::format("m_count: %d", (uint32_t)pHdr->m_count));

  // Write out the entire structure except for the array structure
  XUtil::TRACE_BUF("ip_layout", reinterpret_cast<const char*>(pHdr), (unsigned long)&(pHdr->m_debug_ip_data[0]) - (unsigned long)pHdr);
  debug_ip_layout.put("m_count", XUtil::format("%d", (unsigned int)pHdr->m_count).c_str());

  debug_ip_data mydata = (debug_ip_data){ 0 };


  XUtil::TRACE(XUtil::format("Size of debug_ip_data: %d\nSize of mydata: %d",
                             sizeof(debug_ip_data),
                             sizeof(mydata)));
  unsigned int expectedSize = ((unsigned long)&(pHdr->m_debug_ip_data[0]) - (unsigned long)pHdr) + (sizeof(debug_ip_data) * (uint32_t)pHdr->m_count);

  if (_sectionSize != expectedSize) {
    throw std::runtime_error(XUtil::format("ERROR: Section size (%d) does not match expected section size (%d).",
                                           _sectionSize, expectedSize));
  }


  boost::property_tree::ptree m_debug_ip_data;
  for (int index = 0; index < pHdr->m_count; ++index) {
    boost::property_tree::ptree debug_ip_data;

    XUtil::TRACE(XUtil::format("[%d]: m_type: %d, m_index: %d, m_base_address: 0x%lx, m_name: '%s'",
                               index,
                               getDebugIPTypeStr((enum DEBUG_IP_TYPE)pHdr->m_debug_ip_data[index].m_type),
                               (unsigned int)pHdr->m_debug_ip_data[index].m_index,
                               pHdr->m_debug_ip_data[index].m_base_address,
                               pHdr->m_debug_ip_data[index].m_name));

    // Write out the entire structure
    XUtil::TRACE_BUF("debug_ip_data", reinterpret_cast<const char*>(&pHdr->m_debug_ip_data[index]), sizeof(debug_ip_data));

    debug_ip_data.put("m_type", getDebugIPTypeStr((enum DEBUG_IP_TYPE)pHdr->m_debug_ip_data[index].m_type).c_str());
    debug_ip_data.put("m_index", XUtil::format("%d", (unsigned int)pHdr->m_debug_ip_data[index].m_index).c_str());
    debug_ip_data.put("m_properties", XUtil::format("%d", (unsigned int)pHdr->m_debug_ip_data[index].m_properties).c_str());
    debug_ip_data.put("m_base_address", XUtil::format("0x%lx",  pHdr->m_debug_ip_data[index].m_base_address).c_str());
    debug_ip_data.put("m_name", XUtil::format("%s", pHdr->m_debug_ip_data[index].m_name).c_str());

    m_debug_ip_data.add_child("debug_ip_data", debug_ip_data);
  }

  debug_ip_layout.add_child("m_debug_ip_data", m_debug_ip_data);

  _ptree.add_child("debug_ip_layout", debug_ip_layout);
  XUtil::TRACE("-----------------------------");
}


void
SectionDebugIPLayout::marshalFromJSON(const boost::property_tree::ptree& _ptSection,
                                      std::ostringstream& _buf) const {
  const boost::property_tree::ptree& ptDebugIPLayout = _ptSection.get_child("debug_ip_layout");

  // Initialize the memory to zero's
  debug_ip_layout debugIpLayoutHdr = (debug_ip_layout){ 0 };

  // Read, store, and report mem_topology data
  debugIpLayoutHdr.m_count = ptDebugIPLayout.get<uint16_t>("m_count");

  XUtil::TRACE("DEBUG_IP_LAYOUT");
  XUtil::TRACE(XUtil::format("m_count: %d", debugIpLayoutHdr.m_count));

  if (debugIpLayoutHdr.m_count == 0) {
    std::cout << "WARNING: Skipping DEBUG_IP_LAYOUT section for count size is zero." << std::endl;
    return;
  }

  // Write out the entire structure except for the mem_data structure
  XUtil::TRACE_BUF("debug_ip_layout - minus debug_ip_data", reinterpret_cast<const char*>(&debugIpLayoutHdr), (sizeof(debug_ip_layout) - sizeof(debug_ip_data)));
  _buf.write(reinterpret_cast<const char*>(&debugIpLayoutHdr), sizeof(debug_ip_layout) - sizeof(debug_ip_data));

  // Read, store, and report connection segments
  unsigned int count = 0;
  const boost::property_tree::ptree debugIpDatas = ptDebugIPLayout.get_child("m_debug_ip_data");
  for (const auto& kv : debugIpDatas) {
    debug_ip_data debugIpDataHdr = (debug_ip_data){ 0 };
    boost::property_tree::ptree ptDebugIPData = kv.second;

    std::string sm_type = ptDebugIPData.get<std::string>("m_type");
    debugIpDataHdr.m_type = getDebugIPType(sm_type);
    debugIpDataHdr.m_index = ptDebugIPData.get<int8_t>("m_index");
    debugIpDataHdr.m_properties = ptDebugIPData.get<int8_t>("m_properties");

    std::string sBaseAddress = ptDebugIPData.get<std::string>("m_base_address");
    debugIpDataHdr.m_base_address = XUtil::stringToUInt64(sBaseAddress);

    std::string sm_name = ptDebugIPData.get<std::string>("m_name");
    if (sm_name.length() >= sizeof(debug_ip_data::m_name)) {
      std::string errMsg = XUtil::format("ERROR: The m_name entry length (%d), exceeds the allocated space (%d).  Name: '%s'",
                                         (unsigned int)sm_name.length(), (unsigned int)sizeof(debug_ip_data::m_name), sm_name);
      throw std::runtime_error(errMsg);
    }

    // We already know that there is enough room for this string
    memcpy(debugIpDataHdr.m_name, sm_name.c_str(), sm_name.length() + 1);

    XUtil::TRACE(XUtil::format("[%d]: m_type: %d, m_index: %d, m_properties: %d, m_base_address: 0x%lx, m_name: '%s'",
                               count,
                               (unsigned int)debugIpDataHdr.m_type,
                               (unsigned int)debugIpDataHdr.m_index,
                               (unsigned int)debugIpDataHdr.m_properties,
                               debugIpDataHdr.m_base_address,
                               debugIpDataHdr.m_name));

    // Write out the entire structure
    XUtil::TRACE_BUF("debug_ip_data", reinterpret_cast<const char*>(&debugIpDataHdr), sizeof(debug_ip_data));
    _buf.write(reinterpret_cast<const char*>(&debugIpDataHdr), sizeof(debug_ip_data));
    count++;
  }

  // -- The counts should match --
  if (count != debugIpLayoutHdr.m_count) {
    std::string errMsg = XUtil::format("ERROR: Number of connection sections (%d) does not match expected encoded value: %d",
                                       (unsigned int)count, (unsigned int)debugIpLayoutHdr.m_count);
    throw std::runtime_error(errMsg);
  }

}

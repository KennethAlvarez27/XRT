/**
 * Copyright (C) 2018, 2021 - 2022 Xilinx, Inc
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
#include <boost/format.hpp>
#include <boost/functional/factory.hpp>
#include <iostream>

namespace XUtil = XclBinUtilities;

// Static Variables / Classes
SectionDebugIPLayout::init SectionDebugIPLayout::initializer;

SectionDebugIPLayout::init::init()
{
  auto sectionInfo = std::make_unique<SectionInfo>(DEBUG_IP_LAYOUT, "DEBUG_IP_LAYOUT", boost::factory<SectionDebugIPLayout*>());
  sectionInfo->nodeName = "debug_ip_layout";

  sectionInfo->supportedAddFormats.push_back(FormatType::json);

  sectionInfo->supportedDumpFormats.push_back(FormatType::json);
  sectionInfo->supportedDumpFormats.push_back(FormatType::html);
  sectionInfo->supportedDumpFormats.push_back(FormatType::raw);

  addSectionType(std::move(sectionInfo));
}

// ----------------------------------------------------------------------------

const std::string
SectionDebugIPLayout::getDebugIPTypeStr(DEBUG_IP_TYPE _debugIpType) const
{
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
    case AXI_STREAM_MONITOR:
      return "AXI_STREAM_MONITOR";
    case AXI_STREAM_PROTOCOL_CHECKER:
      return "AXI_STREAM_PROTOCOL_CHECKER";
    case TRACE_S2MM:
      return "TRACE_S2MM";
    case TRACE_S2MM_FULL:
      return "TRACE_S2MM_FULL";
    case AXI_DMA:
      return "AXI_DMA";
    case AXI_NOC:
      return "AXI_NOC";
    case ACCEL_DEADLOCK_DETECTOR:
      return "ACCEL_DEADLOCK_DETECTOR";
    case DEBUG_IP_TYPE_MAX:
      return "DEBUG_IP_TYPE_MAX";
  }

  return (boost::format("UNKNOWN (%d)") % static_cast<unsigned int>(_debugIpType)).str();
}

DEBUG_IP_TYPE
SectionDebugIPLayout::getDebugIPType(std::string& _sDebugIPType) const
{
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

  if (_sDebugIPType == "TRACE_S2MM")
    return TRACE_S2MM;

  if (_sDebugIPType == "TRACE_S2MM_FULL")
    return TRACE_S2MM_FULL;

  if (_sDebugIPType == "AXI_DMA")
    return AXI_DMA;

  if (_sDebugIPType == "AXI_NOC")
    return AXI_NOC;

  if (_sDebugIPType == "AXI_STREAM_MONITOR")
    return AXI_STREAM_MONITOR;

  if (_sDebugIPType == "AXI_STREAM_PROTOCOL_CHECKER")
    return AXI_STREAM_PROTOCOL_CHECKER;

  if (_sDebugIPType == "ACCEL_DEADLOCK_DETECTOR")
    return ACCEL_DEADLOCK_DETECTOR;

  if (_sDebugIPType == "UNDEFINED")
    return UNDEFINED;

  if (_sDebugIPType == "DEBUG_IP_TYPE_MAX")
    return DEBUG_IP_TYPE_MAX;

  std::string errMsg = "ERROR: Unknown IP type: '" + _sDebugIPType + "'";
  throw std::runtime_error(errMsg);
}


void
SectionDebugIPLayout::marshalToJSON(char* _pDataSection,
                                    unsigned int _sectionSize,
                                    boost::property_tree::ptree& _ptree) const
{
  XUtil::TRACE("");
  XUtil::TRACE("Extracting: DEBUG_IP_LAYOUT");
  XUtil::TRACE_BUF("Section Buffer", reinterpret_cast<const char*>(_pDataSection), _sectionSize);

  // Do we have enough room to overlay the header structure
  if (_sectionSize < sizeof(debug_ip_layout)) {
    auto errMsg = boost::format("ERROR: Section size (%d) is smaller than the size of the debug_ip_layout structure (%d)")
        % _sectionSize % sizeof(debug_ip_layout);
    throw std::runtime_error(errMsg.str());
  }

  debug_ip_layout* pHdr = (debug_ip_layout*)_pDataSection;
  boost::property_tree::ptree debug_ip_layout;

  XUtil::TRACE(boost::format("m_count: %d") % (uint32_t)pHdr->m_count);

  // Write out the entire structure except for the array structure
  XUtil::TRACE_BUF("ip_layout", reinterpret_cast<const char*>(pHdr), ((uint64_t)&(pHdr->m_debug_ip_data[0]) - (uint64_t)pHdr));
  debug_ip_layout.put("m_count", (boost::format("%d") % static_cast<unsigned int>(pHdr->m_count)).str());

  debug_ip_data mydata = debug_ip_data{};


  XUtil::TRACE(boost::format("Size of debug_ip_data: %d\nSize of mydata: %d") % sizeof(debug_ip_data) % sizeof(mydata));
  uint64_t expectedSize = ((uint64_t)&(pHdr->m_debug_ip_data[0]) - (uint64_t)pHdr) + (sizeof(debug_ip_data) * (uint64_t)pHdr->m_count);

  if (_sectionSize != expectedSize) {
    auto errMsg = boost::format("ERROR: Section size (%d) does not match expected section size (%d).") % _sectionSize % expectedSize;
    throw std::runtime_error(errMsg.str());
  }


  boost::property_tree::ptree m_debug_ip_data;
  for (int index = 0; index < pHdr->m_count; ++index) {
    boost::property_tree::ptree debug_ip_data;

    // Reform the index value
    uint16_t m_virtual_index = (((uint16_t)pHdr->m_debug_ip_data[index].m_index_highbyte) << 8) + (uint16_t)pHdr->m_debug_ip_data[index].m_index_lowbyte;

    XUtil::TRACE(boost::format("[%d]: m_type: %d, index: %d (m_index_highbyte: 0x%x, m_index_lowbyte: 0x%x), m_properties: %d, m_major: %d, m_minor: %d, m_base_address: 0x%lx, m_name: '%s'")
                 % index
                 % static_cast<unsigned int>(pHdr->m_debug_ip_data[index].m_type)
                 % static_cast<unsigned int>(m_virtual_index)
                 % static_cast<unsigned int>(pHdr->m_debug_ip_data[index].m_index_highbyte)
                 % static_cast<unsigned int>(pHdr->m_debug_ip_data[index].m_index_lowbyte)
                 % static_cast<unsigned int>(pHdr->m_debug_ip_data[index].m_properties)
                 % static_cast<unsigned int>(pHdr->m_debug_ip_data[index].m_major)
                 % static_cast<unsigned int>(pHdr->m_debug_ip_data[index].m_minor)
                 % pHdr->m_debug_ip_data[index].m_base_address
                 % pHdr->m_debug_ip_data[index].m_name);

    // Write out the entire structure
    XUtil::TRACE_BUF("debug_ip_data", reinterpret_cast<const char*>(&pHdr->m_debug_ip_data[index]), sizeof(debug_ip_data));

    debug_ip_data.put("m_type", getDebugIPTypeStr((DEBUG_IP_TYPE)pHdr->m_debug_ip_data[index].m_type).c_str());
    debug_ip_data.put("m_index", (boost::format("%d") % static_cast<unsigned int>(m_virtual_index)).str());
    debug_ip_data.put("m_properties", (boost::format("%d") % static_cast<unsigned int>(pHdr->m_debug_ip_data[index].m_properties)).str());
    debug_ip_data.put("m_major", (boost::format("%d") % static_cast<unsigned int>(pHdr->m_debug_ip_data[index].m_major)).str());
    debug_ip_data.put("m_minor", (boost::format("%d") % static_cast<unsigned int>(pHdr->m_debug_ip_data[index].m_minor)).str());
    debug_ip_data.put("m_base_address", (boost::format("0x%lx") %  pHdr->m_debug_ip_data[index].m_base_address).str());
    debug_ip_data.put("m_name", (boost::format("%s") % pHdr->m_debug_ip_data[index].m_name).str());

    m_debug_ip_data.push_back({ "", debug_ip_data });   // Used to make an array of objects
  }

  debug_ip_layout.add_child("m_debug_ip_data", m_debug_ip_data);

  _ptree.add_child("debug_ip_layout", debug_ip_layout);
  XUtil::TRACE("-----------------------------");
}


void
SectionDebugIPLayout::marshalFromJSON(const boost::property_tree::ptree& _ptSection,
                                      std::ostringstream& _buf) const
{
  const boost::property_tree::ptree& ptDebugIPLayout = _ptSection.get_child("debug_ip_layout");

  // Initialize the memory to zero's
  debug_ip_layout debugIpLayoutHdr = debug_ip_layout{};

  // Read, store, and report mem_topology data
  debugIpLayoutHdr.m_count = ptDebugIPLayout.get<uint16_t>("m_count");

  XUtil::TRACE("DEBUG_IP_LAYOUT");
  XUtil::TRACE(boost::format("m_count: %d") % debugIpLayoutHdr.m_count);

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
    debug_ip_data debugIpDataHdr = debug_ip_data{};
    boost::property_tree::ptree ptDebugIPData = kv.second;

    auto sm_type = ptDebugIPData.get<std::string>("m_type");
    debugIpDataHdr.m_type = (uint8_t)getDebugIPType(sm_type);

    // The index value in 2019.2 was expanded to 2 bytes (a high and low byte)
    uint16_t index = ptDebugIPData.get<uint16_t>("m_index");
    debugIpDataHdr.m_index_lowbyte = index & 0x00FF;
    debugIpDataHdr.m_index_highbyte = (index & 0xFF00) >> 8;

    debugIpDataHdr.m_properties = ptDebugIPData.get<int8_t>("m_properties");

    // Optional value, will set to 0 if not set (as it was initialized)
    debugIpDataHdr.m_major = ptDebugIPData.get<uint8_t>("m_major", 0);
    // Optional value, will set to 0 if not set (as it was initialized)
    debugIpDataHdr.m_minor = ptDebugIPData.get<uint8_t>("m_minor", 0);

    auto sBaseAddress = ptDebugIPData.get<std::string>("m_base_address");
    debugIpDataHdr.m_base_address = XUtil::stringToUInt64(sBaseAddress);

    auto sm_name = ptDebugIPData.get<std::string>("m_name");
    if (sm_name.length() >= sizeof(debug_ip_data::m_name)) {
      auto errMsg = boost::format("ERROR: The m_name entry length (%d), exceeds the allocated space (%d).  Name: '%s'")
          % static_cast<unsigned int>(sm_name.length())
          % static_cast<unsigned int>(sizeof(debug_ip_data::m_name))
          % sm_name;
      throw std::runtime_error(errMsg.str());
    }

    // We already know that there is enough room for this string
    memcpy(debugIpDataHdr.m_name, sm_name.c_str(), sm_name.length() + 1);

    XUtil::TRACE(boost::format("[%d]: m_type: %d, index: %d (m_index_highbyte: 0x%x, m_index_lowbyte: 0x%x), m_properties: %d, m_major: %d, m_minor: %d, m_base_address: 0x%lx, m_name: '%s'")
                 % count
                 % static_cast<unsigned int>(debugIpDataHdr.m_type)
                 % static_cast<unsigned int>(index)
                 % static_cast<unsigned int>(debugIpDataHdr.m_index_highbyte)
                 % static_cast<unsigned int>(debugIpDataHdr.m_index_lowbyte)
                 % static_cast<unsigned int>(debugIpDataHdr.m_properties)
                 % static_cast<unsigned int>(debugIpDataHdr.m_major)
                 % static_cast<unsigned int>(debugIpDataHdr.m_minor)
                 % debugIpDataHdr.m_base_address
                 % debugIpDataHdr.m_name);

    // Write out the entire structure
    XUtil::TRACE_BUF("debug_ip_data", reinterpret_cast<const char*>(&debugIpDataHdr), sizeof(debug_ip_data));
    _buf.write(reinterpret_cast<const char*>(&debugIpDataHdr), sizeof(debug_ip_data));
    count++;
  }

  // -- The counts should match --
  if (count != debugIpLayoutHdr.m_count) {
    auto errMsg = boost::format("ERROR: Number of connection sections (%d) does not match expected encoded value: %d")
        % static_cast<unsigned int>(count)
        % static_cast<unsigned int>(debugIpLayoutHdr.m_count);
    throw std::runtime_error(errMsg.str());
  }

  // -- Buffer needs to be less than 64K--
  unsigned int bufferSize = static_cast<unsigned int>(_buf.str().size());
  const unsigned int maxBufferSize = 64 * 1024;
  if (bufferSize > maxBufferSize) {
    auto errMsg = boost::format("CRITICAL WARNING: The buffer size for the DEBUG_IP_LAYOUT section (%d) exceed the maximum size of %d.\nThis can result in lose of data in the driver.")
        % static_cast<unsigned int>(bufferSize)
        % static_cast<unsigned int>(maxBufferSize);
    std::cout << errMsg << std::endl;
    // throw std::runtime_error(errMsg);
  }
}



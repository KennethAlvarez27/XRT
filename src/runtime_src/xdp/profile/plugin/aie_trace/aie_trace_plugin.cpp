/**
 * Copyright (C) 2020-2021 Xilinx, Inc
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

#define XDP_SOURCE

#include <boost/algorithm/string.hpp>
#include <cmath>
#include <iostream>
#include <memory>

#include "core/common/message.h"
#include "core/common/xrt_profiling.h"
#include "core/edge/user/shim.h"

#include "xdp/profile/database/database.h"
#include "xdp/profile/database/events/creator/aie_trace_data_logger.h"
#include "xdp/profile/database/static_info/aie_constructs.h"
#include "xdp/profile/database/static_info/pl_constructs.h"
#include "xdp/profile/device/aie_trace/aie_trace_offload.h"
#include "xdp/profile/device/device_intf.h"
#include "xdp/profile/device/hal_device/xdp_hal_device.h"
#include "xdp/profile/device/tracedefs.h"
#include "xdp/profile/plugin/aie_trace/aie_trace_plugin.h"
#include "xdp/profile/plugin/vp_base/info.h"
#include "xdp/profile/writer/aie_trace/aie_trace_writer.h"
#include "xdp/profile/writer/aie_trace/aie_trace_config_writer.h"

#define NUM_CORE_TRACE_EVENTS   8
#define NUM_MEMORY_TRACE_EVENTS 8
#define CORE_BROADCAST_EVENT_BASE 107

namespace {
  static void* fetchAieDevInst(void* devHandle)
  {
    auto drv = ZYNQ::shim::handleCheck(devHandle);
    if (!drv)
      return nullptr ;
    auto aieArray = drv->getAieArray() ;
    if (!aieArray)
      return nullptr ;
    return aieArray->getDevInst() ;
  }

  static void* allocateAieDevice(void* devHandle)
  {
    XAie_DevInst* aieDevInst =
      static_cast<XAie_DevInst*>(fetchAieDevInst(devHandle)) ;
    if (!aieDevInst)
      return nullptr ;
    return new xaiefal::XAieDev(aieDevInst, false) ;
  }

  static void deallocateAieDevice(void* aieDevice)
  {
    xaiefal::XAieDev* object = static_cast<xaiefal::XAieDev*>(aieDevice) ;
    if (object != nullptr)
      delete object ;
  }
} // end anonymous namespace

namespace xdp {
  using severity_level = xrt_core::message::severity_level;
  using module_type = xrt_core::edge::aie::module_type;

  bool AieTracePlugin::live = false;

  AieTracePlugin::AieTracePlugin()
                : XDPPlugin(),
                  continuousTrace(false),
                  offloadIntervalms(0)
  {
    AieTracePlugin::live = true;

    db->registerPlugin(this);
    db->registerInfo(info::aie_trace);

    // Check whether continuous trace is enabled in xrt.ini
    // AIE trace is now supported for HW only
    continuousTrace = xrt_core::config::get_aie_trace_periodic_offload();
    if (continuousTrace) {
      offloadIntervalms = xrt_core::config::get_aie_trace_buffer_offload_interval_ms();
    }

    // Pre-defined metric sets
    metricSets = {"functions", "functions_partial_stalls", "functions_all_stalls", "all"};
    
    // Pre-defined metric sets
    //
    // **** Core Module Trace ****
    // NOTE: these are supplemented with counter events as those are dependent on counter #
    coreEventSets = {
      {"functions",                {XAIE_EVENT_INSTR_CALL_CORE,       XAIE_EVENT_INSTR_RETURN_CORE}},
      {"functions_partial_stalls", {XAIE_EVENT_INSTR_CALL_CORE,       XAIE_EVENT_INSTR_RETURN_CORE}},
      {"functions_all_stalls",     {XAIE_EVENT_INSTR_CALL_CORE,       XAIE_EVENT_INSTR_RETURN_CORE}},
      {"all",                      {XAIE_EVENT_INSTR_CALL_CORE,       XAIE_EVENT_INSTR_RETURN_CORE}}
    };

    // These are also broadcast to memory module
    coreTraceStartEvent = XAIE_EVENT_ACTIVE_CORE;
    coreTraceEndEvent   = XAIE_EVENT_DISABLED_CORE;
    
    // **** Memory Module Trace ****
    // NOTE 1: Core events listed here are broadcast by the resource manager
    // NOTE 2: These are supplemented with counter events as those are dependent on counter #
    // NOTE 3: For now, 'all' is the same as 'functions_all_stalls'. Combo events (required 
    //         for all) have limited support in the resource manager.
    memoryEventSets = {
      {"functions",                {XAIE_EVENT_INSTR_CALL_CORE,       XAIE_EVENT_INSTR_RETURN_CORE}},
      {"functions_partial_stalls", {XAIE_EVENT_INSTR_CALL_CORE,       XAIE_EVENT_INSTR_RETURN_CORE,
                                    XAIE_EVENT_STREAM_STALL_CORE, 
                                    XAIE_EVENT_CASCADE_STALL_CORE,    XAIE_EVENT_LOCK_STALL_CORE}},
      {"functions_all_stalls",     {XAIE_EVENT_INSTR_CALL_CORE,       XAIE_EVENT_INSTR_RETURN_CORE,
                                    XAIE_EVENT_MEMORY_STALL_CORE,     XAIE_EVENT_STREAM_STALL_CORE, 
                                    XAIE_EVENT_CASCADE_STALL_CORE,    XAIE_EVENT_LOCK_STALL_CORE}},
      {"all",                      {XAIE_EVENT_INSTR_CALL_CORE,       XAIE_EVENT_INSTR_RETURN_CORE,
                                    XAIE_EVENT_MEMORY_STALL_CORE,     XAIE_EVENT_STREAM_STALL_CORE, 
                                    XAIE_EVENT_CASCADE_STALL_CORE,    XAIE_EVENT_LOCK_STALL_CORE}}
    };

    // **** Core Module Counters ****
    // NOTE 1: Reset events are dependent on actual profile counter reserved.
    // NOTE 2: These counters are required HW workarounds with thresholds chosen 
    //         to produce events before hitting the bug. For example, sync packets 
    //         occur after 1024 cycles and with no events, is incorrectly repeated.
    auto counterScheme = xrt_core::config::get_aie_trace_counter_scheme();
    if (counterScheme == "es1") {
      coreCounterStartEvents   = {XAIE_EVENT_ACTIVE_CORE,             XAIE_EVENT_ACTIVE_CORE};
      coreCounterEndEvents     = {XAIE_EVENT_DISABLED_CORE,           XAIE_EVENT_DISABLED_CORE};
      coreCounterEventValues   = {1020, 1020*1020};
    }
    else if (counterScheme == "es2") {
      coreCounterStartEvents   = {XAIE_EVENT_ACTIVE_CORE};
      coreCounterEndEvents     = {XAIE_EVENT_DISABLED_CORE};
      coreCounterEventValues   = {0x3FF00};
    }
    
    // **** Memory Module Counters ****
    // NOTE 1: Reset events are dependent on actual profile counter reserved.
    // NOTE 2: These counters are required HW workarounds (see description above).
    //         They are only required for ES1 devices. For ES2 devices, the core
    //         counter is broadcast.
    if (counterScheme == "es1") {
      memoryCounterStartEvents = {XAIE_EVENT_TRUE_MEM,                XAIE_EVENT_TRUE_MEM};
      memoryCounterEndEvents   = {XAIE_EVENT_NONE_MEM,                XAIE_EVENT_NONE_MEM};
      memoryCounterEventValues = {1020, 1020*1020};
    }
    else if (counterScheme == "es2") {
      memoryCounterStartEvents = {XAIE_EVENT_TRUE_MEM};
      memoryCounterEndEvents   = {XAIE_EVENT_NONE_MEM};
      memoryCounterEventValues = {0x3FF00};
    }

    //Process the file dump interval
    aie_trace_file_dump_int_s = xrt_core::config::get_aie_trace_file_dump_interval_s();
    if (aie_trace_file_dump_int_s < MIN_TRACE_DUMP_INTERVAL_S){
      aie_trace_file_dump_int_s = MIN_TRACE_DUMP_INTERVAL_S;
      xrt_core::message::send(xrt_core::message::severity_level::warning, "XRT", AIE_TRACE_DUMP_INTERVAL_WARN_MSG);
    }
  }

  AieTracePlugin::~AieTracePlugin()
  {
    if(VPDatabase::alive()) {
      try {
        writeAll(false);
      }
      catch(...) {
      }
      db->unregisterPlugin(this);
    }

    // If the database is dead, then we must have already forced a 
    //  write at the database destructor so we can just move on

    for(auto h : deviceHandles) {
      xclClose(h);
    }
    AieTracePlugin::live = false;
  }

  // Convert broadcast ID to event ID
  inline uint32_t AieTracePlugin::bcIdToEvent(int bcId)
  {
    return bcId + CORE_BROADCAST_EVENT_BASE;
  }

  // Release counters from latest tile (because something went wrong)
  void AieTracePlugin::releaseCurrentTileCounters(int numCoreCounters, int numMemoryCounters)
  {
    for (int i=0; i < numCoreCounters; i++) {
      mCoreCounters.back()->stop();
      mCoreCounters.back()->release();
      mCoreCounters.pop_back();
      mCoreCounterTiles.pop_back();
    }
    for (int i=0; i < numMemoryCounters; i++) {
      mMemoryCounters.back()->stop();
      mMemoryCounters.back()->release();
      mMemoryCounters.pop_back();
    }
  }

  bool tileCompare(tile_type tile1, tile_type tile2) 
  {
    return ((tile1.col == tile2.col) && (tile1.row == tile2.row));
  }

  bool AieTracePlugin::tileHasFreeRsc(xaiefal::XAieDev* aieDevice, XAie_LocType& loc, const std::string& metricSet, bool useDelay)
  {
    auto stats = aieDevice->getRscStat(XAIEDEV_DEFAULT_GROUP_AVAIL);
    uint32_t available = 0;
    uint32_t required = 0;
    std::stringstream msg;

    // Core Module perf counters
    available = stats.getNumRsc(loc, XAIE_CORE_MOD, XAIE_PERFCNT_RSC);
    required = coreCounterStartEvents.size();
    if (useDelay)
      required += 1;
    if (available < required) {
      msg << "Available core module performance counters for aie trace : " << available << std::endl
          << "Required core module performance counters for aie trace : "  << required;
      xrt_core::message::send(severity_level::info, "XRT", msg.str());
      return false;
    }

    // Core Module trace slots
    available = stats.getNumRsc(loc, XAIE_CORE_MOD, xaiefal::XAIE_TRACE_EVENTS_RSC);
    required = coreCounterStartEvents.size() + coreEventSets[metricSet].size();
    if (available < required) {
      msg << "Available core module trace slots for aie trace : " << available << std::endl
          << "Required core module trace slots for aie trace : "  << required;
      xrt_core::message::send(severity_level::info, "XRT", msg.str());
      return false;
    }

    // Core Module broadcasts. 2 events for starting/ending trace
    available = stats.getNumRsc(loc, XAIE_CORE_MOD, XAIE_BCAST_CHANNEL_RSC);
    required = memoryEventSets[metricSet].size() + 2;
    if (available < required) {
      msg << "Available core module broadcast channels for aie trace : " << available << std::endl
          << "Required core module broadcast channels for aie trace : "  << required;
      xrt_core::message::send(severity_level::info, "XRT", msg.str());
      return false;
    }

   // Memory Module perf counters
    available = stats.getNumRsc(loc, XAIE_MEM_MOD, XAIE_PERFCNT_RSC);
    required = memoryCounterStartEvents.size();
    if (available < required) {
      msg << "Available memory module performance counters for aie trace : " << available << std::endl
          << "Required memory module performance counters for aie trace : "  << required;
      xrt_core::message::send(severity_level::info, "XRT", msg.str());
      return false;
    }

    // Memory Module trace slots
    available = stats.getNumRsc(loc, XAIE_MEM_MOD, xaiefal::XAIE_TRACE_EVENTS_RSC);
    required = memoryCounterStartEvents.size() + memoryEventSets[metricSet].size();
    if (available < required) {
      msg << "Available memory module trace slots for aie trace : " << available << std::endl
          << "Required memory module trace slots for aie trace : "  << required;
      xrt_core::message::send(severity_level::info, "XRT", msg.str());
      return false;
    }

    // No need to check memory module broadcast

    return true;
  }

  void AieTracePlugin::printTileStats(xaiefal::XAieDev* aieDevice, const tile_type& tile)
  {
    auto col = tile.col;
    auto row = tile.row + 1;
    auto loc = XAie_TileLoc(col, row);
    std::stringstream msg;

    const std::string groups[3] = {
      XAIEDEV_DEFAULT_GROUP_GENERIC,
      XAIEDEV_DEFAULT_GROUP_STATIC,
      XAIEDEV_DEFAULT_GROUP_AVAIL
    };

    msg << "Resource usage stats for Tile : (" << col << "," << row << ") Module : Core" << std::endl;
    for (auto&g : groups) {
      auto stats = aieDevice->getRscStat(g);
      auto pc = stats.getNumRsc(loc, XAIE_CORE_MOD, XAIE_PERFCNT_RSC);
      auto ts = stats.getNumRsc(loc, XAIE_CORE_MOD, xaiefal::XAIE_TRACE_EVENTS_RSC);
      auto bc = stats.getNumRsc(loc, XAIE_CORE_MOD, XAIE_BCAST_CHANNEL_RSC);
      msg << "Resource Group : " << std::left <<  std::setw(10) << g << " "
          << "Performance Counters : " << pc << " "
          << "Trace Slots : " << ts << " "
          << "Broadcast Channels : " << bc << " "
          << std::endl;
    }
    msg << "Resource usage stats for Tile : (" << col << "," << row << ") Module : Memory" << std::endl;
    for (auto&g : groups) {
    auto stats = aieDevice->getRscStat(g);
    auto pc = stats.getNumRsc(loc, XAIE_MEM_MOD, XAIE_PERFCNT_RSC);
    auto ts = stats.getNumRsc(loc, XAIE_MEM_MOD, xaiefal::XAIE_TRACE_EVENTS_RSC);
    auto bc = stats.getNumRsc(loc, XAIE_MEM_MOD, XAIE_BCAST_CHANNEL_RSC);
    msg << "Resource Group : "  << std::left <<  std::setw(10) << g << " "
        << "Performance Counters : " << pc << " "
        << "Trace Slots : " << ts << " "
        << "Broadcast Channels : " << bc << " "
        << std::endl;
    }
    xrt_core::message::send(severity_level::info, "XRT", msg.str());
  }

  std::string AieTracePlugin::getMetricSet(void* handle)
  {
    // Catch when compile-time trace is specified (e.g., --event-trace=functions)
    std::shared_ptr<xrt_core::device> device = xrt_core::get_userpf_device(handle);
    auto compilerOptions = xrt_core::edge::aie::get_aiecompiler_options(device.get());
    runtimeMetrics = (compilerOptions.event_trace == "runtime");

    if (!runtimeMetrics) {
      std::stringstream msg;
      msg << "Found compiler trace option of " << compilerOptions.event_trace
          << ". No runtime AIE metrics will be changed.";
      xrt_core::message::send(severity_level::info, "XRT", msg.str());
      return {};
    }

    std::string metricsStr = xrt_core::config::get_aie_trace_metrics();
    if (metricsStr.empty()) {
      std::string msg("The setting aie_trace_metrics was not specified in xrt.ini. AIE event trace will not be available.");
      xrt_core::message::send(severity_level::warning, "XRT", msg);
      return {};
    }

    std::vector<std::string> vec;
    boost::split(vec, metricsStr, boost::is_any_of(":"));

    for (int i=0; i < vec.size(); ++i) {
      boost::replace_all(vec.at(i), "{", "");
      boost::replace_all(vec.at(i), "}", "");
    }

    // Determine specification type based on vector size:
    //   * Size = 1: All tiles
    //     * aie_trace_metrics = <functions|functions_partial_stalls|functions_all_stalls|all>
    //   * Size = 2: Single tile or kernel name (supported in future release)
    //     * aie_trace_metrics = {<column>,<row>}:<functions|functions_partial_stalls|functions_all_stalls|all>
    //     * aie_trace_metrics= <kernel name>:<functions|functions_partial_stalls|functions_all_stalls|all>
    //   * Size = 3: Range of tiles (supported in future release)
    //     * aie_trace_metrics= {<mincolumn,<minrow>}:{<maxcolumn>,<maxrow>}:<functions|functions_partial_stalls|functions_all_stalls|all>
    metricSet = vec.at( vec.size()-1 );

    if (metricSets.find(metricSet) == metricSets.end()) {
      std::string defaultSet = "functions";
      std::stringstream msg;
      msg << "Unable to find AIE trace metric set " << metricSet 
          << ". Using default of " << defaultSet << ".";
      xrt_core::message::send(severity_level::warning, "XRT", msg.str());
      metricSet = defaultSet;
    }

    // If requested, turn on debug fal messages
    if (xrt_core::config::get_verbosity() >= static_cast<uint32_t>(severity_level::debug))
      xaiefal::Logger::get().setLogLevel(xaiefal::LogLevel::DEBUG);

    return metricSet;
  }

  bool AieTracePlugin::alive()
  {
    return AieTracePlugin::live;
  }

  std::vector<tile_type> AieTracePlugin::getTilesForTracing(void* handle)
  {
    std::vector<tile_type> tiles;
    // Create superset of all tiles across all graphs
    // NOTE: future releases will support the specification of tile subsets
    auto device = xrt_core::get_userpf_device(handle);
    auto graphs = xrt_core::edge::aie::get_graphs(device.get());
    for (auto& graph : graphs) {
      auto currTiles = xrt_core::edge::aie::get_tiles(device.get(), graph);
      std::copy(currTiles.begin(), currTiles.end(), back_inserter(tiles));

      // TODO: Differentiate between core and DMA-only tiles when 'all' is supported

      // Core Tiles
      //auto coreTiles = xrt_core::edge::aie::get_event_tiles(device.get(), graph, module_type::core);
      //std::unique_copy(coreTiles.begin(), coreTiles.end(), std::back_inserter(tiles), tileCompare);

      // DMA-Only Tiles
      // NOTE: These tiles are only needed when aie_trace_metrics = all
      //auto dmaTiles = xrt_core::edge::aie::get_event_tiles(device.get(), graph, module_type::dma);
      //std::unique_copy(dmaTiles.begin(), dmaTiles.end(), std::back_inserter(tiles), tileCompare);
    }
    return tiles;
  }

  uint64_t AieTracePlugin::getTraceStartDelayCycles(void* handle)
  {
    auto device = xrt_core::get_userpf_device(handle);
    auto freqMhz = xrt_core::edge::aie::get_clock_freq_mhz(device.get());

    std::smatch pieces_match;
    uint64_t cycles_per_sec = static_cast<uint64_t>(freqMhz * 1e6);
    const uint64_t max_cycles = 0xffffffff;
    std::string size_str = xrt_core::config::get_aie_trace_start_time();

    // Catch cases like "1Ms" "1NS"
    std::transform(size_str.begin(), size_str.end(), size_str.begin(),
      [](unsigned char c){ return std::tolower(c); });

    // Default is 0 cycles
    uint64_t cycles = 0;
    // Regex can parse values like : "1s" "1ms" "1ns"
    const std::regex size_regex("\\s*(\\d+\\.?\\d*)\\s*(s|ms|us|ns|)\\s*");
    if (std::regex_match(size_str, pieces_match, size_regex)) {
      try {
        if (pieces_match[2] == "s") {
          cycles = static_cast<uint64_t>(std::stof(pieces_match[1]) * cycles_per_sec);
        } else if (pieces_match[2] == "ms") {
          cycles = static_cast<uint64_t>(std::stof(pieces_match[1]) * cycles_per_sec /  1e3);
        } else if (pieces_match[2] == "us") {
          cycles = static_cast<uint64_t>(std::stof(pieces_match[1]) * cycles_per_sec /  1e6);
        } else if (pieces_match[2] == "ns") {
          cycles = static_cast<uint64_t>(std::stof(pieces_match[1]) * cycles_per_sec /  1e9);
        } else {
          cycles = static_cast<uint64_t>(std::stof(pieces_match[1]));
        }
        
        std::string msg("Parsed aie_trace_start_time: " + std::to_string(cycles) + " cycles.");
        xrt_core::message::send(xrt_core::message::severity_level::info, "XRT", msg);

      } catch (const std::exception& ) {
        // User specified number cannot be parsed
        std::string msg("Unable to parse aie_trace_start_time. Setting start time to 0.");
        xrt_core::message::send(xrt_core::message::severity_level::warning, "XRT", msg);
      }
    } else {  
      std::string msg("Unable to parse aie_trace_start_time. Setting start time to 0.");
      xrt_core::message::send(xrt_core::message::severity_level::warning, "XRT", msg);
    }

    if (cycles > max_cycles) {
      cycles = max_cycles;
      std::string msg("Setting aie_trace_delay to max supported of 0xffffffff cycles.");
      xrt_core::message::send(xrt_core::message::severity_level::warning, "XRT", msg);
    }

    return cycles;
  }

  // Configure all resources necessary for trace control and events
  bool AieTracePlugin::setMetrics(uint64_t deviceId, void* handle)
  {
    XAie_DevInst* aieDevInst =
      static_cast<XAie_DevInst*>(db->getStaticInfo().getAieDevInst(fetchAieDevInst, handle)) ;
    xaiefal::XAieDev* aieDevice =
      static_cast<xaiefal::XAieDev*>(db->getStaticInfo().getAieDevice(allocateAieDevice, deallocateAieDevice, handle)) ;
    if (!aieDevInst || !aieDevice) {
      xrt_core::message::send(severity_level::warning, "XRT",
          "Unable to get AIE device. AIE event trace will not be available.");
      return false;
    }

    auto metricSet = getMetricSet(handle);
    if (metricSet.empty()) {
      if (!runtimeMetrics)
        return true;
      return false;
    }
    auto tiles = getTilesForTracing(handle);
    
    // getTraceStartDelayCycles is 32 bit for now
    uint32_t delayCycles = static_cast<uint32_t>(getTraceStartDelayCycles(handle));
    bool useDelay = (delayCycles > 0) ? true : false;

    // Keep track of number of events reserved per tile
    int numTileCoreTraceEvents[NUM_CORE_TRACE_EVENTS+1] = {0};
    int numTileMemoryTraceEvents[NUM_MEMORY_TRACE_EVENTS+1] = {0};

    // Iterate over all used/specified tiles
    for (auto& tile : tiles) {
      auto  col    = tile.col;
      auto  row    = tile.row;
      // NOTE: resource manager requires absolute row number
      auto& core   = aieDevice->tile(col, row + 1).core();
      auto& memory = aieDevice->tile(col, row + 1).mem();
      auto loc = XAie_TileLoc(col, row + 1);

      // AIE config object for this tile
      auto cfgTile  = std::make_unique<aie_cfg_tile>(col, row + 1);

      // Get vector of pre-defined metrics for this set
      // NOTE: these are local copies as we are adding tile/counter-specific events
      EventVector coreEvents = coreEventSets[metricSet];
      EventVector memoryCrossEvents = memoryEventSets[metricSet];
      EventVector memoryEvents;

      // Check Resource Availability
      // For now only counters are checked
      if (!tileHasFreeRsc(aieDevice, loc, metricSet, useDelay)) {
        xrt_core::message::send(severity_level::warning, "XRT", "Tile doesn't have enough free resources for trace. Aborting trace configuration.");
        printTileStats(aieDevice, tile);
        return false;
      }

      //
      // 1. Reserve and start core module counters (as needed)
      //
      int numCoreCounters = 0;
      {
        XAie_ModuleType mod = XAIE_CORE_MOD;

        for (int i=0; i < coreCounterStartEvents.size(); ++i) {
          auto perfCounter = core.perfCounter();
          if (perfCounter->initialize(mod, coreCounterStartEvents.at(i),
                                      mod, coreCounterEndEvents.at(i)) != XAIE_OK)
            break;
          if (perfCounter->reserve() != XAIE_OK) 
            break;

          // NOTE: store events for later use in trace
          XAie_Events counterEvent;
          perfCounter->getCounterEvent(mod, counterEvent);
          int idx = static_cast<int>(counterEvent) - static_cast<int>(XAIE_EVENT_PERF_CNT_0_CORE);
          perfCounter->changeThreshold(coreCounterEventValues.at(i));

          // Set reset event based on counter number
          perfCounter->changeRstEvent(mod, counterEvent);
          coreEvents.push_back(counterEvent);

          // If no memory counters are used, then we need to broadcast the core counter
          if (memoryCounterStartEvents.empty())
            memoryCrossEvents.push_back(counterEvent);

          if (perfCounter->start() != XAIE_OK) 
            break;

          mCoreCounterTiles.push_back(tile);
          mCoreCounters.push_back(perfCounter);
          numCoreCounters++;

          // Update config file
          uint8_t phyEvent = 0;
          auto& cfg = cfgTile->core_trace_config.pc[idx];
          XAie_EventLogicalToPhysicalConv(aieDevInst, loc, mod, coreCounterStartEvents[i], &phyEvent);
          cfg.start_event = phyEvent;
          XAie_EventLogicalToPhysicalConv(aieDevInst, loc, mod, coreCounterEndEvents[i], &phyEvent);
          cfg.stop_event = phyEvent;
          XAie_EventLogicalToPhysicalConv(aieDevInst, loc, mod, counterEvent, &phyEvent);
          cfg.reset_event = phyEvent;
          cfg.event_value = coreCounterEventValues[i];
        }
      }

      //
      // 2. Reserve and start memory module counters (as needed)
      //
      int numMemoryCounters = 0;
      {
        XAie_ModuleType mod = XAIE_MEM_MOD;

        for (int i=0; i < memoryCounterStartEvents.size(); ++i) {
          auto perfCounter = memory.perfCounter();
          if (perfCounter->initialize(mod, memoryCounterStartEvents.at(i),
                                      mod, memoryCounterEndEvents.at(i)) != XAIE_OK) 
            break;
          if (perfCounter->reserve() != XAIE_OK) 
            break;

          // Set reset event based on counter number
          XAie_Events counterEvent;
          perfCounter->getCounterEvent(mod, counterEvent);
          int idx = static_cast<int>(counterEvent) - static_cast<int>(XAIE_EVENT_PERF_CNT_0_MEM);
          perfCounter->changeThreshold(memoryCounterEventValues.at(i));

          perfCounter->changeRstEvent(mod, counterEvent);
          memoryEvents.push_back(counterEvent);

          if (perfCounter->start() != XAIE_OK) 
            break;

          mMemoryCounters.push_back(perfCounter);
          numMemoryCounters++;

          // Update config file
          uint8_t phyEvent = 0;
          auto& cfg = cfgTile->memory_trace_config.pc[idx];
          XAie_EventLogicalToPhysicalConv(aieDevInst, loc, mod, memoryCounterStartEvents[i], &phyEvent);
          cfg.start_event = phyEvent;
          XAie_EventLogicalToPhysicalConv(aieDevInst, loc, mod, memoryCounterEndEvents[i], &phyEvent);
          cfg.stop_event = phyEvent;
          XAie_EventLogicalToPhysicalConv(aieDevInst, loc, mod, counterEvent, &phyEvent);
          cfg.reset_event = phyEvent;
          cfg.event_value = memoryCounterEventValues[i];
        }
      }

      // Catch when counters cannot be reserved: report, release, and return
      if ((numCoreCounters < coreCounterStartEvents.size())
          || (numMemoryCounters < memoryCounterStartEvents.size())) {
        std::stringstream msg;
        msg << "Unable to reserve " << coreCounterStartEvents.size() << " core counters"
            << " and " << memoryCounterStartEvents.size() << " memory counters"
            << " for AIE tile (" << col << "," << row + 1 << ") required for trace.";
        xrt_core::message::send(severity_level::warning, "XRT", msg.str());

        releaseCurrentTileCounters(numCoreCounters, numMemoryCounters);
        // Print resources availability for this tile
        printTileStats(aieDevice, tile);
        return false;
      }

      //
      // 3. Configure Core Tracing Events
      //
      {
        XAie_ModuleType mod = XAIE_CORE_MOD;
        uint8_t phyEvent = 0;
        auto coreTrace = core.traceControl();

        // Delay cycles and user control are not compatible with each other
        if (xrt_core::config::get_aie_trace_user_control()) {
          coreTraceStartEvent = XAIE_EVENT_INSTR_EVENT_0_CORE;
          coreTraceEndEvent = XAIE_EVENT_INSTR_EVENT_1_CORE;
        } else if (useDelay) {
          auto perfCounter = core.perfCounter();
          if (perfCounter->initialize(mod, XAIE_EVENT_ACTIVE_CORE,
                                      mod, XAIE_EVENT_DISABLED_CORE) != XAIE_OK) 
            break;
          if (perfCounter->reserve() != XAIE_OK) 
            break;

          perfCounter->changeThreshold(delayCycles);
          XAie_Events counterEvent;
          perfCounter->getCounterEvent(mod, counterEvent);

          // Set reset and trace start using this counter
          perfCounter->changeRstEvent(mod, counterEvent);
          coreTraceStartEvent = counterEvent;
          // This is needed because the cores are started/stopped during execution
          // to get around some hw bugs. We cannot restart tracemodules when that happens
          coreTraceEndEvent = XAIE_EVENT_NONE_CORE;

          if (perfCounter->start() != XAIE_OK) 
            break;
        }

        // Set overall start/end for trace capture
        // Wendy said this should be done first
        if (coreTrace->setCntrEvent(coreTraceStartEvent, coreTraceEndEvent) != XAIE_OK) 
          break;

        auto ret = coreTrace->reserve();
        if (ret != XAIE_OK) {
          std::stringstream msg;
          msg << "Unable to reserve core module trace control for AIE tile (" 
              << col << "," << row + 1 << ").";
          xrt_core::message::send(severity_level::warning, "XRT", msg.str());

          releaseCurrentTileCounters(numCoreCounters, numMemoryCounters);
          // Print resources availability for this tile
          printTileStats(aieDevice, tile);
          return false;
        }

        int numTraceEvents = 0;
        for (int i=0; i < coreEvents.size(); i++) {
          uint8_t slot;
          if (coreTrace->reserveTraceSlot(slot) != XAIE_OK) 
            break;
          if (coreTrace->setTraceEvent(slot, coreEvents[i]) != XAIE_OK) 
            break;
          numTraceEvents++;

          // Update config file
          XAie_EventLogicalToPhysicalConv(aieDevInst, loc, mod, coreEvents[i], &phyEvent);
          cfgTile->core_trace_config.traced_events[slot] = phyEvent;
        }
        // Update config file
        XAie_EventLogicalToPhysicalConv(aieDevInst, loc, mod, coreTraceStartEvent, &phyEvent);
        cfgTile->core_trace_config.start_event = phyEvent;
        XAie_EventLogicalToPhysicalConv(aieDevInst, loc, mod, coreTraceEndEvent, &phyEvent);
        cfgTile->core_trace_config.stop_event = phyEvent;
        
        coreEvents.clear();
        numTileCoreTraceEvents[numTraceEvents]++;

        std::stringstream msg;
        msg << "Reserved " << numTraceEvents << " core trace events for AIE tile (" << col << "," << row << ").";
        xrt_core::message::send(severity_level::debug, "XRT", msg.str());

        if (coreTrace->setMode(XAIE_TRACE_EVENT_PC) != XAIE_OK) 
          break;
        XAie_Packet pkt = {0, 0};
        if (coreTrace->setPkt(pkt) != XAIE_OK) 
          break;
        if (coreTrace->start() != XAIE_OK) 
          break;
      }

      //
      // 4. Configure Memory Tracing Events
      //
      // TODO: Configure group or combo events where applicable
      uint32_t coreToMemBcMask = 0;
      {
        auto memoryTrace = memory.traceControl();
        // Set overall start/end for trace capture
        // Wendy said this should be done first
        if (memoryTrace->setCntrEvent(coreTraceStartEvent, coreTraceEndEvent) != XAIE_OK) 
          break;

        auto ret = memoryTrace->reserve();
        if (ret != XAIE_OK) {
          std::stringstream msg;
          msg << "Unable to reserve memory module trace control for AIE tile (" 
              << col << "," << row + 1 << ").";
          xrt_core::message::send(severity_level::warning, "XRT", msg.str());

          releaseCurrentTileCounters(numCoreCounters, numMemoryCounters);
          // Print resources availability for this tile
          printTileStats(aieDevice, tile);
          return false;
        }

        int numTraceEvents = 0;
        
        // Configure cross module events
        for (int i=0; i < memoryCrossEvents.size(); i++) {
          uint32_t bcBit = 0x1;
          auto TraceE = memory.traceEvent();
          TraceE->setEvent(XAIE_CORE_MOD, memoryCrossEvents[i]);
          if (TraceE->reserve() != XAIE_OK) 
            break;

          int bcId = TraceE->getBc();
          coreToMemBcMask |= (bcBit << bcId);

          if (TraceE->start() != XAIE_OK) 
            break;
          numTraceEvents++;

          // Update config file
          uint32_t S = 0;
          XAie_LocType L;
          XAie_ModuleType M;
          TraceE->getRscId(L, M, S);
          cfgTile->memory_trace_config.traced_events[S] = bcIdToEvent(bcId);
          auto mod = XAIE_CORE_MOD;
          uint8_t phyEvent = 0;
          XAie_EventLogicalToPhysicalConv(aieDevInst, loc, mod, memoryCrossEvents[i], &phyEvent);
          cfgTile->core_trace_config.internal_events_broadcast[bcId] = phyEvent;
        }

        // Configure same module events
        for (int i=0; i < memoryEvents.size(); i++) {
          auto TraceE = memory.traceEvent();
          TraceE->setEvent(XAIE_MEM_MOD, memoryEvents[i]);
          if (TraceE->reserve() != XAIE_OK) 
            break;
          if (TraceE->start() != XAIE_OK) 
            break;
          numTraceEvents++;

          // Update config file
          // Get Trace slot
          uint32_t S = 0;
          XAie_LocType L;
          XAie_ModuleType M;
          TraceE->getRscId(L, M, S);
          // Get Physical event
          auto mod = XAIE_MEM_MOD;
          uint8_t phyEvent = 0;
          XAie_EventLogicalToPhysicalConv(aieDevInst, loc, mod, memoryEvents[i], &phyEvent);
          cfgTile->memory_trace_config.traced_events[S] = phyEvent;
        }

        // Update config file
        {
          // Add Memory module trace control events
          uint32_t bcBit = 0x1;
          auto bcId = memoryTrace->getStartBc();
          coreToMemBcMask |= (bcBit << bcId);
          auto mod = XAIE_CORE_MOD;
          uint8_t phyEvent = 0;
          XAie_EventLogicalToPhysicalConv(aieDevInst, loc, mod, coreTraceStartEvent, &phyEvent);
          cfgTile->memory_trace_config.start_event = bcIdToEvent(bcId);
          cfgTile->core_trace_config.internal_events_broadcast[bcId] = phyEvent;

          bcBit = 0x1;
          bcId = memoryTrace->getStopBc();
          coreToMemBcMask |= (bcBit << bcId);
          XAie_EventLogicalToPhysicalConv(aieDevInst, loc, mod, coreTraceEndEvent, &phyEvent);
          cfgTile->memory_trace_config.stop_event = bcIdToEvent(bcId);
          cfgTile->core_trace_config.internal_events_broadcast[bcId] = phyEvent;
        }

        // Odd absolute rows change east mask end even row change west mask
        if ((row + 1) % 2) {
          cfgTile->core_trace_config.broadcast_mask_east = coreToMemBcMask;
        } else {
          cfgTile->core_trace_config.broadcast_mask_west = coreToMemBcMask;
        }
        // Done update config file

        memoryEvents.clear();
        numTileMemoryTraceEvents[numTraceEvents]++;

        std::stringstream msg;
        msg << "Reserved " << numTraceEvents << " memory trace events for AIE tile (" << col << "," << row << ").";
        xrt_core::message::send(severity_level::debug, "XRT", msg.str());

        if (memoryTrace->setMode(XAIE_TRACE_EVENT_TIME) != XAIE_OK) 
          break;
        XAie_Packet pkt = {0, 1};
        if (memoryTrace->setPkt(pkt) != XAIE_OK) 
          break;
        if (memoryTrace->start() != XAIE_OK) 
          break;

        // Update memory packet type in config file
        // NOTE: Use time packets for memory module (type 1)
        cfgTile->memory_trace_config.packet_type = 1;
      }

      std::stringstream msg;
      msg << "Adding tile (" << col << "," << row << ") to static database";
      xrt_core::message::send(severity_level::debug, "XRT", msg.str());

      // Add config info to static database
      // NOTE: Do not access cfgTile after this
      (db->getStaticInfo()).addAIECfgTile(deviceId, cfgTile);
    } // For tiles

    // Report trace events reserved per tile
    {
      std::stringstream msg;
      msg << "AIE trace events reserved in core modules - ";
      for (int n=0; n <= NUM_CORE_TRACE_EVENTS; ++n) {
        if (numTileCoreTraceEvents[n] == 0) continue;
        msg << n << ": " << numTileCoreTraceEvents[n] << " tiles";
        if (n != NUM_CORE_TRACE_EVENTS) msg << ", ";

        (db->getStaticInfo()).addAIECoreEventResources(deviceId, n, numTileCoreTraceEvents[n]);
      }
      xrt_core::message::send(severity_level::info, "XRT", msg.str());
    }
    {
      std::stringstream msg;
      msg << "AIE trace events reserved in memory modules - ";
      for (int n=0; n <= NUM_MEMORY_TRACE_EVENTS; ++n) {
        if (numTileMemoryTraceEvents[n] == 0) continue;
        msg << n << ": " << numTileMemoryTraceEvents[n] << " tiles";
        if (n != NUM_MEMORY_TRACE_EVENTS) msg << ", ";

        (db->getStaticInfo()).addAIEMemoryEventResources(deviceId, n, numTileMemoryTraceEvents[n]);
      }
      xrt_core::message::send(severity_level::info, "XRT", msg.str());
    }

    return true;
  } // end setMetrics

  void AieTracePlugin::updateAIEDevice(void* handle)
  {
    if (handle == nullptr)
      return;

    char pathBuf[512];
    memset(pathBuf, 0, 512);
    xclGetDebugIPlayoutPath(handle, pathBuf, 512);

    std::string sysfspath(pathBuf);

    uint64_t deviceId = db->addDevice(sysfspath); // Get the unique device Id

    deviceIdToHandle[deviceId] = handle;
    // handle is not added to "deviceHandles" as this is user provided handle, not owned by XDP

    if (!(db->getStaticInfo()).isDeviceReady(deviceId)) {
      // first delete the offloader, logger
      // Delete the old offloader as data is already from it
      if(aieOffloaders.find(deviceId) != aieOffloaders.end()) {
        auto entry = aieOffloaders[deviceId];

        auto aieOffloader = std::get<0>(entry);
        auto aieLogger    = std::get<1>(entry);

        delete aieOffloader;
        delete aieLogger;
        // don't delete DeviceIntf

        aieOffloaders.erase(deviceId);
      }

      // Update the static database with information from xclbin
      (db->getStaticInfo()).updateDevice(deviceId, handle);
      {
        struct xclDeviceInfo2 info;
        if(xclGetDeviceInfo2(handle, &info) == 0) {
          (db->getStaticInfo()).setDeviceName(deviceId, std::string(info.mName));
        }
      }
    }

    // Set metrics for counters and trace events 
    if (!setMetrics(deviceId, handle)) {
      std::string msg("Unable to configure AIE trace control and events. No trace will be generated.");
      xrt_core::message::send(severity_level::warning, "XRT", msg);
      return;
    }
    
    if (!(db->getStaticInfo()).isGMIORead(deviceId)) {
      // Update the AIE specific portion of the device
      // When new xclbin is loaded, the xclbin specific datastructure is already recreated
      std::shared_ptr<xrt_core::device> device = xrt_core::get_userpf_device(handle) ;
      if (device != nullptr) {
        for (auto& gmio : xrt_core::edge::aie::get_trace_gmios(device.get())) {
          (db->getStaticInfo()).addTraceGMIO(deviceId, gmio.id, gmio.shimColumn, gmio.channelNum, gmio.streamId, gmio.burstLength) ;
        }
      }
      (db->getStaticInfo()).setIsGMIORead(deviceId, true);
    }

    uint64_t numAIETraceOutput = (db->getStaticInfo()).getNumAIETraceStream(deviceId);
    if (numAIETraceOutput == 0) {
      // no AIE Trace Stream to offload trace, so return
      std::string msg("Neither PLIO nor GMIO trace infrastucture is found in the given design. So, AIE event trace will not be available.");
      xrt_core::message::send(severity_level::warning, "XRT", msg);
      return;
    }

    DeviceIntf* deviceIntf = (db->getStaticInfo()).getDeviceIntf(deviceId);
    if (deviceIntf == nullptr) {
      // If DeviceIntf is not already created, create a new one to communicate with physical device
      deviceIntf = new DeviceIntf();
      try {
        deviceIntf->setDevice(new HalDevice(handle));
        deviceIntf->readDebugIPlayout();
      } catch(std::exception& e) {
        // Read debug IP layout could throw an exception
        std::stringstream msg;
        msg << "Unable to read debug IP layout for device " << deviceId << ": " << e.what();
        xrt_core::message::send(severity_level::warning, "XRT", msg.str());
        delete deviceIntf;
        return;
      }
      (db->getStaticInfo()).setDeviceIntf(deviceId, deviceIntf);
      // configure dataflow etc. may not be required here as those are for PL side
    }

    // Create runtime config file
    if (runtimeMetrics) {
      std::string configFile = "aie_event_runtime_config.json";
      VPWriter* writer = new AieTraceConfigWriter(configFile.c_str(),
                                                  deviceId, metricSet) ;
      writers.push_back(writer);
      (db->getStaticInfo()).addOpenedFile(writer->getcurrentFileName(), "AIE_EVENT_RUNTIME_CONFIG");
    }

    // Create trace output files
    for(uint64_t n = 0; n < numAIETraceOutput; n++) {
      // Consider both Device Id and Stream Id to create the output file name
      std::string fileName = "aie_trace_" + std::to_string(deviceId) + "_" + std::to_string(n) + ".txt";
      VPWriter* writer = new AIETraceWriter(fileName.c_str(), deviceId, n,
                                            "" /*version*/,
                                            "" /*creationTime*/,
                                            "" /*xrtVersion*/,
                                            "" /*toolVersion*/);
      writers.push_back(writer);
      (db->getStaticInfo()).addOpenedFile(writer->getcurrentFileName(), "AIE_EVENT_TRACE");

      std::stringstream msg;
      msg << "Creating AIE trace file " << fileName << " for device " << deviceId;
      xrt_core::message::send(severity_level::info, "XRT", msg.str());
    }

    // Ensure trace buffer size is appropriate
    uint64_t aieTraceBufSize = GetTS2MMBufSize(true /*isAIETrace*/);
    bool isPLIO = (db->getStaticInfo()).getNumTracePLIO(deviceId) ? true : false;

    if (continuousTrace) {
      // Continuous Trace Offload is supported only for PLIO flow
      if (isPLIO) {
        XDPPlugin::startWriteThread(aie_trace_file_dump_int_s, "AIE_EVENT_TRACE", false);
      } else {
        std::string msg("Continuous offload of AIE Trace is not supported for GMIO mode. So, AIE Trace for GMIO mode will be offloaded only at the end of application.");
        xrt_core::message::send(severity_level::warning, "XRT", msg);
      }
    }

    // First, check against memory bank size
    // NOTE: Check first buffer for PLIO; assume bank 0 for GMIO
    uint8_t memIndex = isPLIO ? deviceIntf->getAIETs2mmMemIndex(0) : 0;
    Memory* memory = (db->getStaticInfo()).getMemory(deviceId, memIndex);
    if (memory != nullptr) {
      uint64_t fullBankSize = memory->size * 1024;
      
      if ((fullBankSize > 0) && (aieTraceBufSize > fullBankSize)) {
        aieTraceBufSize = fullBankSize;
        std::string msg = "Requested AIE trace buffer is too big for memory resource. Limiting to " + std::to_string(fullBankSize) + "." ;
        xrt_core::message::send(severity_level::warning, "XRT", msg);
      }
    }

    // Second, check against amount dedicated as device memory (Linux only)
#ifndef _WIN32
    try {
      std::string line;
      std::ifstream ifs;
      ifs.open("/proc/meminfo");
      while (getline(ifs, line)) {
        if (line.find("CmaTotal") == std::string::npos)
          continue;
          
        // Memory sizes are always expressed in kB
        std::vector<std::string> cmaVector;
        boost::split(cmaVector, line, boost::is_any_of(":"));
        auto deviceMemorySize = std::stoull(cmaVector.at(1)) * 1024;
        if (deviceMemorySize == 0)
          break;

        double percentSize = (100.0 * aieTraceBufSize) / deviceMemorySize;
        std::stringstream percentSizeStr;
        percentSizeStr << std::fixed << std::setprecision(3) << percentSize;

        // Limit size of trace buffer if requested amount is too high
        if (percentSize >= 80.0) {
          uint64_t newAieTraceBufSize = (uint64_t)std::ceil(0.8 * deviceMemorySize);
          aieTraceBufSize = newAieTraceBufSize;

          std::stringstream newBufSizeStr;
          newBufSizeStr << std::fixed << std::setprecision(3) << (newAieTraceBufSize / (1024.0 * 1024.0));
          
          std::string msg = "Requested AIE trace buffer is " + percentSizeStr.str() + "% of device memory."
              + " You may run into errors depending upon memory usage of your application."
              + " Limiting to " + newBufSizeStr.str() + " MB.";
          xrt_core::message::send(severity_level::warning, "XRT", msg);
        }
        else {
          std::string msg = "Requested AIE trace buffer is " + percentSizeStr.str() + "% of device memory.";
          xrt_core::message::send(severity_level::info, "XRT", msg);
        }
        
        break;
      }
      ifs.close();
    }
    catch (...) {
        // Do nothing
    }
#endif

    // Create AIE Trace Offloader
    AIETraceDataLogger* aieTraceLogger = new AIETraceDataLogger(deviceId);

    if (xrt_core::config::get_verbosity() >= static_cast<uint32_t>(severity_level::debug)) {
      std::string flowType = (isPLIO) ? "PLIO" : "GMIO";
      std::stringstream msg;
      msg << "Total size of " << std::fixed << std::setprecision(3) << (aieTraceBufSize / (1024.0 * 1024.0))
          << " MB is used for AIE trace buffer for " << std::to_string(numAIETraceOutput) << " " << flowType 
          << " streams.";
      xrt_core::message::send(severity_level::debug, "XRT", msg.str());
    }

    AIETraceOffload* aieTraceOffloader = new AIETraceOffload(handle, deviceId,
                                              deviceIntf, aieTraceLogger,
                                              isPLIO,              // isPLIO?
                                              aieTraceBufSize,     // total trace buffer size
                                              numAIETraceOutput);  // numStream

    if (!aieTraceOffloader->initReadTrace()) {
      std::string msg = "Allocation of buffer for AIE trace failed. AIE trace will not be available.";
      xrt_core::message::send(severity_level::warning, "XRT", msg);
      delete aieTraceOffloader;
      delete aieTraceLogger;
      return;
    }
    aieOffloaders[deviceId] = std::make_tuple(aieTraceOffloader, aieTraceLogger, deviceIntf);

    // Continuous Trace Offload is supported only for PLIO flow
    if (continuousTrace && isPLIO) {
      aieTraceOffloader->setContinuousTrace();
      aieTraceOffloader->setOffloadIntervalms(offloadIntervalms);
      aieTraceOffloader->startOffload();
    }
  }

  void AieTracePlugin::setFlushMetrics(uint64_t deviceId, void* handle)
  {
    XAie_DevInst* aieDevInst =
      static_cast<XAie_DevInst*>(db->getStaticInfo().getAieDevInst(fetchAieDevInst, handle)) ;
    xaiefal::XAieDev* aieDevice =
      static_cast<xaiefal::XAieDev*>(db->getStaticInfo().getAieDevice(allocateAieDevice, deallocateAieDevice, handle)) ;

    if (!aieDevInst || !aieDevice) {
      xrt_core::message::send(severity_level::warning, "XRT", 
          "Unable to get AIE device. There will be no flushing of AIE event trace.");
      return;
    }

    // New start & end events for trace control and counters
    coreTraceStartEvent = XAIE_EVENT_TIMER_SYNC_CORE;
    //coreTraceStartEvent = XAIE_EVENT_TRUE_CORE;
    coreTraceEndEvent   = XAIE_EVENT_TIMER_VALUE_REACHED_CORE;

    // Timer trigger value: 300*1020*1020 = 312,120,000 = 0x129A92C0
    // NOTES: Each packet has 7 payload words (one word: 32 bits)
    //        We need 64 packets, so we need 7 * 64 = 448 words.
    //        Existing counters generates 1.5 words for every perf 
    //        counter 2 triggers. Thus, 300 perf 2 counter events.
    //uint32_t timerTrigValueLow  = 0x129A92C0;
    //uint32_t timerTrigValueLow  = 0x4A6A4B00;
    uint32_t timerTrigValueLow  = 0x29A92C00;
    uint32_t timerTrigValueHigh = 0x1;

    uint32_t prevCol = 0;
    uint32_t prevRow = 0;
    constexpr int numFlushCounters = 2;

    // 1. Ensure we have counters, whether or not requested previously
    if (coreCounterStartEvents.size() < numFlushCounters) {
      XAie_ModuleType mod = XAIE_CORE_MOD;
      auto tiles = getTilesForTracing(handle);
      auto numCountersToRequest = numFlushCounters - coreCounterStartEvents.size();

      // For consistency, always use ES1 counter values for flushing
      ValueVector flushEventValues = {1020, 1020*1020};

      for (auto& tile : tiles) {
        auto& core = aieDevice->tile(tile.col, tile.row + 1).core();

        // We need two counters to flush out the trace
        for (int c=0; c < numCountersToRequest; ++c) {
          auto perfCounter = core.perfCounter();
          if (perfCounter->initialize(mod, XAIE_EVENT_NONE_CORE,
                                      mod, XAIE_EVENT_NONE_CORE) != XAIE_OK) 
            continue;
          if (perfCounter->reserve() != XAIE_OK) 
            continue;

          // Configure threshold and reset of counter
          XAie_Events counterEvent;
          perfCounter->getCounterEvent(mod, counterEvent);
          perfCounter->changeThreshold(flushEventValues.at(c));
          perfCounter->changeRstEvent(mod, counterEvent);

          // Add the counter event to trace so it forces the flush
          auto coreTrace = core.traceControl();
          coreTrace->stop();
          uint8_t slot = 0;
          if (coreTrace->reserveTraceSlot(slot) != XAIE_OK) 
            break;
          if (coreTrace->setTraceEvent(slot, counterEvent) != XAIE_OK) 
            break;

          mCoreCounters.push_back(perfCounter);
          mCoreCounterTiles.push_back(tile);
        }
      }
    }
    
    // Reconfigure profile counters
    for (int i=0; i < mCoreCounters.size(); ++i) {
      // 2. For every tile, stop trace & change trace start/stop and timer
      auto& tile = mCoreCounterTiles.at(i);
      auto  col  = tile.col;
      auto  row  = tile.row;
      if ((col != prevCol) || (row != prevRow)) {
        std::stringstream msg;
        msg << "AIE Trace Flush: Modifying control and timer for tile (" << col << "," << row << ")";
        xrt_core::message::send(severity_level::debug, "XRT", msg.str());

        prevCol        = col;
        prevRow        = row;
        auto& core     = aieDevice->tile(col, row + 1).core();
        auto coreTrace = core.traceControl();

        coreTrace->stop();
        coreTrace->setCntrEvent(coreTraceStartEvent, coreTraceEndEvent);
      }

      // 3. For every counter, change start/stop events
      std::stringstream msg;
      msg << "AIE Trace Flush: Modifying start/stop events for counter " << i;
      xrt_core::message::send(severity_level::debug, "XRT", msg.str());
      
      auto& counter = mCoreCounters.at(i);
      counter->stop();
      counter->changeStartEvent(XAIE_CORE_MOD, coreTraceStartEvent);
      counter->changeStopEvent(XAIE_CORE_MOD,  coreTraceEndEvent);
      counter->start();
    }

    // 4. For every tile, restart trace and reset timer
    prevCol = 0;
    prevRow = 0;
    for (int i=0; i < mCoreCounters.size(); ++i) {
      auto& tile = mCoreCounterTiles.at(i);
      auto  col  = tile.col;
      auto  row  = tile.row;
      if ((col != prevCol) || (row != prevRow)) {
        prevCol        = col;
        prevRow        = row;
        auto& core     = aieDevice->tile(col, row + 1).core();
        auto coreTrace = core.traceControl();
        coreTrace->start();

        XAie_LocType tileLocation = XAie_TileLoc(col, row + 1);
        XAie_SetTimerTrigEventVal(aieDevInst, tileLocation, XAIE_CORE_MOD,
                                  timerTrigValueLow, timerTrigValueHigh);
        XAie_ResetTimer(aieDevInst, tileLocation, XAIE_CORE_MOD);
      }
    }
  }

  void AieTracePlugin::flushAIEDevice(void* handle)
  {
    if (handle == nullptr)
      return;

    char pathBuf[512];
    memset(pathBuf, 0, 512);
    xclGetDebugIPlayoutPath(handle, pathBuf, 512);

    std::string sysfspath(pathBuf);

    uint64_t deviceId = db->addDevice(sysfspath); // Get the unique device Id

    if(aieOffloaders.find(deviceId) != aieOffloaders.end()) {
      (std::get<0>(aieOffloaders[deviceId]))->readTrace();
    }
  }

  void AieTracePlugin::finishFlushAIEDevice(void* handle)
  {
    if (handle == nullptr)
      return;
    
    char pathBuf[512];
    memset(pathBuf, 0, 512);
    xclGetDebugIPlayoutPath(handle, pathBuf, 512);

    std::string sysfspath(pathBuf);

    uint64_t deviceId = db->addDevice(sysfspath); // Get the unique device Id

    auto itr =  deviceIdToHandle.find(deviceId);
    if ((itr == deviceIdToHandle.end()) || (itr->second != handle))
      return;

    // Set metrics to flush the trace FIFOs
    // NOTE 1: The data mover uses a burst length of 128, so we need dummy packets
    //         to ensure all execution trace gets written to DDR.
    // NOTE 2: This flush mechanism is only valid for runtime event trace
    if (runtimeMetrics && xrt_core::config::get_aie_trace_flush()) {
      setFlushMetrics(deviceId, handle);
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if(aieOffloaders.find(deviceId) != aieOffloaders.end()) {
      auto offloader = std::get<0>(aieOffloaders[deviceId]);
      auto logger    = std::get<1>(aieOffloaders[deviceId]);

      if (offloader->continuousTrace()) {
        offloader->stopOffload() ;
        while(offloader->getOffloadStatus() != AIEOffloadThreadStatus::STOPPED) ;
      }

      offloader->readTrace();
      if (offloader->isTraceBufferFull())
        xrt_core::message::send(severity_level::warning, "XRT", AIE_TS2MM_WARN_MSG_BUF_FULL);
      offloader->endReadTrace();

      delete (offloader);
      delete (logger);

      aieOffloaders.erase(deviceId);
    }
    
  }

  void AieTracePlugin::writeAll(bool /*openNewFiles*/)
  {
    // read the trace data from device and wrie to the output file
    for(auto o : aieOffloaders) {
      auto offloader = std::get<0>(o.second);
      auto logger    = std::get<1>(o.second);

      if (offloader->continuousTrace()) {
        offloader->stopOffload() ;
        while(offloader->getOffloadStatus() != AIEOffloadThreadStatus::STOPPED) ;
      }

      offloader->readTrace();
      if (offloader->isTraceBufferFull())
        xrt_core::message::send(severity_level::warning, "XRT", AIE_TS2MM_WARN_MSG_BUF_FULL);
      offloader->endReadTrace();

      delete offloader;
      delete logger;
      // don't delete DeviceIntf
    }
    aieOffloaders.clear();

    XDPPlugin::endWrite();
  }

} // namespace xdp

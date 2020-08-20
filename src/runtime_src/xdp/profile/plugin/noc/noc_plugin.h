/**
 * Copyright (C) 2020 Xilinx, Inc
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

#ifndef XDP_NOC_PLUGIN_DOT_H
#define XDP_NOC_PLUGIN_DOT_H

#include <vector>
#include <string>
#include <thread>

#include "xdp/profile/plugin/vp_base/vp_base_plugin.h"
#include "xdp/config.h"

namespace xdp {

  class NOCProfilingPlugin : public XDPPlugin
  {
  public:
    NOCProfilingPlugin();
    ~NOCProfilingPlugin();

  private:
    void pollNOCCounters();

  private:
    // NOC profiling uses its own thread
    bool mKeepPolling;
    unsigned int mPollingInterval;
    std::thread mPollingThread;
    std::vector<std::string> mDevices;
  };

} // end namespace xdp

#endif

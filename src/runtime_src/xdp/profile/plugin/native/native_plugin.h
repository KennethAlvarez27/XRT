/**
 * Copyright (C) 2016-2022 Xilinx, Inc
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

#ifndef NATIVE_PLUGIN_DOT_H
#define NATIVE_PLUGIN_DOT_H

#include "xdp/profile/plugin/vp_base/vp_base_plugin.h"

namespace xdp {

  class NativeProfilingPlugin : public XDPPlugin
  {
  private:
    static bool live;
  public:
    NativeProfilingPlugin() ;
    ~NativeProfilingPlugin() ;

    static bool alive() { return NativeProfilingPlugin::live; }
  } ;

} // end namespace xdp

#endif

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

#ifndef XDP_AIE_TRACE_H
#define XDP_AIE_TRACE_H

namespace xdpaietrace {

  void load_xdp_aie_trace_plugin();
  void register_aie_trace_callbacks(void* handle);
  void aie_trace_warning_function();
  int  aie_trace_error_function();

} // end namespace xdpaietrace

namespace xdpaie {
    
  void update_aie_device(void* handle);
  void flush_aie_device(void* handle);
}

#endif


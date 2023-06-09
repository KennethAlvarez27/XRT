/**
 * Copyright (C) 2022 Xilinx, Inc
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

#ifndef HW_EMU_DEVICE_OFFLOAD_DOT_H
#define HW_EMU_DEVICE_OFFLOAD_DOT_H

namespace xdp {
namespace hal {
namespace hw_emu {
namespace device_offload {

  void load() ;
  void register_callbacks(void* handle) ;
  void warning_messages();

} // end namespace device_offload

  void update_device(void* handle) ;
  void flush_device(void* handle) ;

} // end namespace hw_emu
} // end namespace hal
} // end namespace xdp

#endif

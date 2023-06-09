/**
 * Copyright (C) 2020-2022 Xilinx, Inc
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

#ifndef HAL_PLUGIN_LOADER_DOT_H
#define HAL_PLUGIN_LOADER_DOT_H

namespace xdp {
namespace hal_hw_plugins {

  bool load();

} // end namespace hal_hw_plugins
namespace hal_hw_emu_plugins {

  bool load();

} // end namespace hal_hw_em_plugins
} // end namespace xdp

#endif

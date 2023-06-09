/**
 * Copyright (C) 2016-2020 Xilinx, Inc
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

#ifndef xrtx_message_h_
#define xrtx_message_h_

#include "core/common/message.h"
#include <string>

namespace xrt_xocl { namespace message {

using namespace xrt_core::message;

inline void
send(severity_level l, const char* msg)
{
  xrt_core::message::send(l,"XRT",msg);
};

inline void
send(severity_level l, const std::string& msg)
{
  send(l,msg.c_str());
}

}} // message,xrt

#endif

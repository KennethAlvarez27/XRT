/**
 * Copyright (C) 2017-2018 Xilinx, Inc
 * Author: Sonal Santan, Ryan Radjabi
 * Simple command line utility to inetract with SDX PCIe devices
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

#include "core/pcie/user_aws/awssak.h"

int main(int argc, char *argv[])
{
    try {
      return xcldev::xclAwssak(argc, argv);
    } catch (const std::exception& ex) {
      std::cout << ex.what() << std::endl;
    } catch(...) {
      std::cout << "Unknown exception caught from xcldev::xclAwssak()" << std::endl;
    }

    return 1;
}

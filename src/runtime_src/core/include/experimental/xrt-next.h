/*
 * Copyright (C) 2019, Xilinx Inc - All rights reserved
 * Xilinx Runtime (XRT) Experimental APIs
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

#ifndef _XRT_NEXT_APIS_H_
#define _XRT_NEXT_APIS_H_

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Place holder for experimental APIs
 */

/**
 * xclP2pEnable() - enable or disable p2p
 *
 * @handle:        Device handle
 * @enable:        false-disable, true-enable
 * @force:         true-force to reassign bus IO memory
 * Return:         0 on success or appropriate error number
 *
 * Enable or Disable P2P feature. Warm reboot might be required.
 */
XCL_DRIVER_DLLESPEC int xclP2pEnable(xclDeviceHandle handle, bool enable, bool force);

/*
 * API to get number of live processes on the given device.
 * This uses kdsstat information in sysfs.
 */

XCL_DRIVER_DLLESPEC uint xclGetNumLiveProcesses(xclDeviceHandle handle);

/**
 * xclGetSysfsPath() - Helper function to build a sysfs node full path
 *
 * @handle:              Device handle
 * @subdev:              Sub-device name
 * @entry:               Name of sysfs node
 * @sysfsPath:           Return string with full path for sysfs node
 * @size:                Length of the return string
 * Return:               0 or standard error number
 *
 * (For debug and profile usage only for now)
 * The sysfs information is not accessible above XRT layer now. However, debug/profile
 * needs information from sysfs (for example debug_ip_layout) to properly initialize
 * xdp code, so this helper API can be used
 */
XCL_DRIVER_DLLESPEC int xclGetSysfsPath(xclDeviceHandle handle, const char* subdev,
                                        const char* entry, char* sysfsPath, size_t size);

/**
 * Experimental APIs for reading debug and profile
 *
 * Warning: These APIs are experimental and can be
 * changed or removed at any time. They should only
 * be used by debug and profile code.
 *
 * @param handle the device handle
 * @param info the xclDebugProfileDeviceInfo
 * structure that this API will fill in as
 * result
 */
XCL_DRIVER_DLLESPEC int xclGetDebugProfileDeviceInfo(xclDeviceHandle handle, xclDebugProfileDeviceInfo* info);


struct KernelTransferData
{
   char* cuPortName;
   char* argName;
   char* memoryName;
   
   uint64_t totalReadBytes; 
   uint64_t totalReadTranx; 
   uint64_t totalReadLatency; 
   uint64_t totalReadBusyCycles; 
   uint64_t minReadLatency; 
   uint64_t maxReadLatency; 

   uint64_t totalWriteBytes; 
   uint64_t totalWriteTranx; 
   uint64_t totalWriteLatency; 
   uint64_t totalWriteBusyCycles; 
   uint64_t minWriteLatency; 
   uint64_t maxWriteLatency; 
};

struct CuExecData
{
   char* cuName;
   char* kernelName;

   uint64_t cuExecCount;
   uint64_t cuExecCycles;
   uint64_t cuBusyCycles;
   uint64_t cuMaxExecCycles;
   uint64_t cuMinExecCycles;
   uint64_t cuMaxParallelIter;
   uint64_t cuStallExtCycles;
   uint64_t cuStallIntCycles;
   uint64_t cuStallStrCycles;

}; 

struct StreamTransferData
{
  char* masterPortName;
  char* slavePortName;

  uint64_t strmNumTranx;
  uint64_t strmBusyCycles;
  uint64_t strmDataBytes;
  uint64_t strmStallCycles;
  uint64_t strmStarveCycles;
};

struct ProfileResults
{
  char* deviceName;

  uint64_t numAIM;
  KernelTransferData* kernelTransferData;

  uint64_t numAM;
  CuExecData* cuExecData;

  uint64_t numASM;
  StreamTransferData* streamData;
};


XCL_DRIVER_DLLESPEC int xclCreateProfileResults(xclDeviceHandle, ProfileResults**);
XCL_DRIVER_DLLESPEC int xclGetProfileResults(xclDeviceHandle, ProfileResults*);
XCL_DRIVER_DLLESPEC int xclDestroyProfileResults(xclDeviceHandle, ProfileResults*);

#ifdef __cplusplus
}
#endif

#endif

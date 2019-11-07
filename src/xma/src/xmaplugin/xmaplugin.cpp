/*
 * Copyright (C) 2018, Xilinx Inc - All rights reserved
 * Xilinx SDAccel Media Accelerator API
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
#include "xmaplugin.h"
#include "xrt.h"
#include "ert.h"
#include "lib/xmahw_lib.h"
//#include "lib/xmares.h"
#include "app/xma_utils.hpp"
#include "lib/xma_utils.hpp"

#include <cstdio>
#include <iostream>
#include <cstring>
#include <thread>
#include <chrono>
using namespace std;

#define XMAPLUGIN_MOD "xmapluginlib"

//Sarab: TODO .. Assign buffr object fields.. bank etc..
//Add new API for allocating device only buffer object.. which will not have host mappped buffer.. This could be used for zero copy plugins..
//NULL data pointer in buffer obj implies it is device only buffer..
//Remove read/write before SyncBO.. As plugin should manage that using host mapped data pointer..

XmaBufferObj
xma_plg_buffer_alloc(XmaSession s_handle, size_t size, bool device_only_buffer, int32_t* return_code)
{
    XmaBufferObj b_obj;
    XmaBufferObj b_obj_error;
    b_obj_error.data = NULL;
    //b_obj_error.ref_cnt = 0;
    b_obj_error.size = 0;
    b_obj_error.paddr = 0;
    b_obj_error.bank_index = -1;
    b_obj_error.dev_index = -1;
    b_obj_error.device_only_buffer = false;
    b_obj_error.private_do_not_touch = NULL;
    b_obj.data = NULL;
    //b_obj.ref_cnt = 0;
    b_obj.user_ptr = NULL;
    b_obj.device_only_buffer = false;
    b_obj.private_do_not_touch = NULL;

    XmaHwSessionPrivate *priv1 = (XmaHwSessionPrivate*) s_handle.hw_session.private_do_not_use;
    if (priv1 == NULL) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "xma_plg_buffer_alloc failed. XMASession is corrupted.\n");
        if (return_code) *return_code = XMA_ERROR;
        return b_obj_error;
    }
    xclDeviceHandle dev_handle = priv1->dev_handle;
    uint32_t ddr_bank = s_handle.hw_session.bank_index;
    b_obj.bank_index = ddr_bank;
    b_obj.size = size;
    b_obj.dev_index = s_handle.hw_session.dev_index;

    if (s_handle.session_signature != (void*)(((uint64_t)priv1) | ((uint64_t)priv1->reserved))) {
        //std::cout << "ERROR: xma_plg_buffer_alloc failed. XMASession is corrupted" << std::endl;
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "xma_plg_buffer_alloc failed. XMASession is corrupted.\n");
        if (return_code) *return_code = XMA_ERROR;
        return b_obj_error;
    }
    if (s_handle.session_type >= XMA_ADMIN) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "xma_plg_buffer_alloc can not be used for this XMASession type\n");
        if (return_code) *return_code = XMA_ERROR;
        return b_obj_error;
    }
    if (s_handle.hw_session.bank_index < 0) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "xma_plg_buffer_alloc can not be used for this XMASession as kernel not connected to any DDR\n");
        if (return_code) *return_code = XMA_ERROR;
        return b_obj_error;
    }

    /*
    #define XRT_BO_FLAGS_MEMIDX_MASK        (0xFFFFFFUL)
    #define XCL_BO_FLAGS_CACHEABLE          (1 << 24)
    #define XCL_BO_FLAGS_SVM                (1 << 27)
    #define XCL_BO_FLAGS_DEV_ONLY           (1 << 28)
    #define XCL_BO_FLAGS_HOST_ONLY          (1 << 29)
    #define XCL_BO_FLAGS_P2P                (1 << 30)
    #define XCL_BO_FLAGS_EXECBUF            (1 << 31)
    */
    uint64_t b_obj_handle = 0;
    if (device_only_buffer) {
        b_obj_handle = xclAllocBO(dev_handle, size, 0, XCL_BO_FLAGS_DEV_ONLY | ddr_bank);
        b_obj.device_only_buffer = true;
    } else {
        b_obj_handle = xclAllocBO(dev_handle, size, 0, ddr_bank);
    }
    /*BO handlk is uint64_t
    if (b_obj_handle < 0) {
        std::cout << "ERROR: xma_plg_buffer_alloc failed. handle=0x" << std::hex << b_obj_handle << std::endl;
        //printf("xclAllocBO failed. handle=0x%ullx\n", b_obj_handle);
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "xclAllocBO failed.\n");
        if (return_code) *return_code = XMA_ERROR;
        return b_obj_error;
    }
    */
    b_obj.paddr = xclGetDeviceAddr(dev_handle, b_obj_handle);
    if (!device_only_buffer) {
        b_obj.data = (uint8_t*) xclMapBO(dev_handle, b_obj_handle, true);
    }
    XmaBufferObjPrivate* tmp1 = new XmaBufferObjPrivate;
    b_obj.private_do_not_touch = (void*) tmp1;
    tmp1->dummy = (void*)(((uint64_t)tmp1) | signature);
    tmp1->size = size;
    tmp1->paddr = b_obj.paddr;
    tmp1->bank_index = b_obj.bank_index;
    tmp1->dev_index = b_obj.dev_index;
    tmp1->boHandle = b_obj_handle;
    tmp1->device_only_buffer = b_obj.device_only_buffer;
    tmp1->dev_handle = dev_handle;

    if (return_code) *return_code = XMA_SUCCESS;
    return b_obj;
}

XmaBufferObj xma_plg_buffer_alloc_arg_num(XmaSession s_handle, size_t size, bool device_only_buffer, int32_t arg_num, int32_t* return_code)
{
    XmaBufferObj b_obj;
    XmaBufferObj b_obj_error;
    b_obj_error.data = NULL;
    //b_obj_error.ref_cnt = 0;
    b_obj_error.size = 0;
    b_obj_error.paddr = 0;
    b_obj_error.bank_index = -1;
    b_obj_error.dev_index = -1;
    b_obj_error.device_only_buffer = false;
    b_obj_error.private_do_not_touch = NULL;
    b_obj_error.user_ptr = NULL;
    b_obj.data = NULL;
    //b_obj.ref_cnt = 0;
    b_obj.user_ptr = NULL;
    b_obj.device_only_buffer = false;
    b_obj.private_do_not_touch = NULL;

    XmaHwSessionPrivate *priv1 = (XmaHwSessionPrivate*) s_handle.hw_session.private_do_not_use;
    if (priv1 == NULL) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "xma_plg_buffer_alloc_arg_num failed. XMASession is corrupted.\n");
        if (return_code) *return_code = XMA_ERROR;
        return b_obj_error;
    }
    xclDeviceHandle dev_handle = priv1->dev_handle;
    uint32_t ddr_bank = s_handle.hw_session.bank_index;
    b_obj.bank_index = ddr_bank;
    b_obj.size = size;
    b_obj.dev_index = s_handle.hw_session.dev_index;

    if (s_handle.session_signature != (void*)(((uint64_t)priv1) | ((uint64_t)priv1->reserved))) {
        //std::cout << "ERROR: xma_plg_buffer_alloc failed. XMASession is corrupted" << std::endl;
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "xma_plg_buffer_alloc_arg_num failed. XMASession is corrupted.\n");
        if (return_code) *return_code = XMA_ERROR;
        return b_obj_error;
    }
    if (s_handle.session_type >= XMA_ADMIN) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "xma_plg_buffer_alloc_arg_num can not be used for this XMASession type\n");
        if (return_code) *return_code = XMA_ERROR;
        return b_obj_error;
    }

    XmaHwKernel* kernel_info = priv1->kernel_info;
    if (arg_num < 0) {
        xma_logmsg(XMA_WARNING_LOG, XMAPLUGIN_MOD, "xma_plg_buffer_alloc_arg_num: arg_num is invalid, using default session ddr_bank.\n");
    } else {
        auto arg_to_mem_itr1 = kernel_info->CU_arg_to_mem_info.find(arg_num);
        if (arg_to_mem_itr1 == kernel_info->CU_arg_to_mem_info.end()) {
            xma_logmsg(XMA_WARNING_LOG, XMAPLUGIN_MOD, "xma_plg_buffer_alloc_arg_num: arg_num is not connected to any DDR bank, using default session ddr_bank.\n");
        } else {
            ddr_bank = arg_to_mem_itr1->second;
            b_obj.bank_index = ddr_bank;
            xma_logmsg(XMA_DEBUG_LOG, XMAPLUGIN_MOD, "xma_plg_buffer_alloc_arg_num: Using ddr_bank# %d connected to arg_num# %d.\n", ddr_bank, arg_num);
        }
    }

    /*
    #define XRT_BO_FLAGS_MEMIDX_MASK        (0xFFFFFFUL)
    #define XCL_BO_FLAGS_CACHEABLE          (1 << 24)
    #define XCL_BO_FLAGS_SVM                (1 << 27)
    #define XCL_BO_FLAGS_DEV_ONLY           (1 << 28)
    #define XCL_BO_FLAGS_HOST_ONLY          (1 << 29)
    #define XCL_BO_FLAGS_P2P                (1 << 30)
    #define XCL_BO_FLAGS_EXECBUF            (1 << 31)
    */
    uint64_t b_obj_handle = 0;
    if (device_only_buffer) {
        b_obj_handle = xclAllocBO(dev_handle, size, 0, XCL_BO_FLAGS_DEV_ONLY | ddr_bank);
        b_obj.device_only_buffer = true;
    } else {
        b_obj_handle = xclAllocBO(dev_handle, size, 0, ddr_bank);
    }
    b_obj.paddr = xclGetDeviceAddr(dev_handle, b_obj_handle);
    if (!device_only_buffer) {
        b_obj.data = (uint8_t*) xclMapBO(dev_handle, b_obj_handle, true);
    }
    XmaBufferObjPrivate* tmp1 = new XmaBufferObjPrivate;
    b_obj.private_do_not_touch = (void*) tmp1;
    tmp1->dummy = (void*)(((uint64_t)tmp1) | signature);
    tmp1->size = size;
    tmp1->paddr = b_obj.paddr;
    tmp1->bank_index = b_obj.bank_index;
    tmp1->dev_index = b_obj.dev_index;
    tmp1->boHandle = b_obj_handle;
    tmp1->device_only_buffer = b_obj.device_only_buffer;
    tmp1->dev_handle = dev_handle;

    if (return_code) *return_code = XMA_SUCCESS;
    return b_obj;
}

XmaBufferObj
xma_plg_buffer_alloc_ddr(XmaSession s_handle, size_t size, bool device_only_buffer, int32_t ddr_index, int32_t* return_code)
{
    XmaBufferObj b_obj;
    XmaBufferObj b_obj_error;
    b_obj_error.data = NULL;
    //b_obj_error.ref_cnt = 0;
    b_obj_error.size = 0;
    b_obj_error.paddr = 0;
    b_obj_error.bank_index = -1;
    b_obj_error.dev_index = -1;
    b_obj_error.device_only_buffer = false;
    b_obj_error.private_do_not_touch = NULL;
    b_obj_error.user_ptr = NULL;
    b_obj.data = NULL;
    //b_obj.ref_cnt = 0;
    b_obj.user_ptr = NULL;
    b_obj.device_only_buffer = false;
    b_obj.private_do_not_touch = NULL;

    XmaHwSessionPrivate *priv1 = (XmaHwSessionPrivate*) s_handle.hw_session.private_do_not_use;
    if (priv1 == NULL) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "xma_plg_buffer_alloc_ddr failed. XMASession is corrupted.\n");
        if (return_code) *return_code = XMA_ERROR;
        return b_obj_error;
    }
    xclDeviceHandle dev_handle = priv1->dev_handle;
    uint32_t ddr_bank = ddr_index;
    b_obj.bank_index = ddr_bank;
    b_obj.size = size;
    b_obj.dev_index = s_handle.hw_session.dev_index;

    if (s_handle.session_signature != (void*)(((uint64_t)priv1) | ((uint64_t)priv1->reserved))) {
        //std::cout << "ERROR: xma_plg_buffer_alloc failed. XMASession is corrupted" << std::endl;
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "xma_plg_buffer_alloc_ddr failed. XMASession is corrupted.\n");
        if (return_code) *return_code = XMA_ERROR;
        return b_obj_error;
    }
    if (s_handle.session_type != XMA_ADMIN) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "xma_plg_buffer_alloc_ddr can be used only for XMA_ADMIN session type\n");
        if (return_code) *return_code = XMA_ERROR;
        return b_obj_error;
    }

    //Use this lambda func to print ddr info
    auto print_ddrs = [&](XmaLogLevelType log_level, XmaHwDevice     *device) {
        uint32_t tmp_int1 = 0;
        for (auto& ddr: device->ddrs) {
            if (ddr.in_use) {
                xma_logmsg(log_level, XMAPLUGIN_MOD,"\tMEM# %d - %s - size: %lu KB", tmp_int1, (char*)ddr.name, ddr.size_kb);
            } else {
                xma_logmsg(log_level, XMAPLUGIN_MOD,"\tMEM# %d - %s - size: UnUsed", tmp_int1, (char*)ddr.name);
            }
            tmp_int1++;
        }
        return; 
    };

    if ((uint32_t)ddr_index >= priv1->device->ddrs.size() || ddr_index < 0) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "xma_plg_buffer_alloc_ddr failed. Invalid DDR index.\nAvailable DDRs are:");
        print_ddrs(XMA_ERROR_LOG, priv1->device);
        if (return_code) *return_code = XMA_ERROR;
        return b_obj_error;
    }
    if (!priv1->device->ddrs[ddr_bank].in_use) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "xma_plg_buffer_alloc_ddr failed. This DDR is UnUsed.\nAvailable DDRs are:");
        print_ddrs(XMA_ERROR_LOG, priv1->device);
        if (return_code) *return_code = XMA_ERROR;
        return b_obj_error;
    }

    /*
    #define XRT_BO_FLAGS_MEMIDX_MASK        (0xFFFFFFUL)
    #define XCL_BO_FLAGS_CACHEABLE          (1 << 24)
    #define XCL_BO_FLAGS_SVM                (1 << 27)
    #define XCL_BO_FLAGS_DEV_ONLY           (1 << 28)
    #define XCL_BO_FLAGS_HOST_ONLY          (1 << 29)
    #define XCL_BO_FLAGS_P2P                (1 << 30)
    #define XCL_BO_FLAGS_EXECBUF            (1 << 31)
    */
    uint64_t b_obj_handle = 0;
    if (device_only_buffer) {
        b_obj_handle = xclAllocBO(dev_handle, size, 0, XCL_BO_FLAGS_DEV_ONLY | ddr_bank);
        b_obj.device_only_buffer = true;
    } else {
        b_obj_handle = xclAllocBO(dev_handle, size, 0, ddr_bank);
    }
    /*BO handlk is uint64_t
    if (b_obj_handle < 0) {
        std::cout << "ERROR: xma_plg_buffer_alloc failed. handle=0x" << std::hex << b_obj_handle << std::endl;
        //printf("xclAllocBO failed. handle=0x%ullx\n", b_obj_handle);
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "xclAllocBO failed.\n");
        if (return_code) *return_code = XMA_ERROR;
        return b_obj_error;
    }
    */
    b_obj.paddr = xclGetDeviceAddr(dev_handle, b_obj_handle);
    if (!device_only_buffer) {
        b_obj.data = (uint8_t*) xclMapBO(dev_handle, b_obj_handle, true);
    }
    XmaBufferObjPrivate* tmp1 = new XmaBufferObjPrivate;
    b_obj.private_do_not_touch = (void*) tmp1;
    tmp1->dummy = (void*)(((uint64_t)tmp1) | signature);
    tmp1->size = size;
    tmp1->paddr = b_obj.paddr;
    tmp1->bank_index = b_obj.bank_index;
    tmp1->dev_index = b_obj.dev_index;
    tmp1->boHandle = b_obj_handle;
    tmp1->device_only_buffer = b_obj.device_only_buffer;
    tmp1->dev_handle = dev_handle;

    if (return_code) *return_code = XMA_SUCCESS;
    return b_obj;
}

int32_t xma_check_device_buffer(XmaBufferObj *b_obj) {
    if (b_obj == NULL) {
        //std::cout << "ERROR: xma_device_buffer_free failed. XMABufferObj failed allocation" << std::endl;
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "xma_device_buffer_free failed. XMABufferObj failed allocation\n");
        return XMA_ERROR;
    }

    XmaBufferObjPrivate* b_obj_priv = (XmaBufferObjPrivate*) b_obj->private_do_not_touch;
    if (b_obj_priv == NULL) {
        //std::cout << "ERROR: xma_device_buffer_free failed. XMABufferObj failed allocation" << std::endl;
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "xma_device_buffer_free failed. XMABufferObj failed allocation\n");
        return XMA_ERROR;
    }
    if (b_obj_priv->dev_index < 0 || b_obj_priv->bank_index < 0 || b_obj_priv->size <= 0) {
        //std::cout << "ERROR: xma_device_buffer_free failed. XMABufferObj failed allocation" << std::endl;
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "xma_device_buffer_free failed. XMABufferObj failed allocation\n");
        return XMA_ERROR;
    }
    if (b_obj_priv->dummy != (void*)(((uint64_t)b_obj_priv) | signature)) {
        //std::cout << "ERROR: xma_device_buffer_free failed. XMABufferObj is corrupted" << std::endl;
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "xma_device_buffer_free failed. XMABufferObj is corrupted.\n");
        return XMA_ERROR;
    }
    if (b_obj_priv->dev_handle == NULL) {
        //std::cout << "ERROR: xma_device_buffer_free failed. XMABufferObj is corrupted" << std::endl;
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "xma_device_buffer_free failed. XMABufferObj is corrupted.\n");
        return XMA_ERROR;
    }
    return XMA_SUCCESS;
}

void
xma_plg_buffer_free(XmaSession s_handle, XmaBufferObj b_obj)
{
    XmaHwSessionPrivate *priv1 = (XmaHwSessionPrivate*) s_handle.hw_session.private_do_not_use;
    if (priv1 == NULL) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "xma_plg_buffer_free failed. XMASession is corrupted.\n");
        return;
    }
    if (s_handle.session_signature != (void*)(((uint64_t)priv1) | ((uint64_t)priv1->reserved))) {
        //std::cout << "ERROR: xma_plg_buffer_free failed. XMASession is corrupted" << std::endl;
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "xma_plg_buffer_free failed. XMASession is corrupted.\n");
        return;
    }
    if (xma_check_device_buffer(&b_obj) != XMA_SUCCESS) {
        return;
    }
    XmaBufferObjPrivate* b_obj_priv = (XmaBufferObjPrivate*) b_obj.private_do_not_touch;
    //xclDeviceHandle dev_handle = s_handle.hw_session.dev_handle;
    xclFreeBO(b_obj_priv->dev_handle, b_obj_priv->boHandle);
    b_obj_priv->dummy = NULL;
    b_obj_priv->size = -1;
    b_obj_priv->bank_index = -1;
    b_obj_priv->dev_index = -1;
    delete b_obj_priv;
}

int32_t
xma_plg_buffer_write(XmaSession s_handle,
                     XmaBufferObj  b_obj,
                     size_t           size,
                     size_t           offset)
{
    int32_t rc;
    XmaHwSessionPrivate *priv1 = (XmaHwSessionPrivate*) s_handle.hw_session.private_do_not_use;
    if (priv1 == NULL) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "xma_plg_buffer_write failed. XMASession is corrupted.\n");
        return XMA_ERROR;
    }
    if (s_handle.session_signature != (void*)(((uint64_t)priv1) | ((uint64_t)priv1->reserved))) {
        //std::cout << "ERROR: xma_plg_buffer_write failed. XMASession is corrupted" << std::endl;
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "xma_plg_buffer_write failed. XMASession is corrupted.\n");
        return XMA_ERROR;
    }
    if (xma_check_device_buffer(&b_obj) != XMA_SUCCESS) {
        return XMA_ERROR;
    }
    XmaBufferObjPrivate* b_obj_priv = (XmaBufferObjPrivate*) b_obj.private_do_not_touch;
    if (b_obj_priv->device_only_buffer) {
        xma_logmsg(XMA_WARNING_LOG, XMAPLUGIN_MOD, "xma_plg_buffer_write skipped as it is device only buffer.\n");
        return XMA_SUCCESS;
    }
    if (size + offset > b_obj_priv->size) {
        //std::cout << "ERROR: xma_plg_buffer_write failed. Can not write past end of buffer" << std::endl;
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "xma_plg_buffer_write failed. Can not write past end of buffer.\n");
        return XMA_ERROR;
    }
    //xclDeviceHandle dev_handle = s_handle.hw_session.dev_handle;

    //printf("xma_plg_buffer_write b_obj=%d,src=%p,size=%lu,offset=%lx\n", b_obj, src, size, offset);
    rc = xclSyncBO(b_obj_priv->dev_handle, b_obj_priv->boHandle, XCL_BO_SYNC_BO_TO_DEVICE, size, offset);
    if (rc != 0) {
        //std::cout << "ERROR: xma_plg_buffer_write xclSyncBO failed " << std::dec << rc << std::endl;
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "xclSyncBO failed %d\n", rc);
    }

    return XMA_SUCCESS;
}

int32_t
xma_plg_buffer_read(XmaSession s_handle,
                    XmaBufferObj  b_obj,
                    size_t           size,
                    size_t           offset)
{
    int32_t rc;
    XmaHwSessionPrivate *priv1 = (XmaHwSessionPrivate*) s_handle.hw_session.private_do_not_use;
    if (priv1 == NULL) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "xma_plg_buffer_read failed. XMASession is corrupted.\n");
        return XMA_ERROR;
    }
    if (s_handle.session_signature != (void*)(((uint64_t)priv1) | ((uint64_t)priv1->reserved))) {
        //std::cout << "ERROR: xma_plg_buffer_read failed. XMASession is corrupted" << std::endl;
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "xma_plg_buffer_read failed. XMASession is corrupted.\n");
        return XMA_ERROR;
    }
    if (xma_check_device_buffer(&b_obj) != XMA_SUCCESS) {
        return XMA_ERROR;
    }
    XmaBufferObjPrivate* b_obj_priv = (XmaBufferObjPrivate*) b_obj.private_do_not_touch;
    if (b_obj_priv->device_only_buffer) {
        xma_logmsg(XMA_WARNING_LOG, XMAPLUGIN_MOD, "xma_plg_buffer_read skipped as it is device only buffer.\n");
        return XMA_SUCCESS;
    }
    if (size + offset > b_obj_priv->size) {
        //std::cout << "ERROR: xma_plg_buffer_read failed. Can not read past end of buffer" << std::endl;
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "xma_plg_buffer_read failed. Can not read past end of buffer.\n");
        return XMA_ERROR;
    }

    //xclDeviceHandle dev_handle = s_handle.hw_session.dev_handle;

    //printf("xma_plg_buffer_read b_obj=%d,dst=%p,size=%lu,offset=%lx\n",
    //       b_obj, dst, size, offset);
    rc = xclSyncBO(b_obj_priv->dev_handle, b_obj_priv->boHandle, XCL_BO_SYNC_BO_FROM_DEVICE,
                   size, offset);
    if (rc != 0)
    {
        //std::cout << "ERROR: xma_plg_buffer_read xclSyncBO failed " << std::dec << rc << std::endl;
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "xma_plg_buffer_read xclSyncBO failed. Check device status with \"xbutil/awssak query\" cmmand\n");
        return XMA_ERROR;
    }

    return XMA_SUCCESS;
}

int32_t xma_plg_execbo_avail_get(XmaSession s_handle)
{
    XmaHwSessionPrivate *priv1 = (XmaHwSessionPrivate*) s_handle.hw_session.private_do_not_use;
    //std::cout << "Sarab: Debug - " << __func__ << "; " << __LINE__ << std::endl;
    XmaHwKernel* kernel_tmp1 = priv1->kernel_info;
    XmaHwDevice *dev_tmp1 = priv1->device;
    if (dev_tmp1 == NULL) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "Session XMA private pointer is NULL\n");
        return -1;
    }
    int32_t num_execbo = priv1->num_execbo_allocated;
    if (num_execbo <= 0) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "Session XMA private: No execbo allocated\n");
        return -1;
    }
    int32_t i;
    int32_t rc = -1;
    bool    found = false;
    //NOTE: execbo lock must be already acquired

    for (i = 0; i < num_execbo; i++) {
        XmaHwExecBO* execbo_tmp1 = &priv1->kernel_execbos[i];
        if (!execbo_tmp1->in_use) {
            found = true;
        }

        if (found) {
            execbo_tmp1->in_use = true;
            execbo_tmp1->cu_index = kernel_tmp1->cu_index;
            execbo_tmp1->session_id = s_handle.session_id;
            rc = i;
            break;
        }
    }
    //std::cout << "Sarab: Debug - " << __func__ << "; " << __LINE__ << std::endl;

    return rc;
}

XmaCUCmdObj xma_plg_schedule_work_item(XmaSession s_handle,
                                 void            *regmap,
                                 int32_t         regmap_size,
                                 int32_t*   return_code)
{
    XmaCUCmdObj cmd_obj_error;
    cmd_obj_error.cmd_id1 = 0;
    cmd_obj_error.cmd_id2 = 0;
    cmd_obj_error.cmd_finished = false;
    cmd_obj_error.cu_index = -1;
    cmd_obj_error.do_not_use1 = NULL;

    XmaHwSessionPrivate *priv1 = (XmaHwSessionPrivate*) s_handle.hw_session.private_do_not_use;
    if (priv1 == NULL) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "xma_plg_schedule_work_item failed. XMASession is corrupted.\n");
        if (return_code) *return_code = XMA_ERROR;
        return cmd_obj_error;
    }
    if (s_handle.session_signature != (void*)(((uint64_t)priv1) | ((uint64_t)priv1->reserved))) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "xma_plg_schedule_work_item failed. XMASession is corrupted.\n");
        if (return_code) *return_code = XMA_ERROR;
        return cmd_obj_error;
    }
    if (s_handle.session_type >= XMA_ADMIN) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "xma_plg_schedule_work_item can not be used for this XMASession type\n");
        if (return_code) *return_code = XMA_ERROR;
        return cmd_obj_error;
    }

    XmaHwKernel* kernel_tmp1 = priv1->kernel_info;
    XmaHwDevice *dev_tmp1 = priv1->device;
    if (dev_tmp1 == NULL) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "Session XMA private pointer is NULL\n");
        if (return_code) *return_code = XMA_ERROR;
        return cmd_obj_error;
    }
    if (regmap == NULL) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "regmap is NULL\n");
        if (return_code) *return_code = XMA_ERROR;
        return cmd_obj_error;
    }
    if (regmap_size <= 0) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "regmap_size of %d is invalid\n", regmap_size);
        if (return_code) *return_code = XMA_ERROR;
        return cmd_obj_error;
    }
    //Kernel regmap 4KB in xmahw.h; execBO size is 4096 = 4KB in xmahw_hal.cpp; But ERT uses some space for ert pkt so allow max of 4032 Bytes for regmap
    if (regmap_size > MAX_KERNEL_REGMAP_SIZE) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "Max kernel regmap size is %d Bytes\n", MAX_KERNEL_REGMAP_SIZE);
        if (return_code) *return_code = XMA_ERROR;
        return cmd_obj_error;
    }
    if ((uint32_t)regmap_size != ((uint32_t)regmap_size & 0xFFFFFFFC)) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "regmap_size of %d is not a multiple of four bytes\n", regmap_size);
        if (return_code) *return_code = XMA_ERROR;
        return cmd_obj_error;
    }
    if (kernel_tmp1->regmap_size > 0) {
        if (regmap_size > kernel_tmp1->regmap_size) {
            xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "Can not exceed kernel register_map size. Kernel regamp_size: %d, trying to use size: %d\n", kernel_tmp1->regmap_size, regmap_size);
            /*Sarab TODO
            if (return_code) *return_code = XMA_ERROR;
            return cmd_obj_error;
            */
        }
    }

    uint8_t *src = (uint8_t*)regmap;
    int32_t bo_idx;
    
    bool expected = false;
    bool desired = true;
    while (!priv1->execbo_locked.compare_exchange_weak(expected, desired)) {
        //std::this_thread::sleep_for(std::chrono::milliseconds(1));
        expected = false;
    }
    //kernel completion lock acquired
    //xma_logmsg(XMA_DEBUG_LOG, XMAPLUGIN_MOD, "1. Num of cmds in-progress = %lu\n", priv1->CU_cmds.size());

    // Find an available execBO buffer
    bo_idx = xma_plg_execbo_avail_get(s_handle);
    if (bo_idx == -1) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "Unable to find free execbo to use\n");
        priv1->execbo_locked = false;
        if (return_code) *return_code = XMA_ERROR;
        return cmd_obj_error;
    }
    // Setup ert_start_kernel_cmd 
    ert_start_kernel_cmd *cu_cmd = 
        (ert_start_kernel_cmd*)priv1->kernel_execbos[bo_idx].data;
    cu_cmd->state = ERT_CMD_STATE_NEW;
    if (kernel_tmp1->soft_kernel) {
        cu_cmd->opcode = ERT_SK_START;
    } else {
        cu_cmd->opcode = ERT_START_CU;
    }
    cu_cmd->extra_cu_masks = 3;//XMA now supports 128 CUs

    cu_cmd->cu_mask = kernel_tmp1->cu_mask0;

    cu_cmd->data[0] = kernel_tmp1->cu_mask1;
    cu_cmd->data[1] = kernel_tmp1->cu_mask2;
    cu_cmd->data[2] = kernel_tmp1->cu_mask3;
    // Copy reg_map into execBO buffer 
    memcpy(&cu_cmd->data[3], src, regmap_size);
    xma_logmsg(XMA_DEBUG_LOG, XMAPLUGIN_MOD, "Dev# %d; Kernel: %s; Regmap size used is: %d\n", dev_tmp1->dev_index, kernel_tmp1->name, regmap_size);

    if (kernel_tmp1->arg_start > 0) {
        uint32_t tmp_int1 = 3 + (kernel_tmp1->arg_start / 4);
        for (uint32_t i = 3; i < tmp_int1; i++) {
            cu_cmd->data[i] = 0;
        }
    }
    if (kernel_tmp1->kernel_channels) {
        // XMA will write @ 0x10 and XRT read @ 0x14 to generate interupt and capture in execbo
        cu_cmd->data[7] = s_handle.channel_id;//0x10 == 4th integer;
        cu_cmd->data[8] = 0;//clear out the output
        xma_logmsg(XMA_DEBUG_LOG, XMAPLUGIN_MOD, "This is dataflow kernel. Using channel id: %d\n", s_handle.channel_id);
    }
    
    // Set count to size in 32-bit words + 4; Three extra_cu_mask are present
    cu_cmd->count = (regmap_size >> 2) + 4;
    
    if (xclExecBuf(priv1->dev_handle, 
                    priv1->kernel_execbos[bo_idx].handle) != 0)
    {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD,
                    "Failed to submit kernel start with xclExecBuf\n");
        priv1->execbo_locked = false;
        if (return_code) *return_code = XMA_ERROR;
        return cmd_obj_error;
    }
    
    XmaCUCmdObj cmd_obj;
    cmd_obj.cmd_id1 = 0;
    cmd_obj.cmd_id2 = 0;
    cmd_obj.cmd_finished = false;
    cmd_obj.cu_index = kernel_tmp1->cu_index;
    cmd_obj.do_not_use1 = s_handle.session_signature;

    bool found = false;
    while(!found) {
        dev_tmp1->cu_cmd_id1++;
        uint32_t tmp_int1 = dev_tmp1->cu_cmd_id1;
        if (tmp_int1 == 0) {
            tmp_int1 = 1;
            dev_tmp1->cu_cmd_id1 = tmp_int1;
            //Change seed of random_generator
            std::random_device rd;
            uint32_t tmp_int = time(0);
            std::seed_seq seed_seq{rd(), tmp_int};
            dev_tmp1->mt_gen = std::mt19937(seed_seq);
            dev_tmp1->cu_cmd_id2 = dev_tmp1->rnd_dis(dev_tmp1->mt_gen);
        } else {
            dev_tmp1->cu_cmd_id2++;
        }
        auto itr_tmp1 = priv1->CU_cmds.emplace(tmp_int1, XmaCUCmdObjPrivate{});
        if (itr_tmp1.second) {//It is newly inserted item;
            priv1->num_cu_cmds++;
            found = true;
            cmd_obj.cmd_id1 = tmp_int1;
            cmd_obj.cmd_id2 = dev_tmp1->cu_cmd_id2;
            itr_tmp1.first->second.cmd_id2 = cmd_obj.cmd_id2;
            itr_tmp1.first->second.cu_id = cmd_obj.cu_index;
            itr_tmp1.first->second.execbo_id = bo_idx;

            priv1->kernel_execbos[bo_idx].cu_cmd_id1 = tmp_int1;
            priv1->kernel_execbos[bo_idx].cu_cmd_id2 = cmd_obj.cmd_id2;
        }
    }

    //xma_logmsg(XMA_DEBUG_LOG, XMAPLUGIN_MOD, "2. Num of cmds in-progress = %lu\n", priv1->CU_cmds.size());
    priv1->execbo_locked = false;
    if (return_code) *return_code = XMA_SUCCESS;
    return cmd_obj;
}

XmaCUCmdObj xma_plg_schedule_cu_cmd(XmaSession s_handle,
                                 void       *regmap,
                                 int32_t    regmap_size,
                                 int32_t    cu_index,
                                 int32_t*   return_code)
{
    XmaCUCmdObj cmd_obj_error;
    cmd_obj_error.cmd_id1 = 0;
    cmd_obj_error.cmd_id2 = 0;
    cmd_obj_error.cmd_finished = false;
    cmd_obj_error.cu_index = -1;
    cmd_obj_error.do_not_use1 = NULL;

    XmaHwSessionPrivate *priv1 = (XmaHwSessionPrivate*) s_handle.hw_session.private_do_not_use;
    if (priv1 == NULL) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "xma_plg_schedule_cu_cmd failed. XMASession is corrupted.\n");
        if (return_code) *return_code = XMA_ERROR;
        return cmd_obj_error;
    }
    if (s_handle.session_signature != (void*)(((uint64_t)priv1) | ((uint64_t)priv1->reserved))) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "xma_plg_schedule_cu_cmd failed. XMASession is corrupted.\n");
        if (return_code) *return_code = XMA_ERROR;
        return cmd_obj_error;
    }
    XmaHwDevice *dev_tmp1 = priv1->device;
    if (dev_tmp1 == NULL) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "Session XMA private pointer is NULL\n");
        if (return_code) *return_code = XMA_ERROR;
        return cmd_obj_error;
    }
    XmaHwKernel* kernel_tmp1 = priv1->kernel_info;
    if (s_handle.session_type < XMA_ADMIN) {
        xma_logmsg(XMA_INFO_LOG, XMAPLUGIN_MOD, "xma_plg_schedule_cu_cmd: cu_index ignored for this session type\n");
    } else {
        //Get the kernel_info
        if (cu_index < 0 || (uint32_t)cu_index > priv1->device->kernels.size()) {
            xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "xma_plg_schedule_cu_cmd failed. Invalud cu_index.\n");
            if (return_code) *return_code = XMA_ERROR;
            return cmd_obj_error;
        }
        kernel_tmp1 = &priv1->device->kernels[cu_index];
    
        if (!kernel_tmp1->in_use && !kernel_tmp1->soft_kernel) {
            if (xclOpenContext(dev_tmp1->handle, dev_tmp1->uuid, kernel_tmp1->cu_index_ert, true) != 0) {
                xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "Failed to open context to CU %s for this session\n", kernel_tmp1->name);
                if (return_code) *return_code = XMA_ERROR;
                return cmd_obj_error;
            }
        }
        xma_logmsg(XMA_DEBUG_LOG, XMAPLUGIN_MOD, "xma_plg_schedule_cu_cmd: Using admin session with CU %s\n", kernel_tmp1->name);
    }

    if (regmap == NULL) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "regmap is NULL\n");
        if (return_code) *return_code = XMA_ERROR;
        return cmd_obj_error;
    }
    if (regmap_size <= 0) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "regmap_size of %d is invalid\n", regmap_size);
        if (return_code) *return_code = XMA_ERROR;
        return cmd_obj_error;
    }
    //Kernel regmap 4KB in xmahw.h; execBO size is 4096 = 4KB in xmahw_hal.cpp; But ERT uses some space for ert pkt so allow max of 4032 Bytes for regmap
    if (regmap_size > MAX_KERNEL_REGMAP_SIZE) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "Max kernel regmap size is %d Bytes\n", MAX_KERNEL_REGMAP_SIZE);
        if (return_code) *return_code = XMA_ERROR;
        return cmd_obj_error;
    }
    if ((uint32_t)regmap_size != ((uint32_t)regmap_size & 0xFFFFFFFC)) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "regmap_size of %d is not a multiple of four bytes\n", regmap_size);
        if (return_code) *return_code = XMA_ERROR;
        return cmd_obj_error;
    }
    if (kernel_tmp1->regmap_size > 0) {
        if (regmap_size > kernel_tmp1->regmap_size) {
            xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "Can not exceed kernel register_map size. Kernel regamp_size: %d, trying to use size: %d\n", kernel_tmp1->regmap_size, regmap_size);
            /*Sarab TODO
            if (return_code) *return_code = XMA_ERROR;
            return cmd_obj_error;
            */
        }
    }

    uint8_t *src = (uint8_t*)regmap;
    int32_t bo_idx;
    
    bool expected = false;
    bool desired = true;
    while (!priv1->execbo_locked.compare_exchange_weak(expected, desired)) {
        //std::this_thread::sleep_for(std::chrono::milliseconds(1));
        expected = false;
    }
    //kernel completion lock acquired

    
    //xma_logmsg(XMA_DEBUG_LOG, XMAPLUGIN_MOD, "1. Num of cmds in-progress = %lu\n", priv1->CU_cmds.size());

    // Find an available execBO buffer
    bo_idx = xma_plg_execbo_avail_get(s_handle);
    if (bo_idx == -1) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "Unable to find free execbo to use\n");
        priv1->execbo_locked = false;
        if (return_code) *return_code = XMA_ERROR;
        return cmd_obj_error;
    }
    // Setup ert_start_kernel_cmd 
    ert_start_kernel_cmd *cu_cmd = 
        (ert_start_kernel_cmd*)priv1->kernel_execbos[bo_idx].data;
    cu_cmd->state = ERT_CMD_STATE_NEW;
    if (kernel_tmp1->soft_kernel) {
        cu_cmd->opcode = ERT_SK_START;
    } else {
        cu_cmd->opcode = ERT_START_CU;
    }
    cu_cmd->extra_cu_masks = 3;//XMA now supports 128 CUs

    cu_cmd->cu_mask = kernel_tmp1->cu_mask0;

    cu_cmd->data[0] = kernel_tmp1->cu_mask1;
    cu_cmd->data[1] = kernel_tmp1->cu_mask2;
    cu_cmd->data[2] = kernel_tmp1->cu_mask3;
    // Copy reg_map into execBO buffer 
    memcpy(&cu_cmd->data[3], src, regmap_size);
    xma_logmsg(XMA_DEBUG_LOG, XMAPLUGIN_MOD, "Dev# %d; Kernel: %s; Regmap size used is: %d\n", dev_tmp1->dev_index, kernel_tmp1->name, regmap_size);

    if (kernel_tmp1->arg_start > 0) {
        uint32_t tmp_int1 = 3 + (kernel_tmp1->arg_start / 4);
        for (uint32_t i = 3; i < tmp_int1; i++) {
            cu_cmd->data[i] = 0;
        }
    }
    if (kernel_tmp1->kernel_channels) {
        // XMA will write @ 0x10 and XRT read @ 0x14 to generate interupt and capture in execbo
        cu_cmd->data[7] = s_handle.channel_id;//0x10 == 4th integer;
        cu_cmd->data[8] = 0;//clear out the output
        xma_logmsg(XMA_DEBUG_LOG, XMAPLUGIN_MOD, "This is dataflow kernel. Using channel id: %d\n", s_handle.channel_id);
    }
    
    // Set count to size in 32-bit words + 4; Three extra_cu_mask are present
    cu_cmd->count = (regmap_size >> 2) + 4;
    
    if (xclExecBuf(priv1->dev_handle, 
                    priv1->kernel_execbos[bo_idx].handle) != 0)
    {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD,
                    "Failed to submit kernel start with xclExecBuf\n");
        priv1->execbo_locked = false;
        if (return_code) *return_code = XMA_ERROR;
        return cmd_obj_error;
    }

    XmaCUCmdObj cmd_obj;
    cmd_obj.cmd_id1 = 0;
    cmd_obj.cmd_id2 = 0;
    cmd_obj.cmd_finished = false;
    cmd_obj.cu_index = kernel_tmp1->cu_index;
    cmd_obj.do_not_use1 = s_handle.session_signature;

    bool found = false;
    while(!found) {
        dev_tmp1->cu_cmd_id1++;
        uint32_t tmp_int1 = dev_tmp1->cu_cmd_id1;
        if (tmp_int1 == 0) {
            tmp_int1 = 1;
            dev_tmp1->cu_cmd_id1 = tmp_int1;
            //Change seed of random_generator
            std::random_device rd;
            uint32_t tmp_int = time(0);
            std::seed_seq seed_seq{rd(), tmp_int};
            dev_tmp1->mt_gen = std::mt19937(seed_seq);
            dev_tmp1->cu_cmd_id2 = dev_tmp1->rnd_dis(dev_tmp1->mt_gen);
        } else {
            dev_tmp1->cu_cmd_id2++;
        }
        auto itr_tmp1 = priv1->CU_cmds.emplace(tmp_int1, XmaCUCmdObjPrivate{});
        if (itr_tmp1.second) {//It is newly inserted item;
            priv1->num_cu_cmds++;
            found = true;
            cmd_obj.cmd_id1 = tmp_int1;
            cmd_obj.cmd_id2 = dev_tmp1->cu_cmd_id2;
            itr_tmp1.first->second.cmd_id2 = cmd_obj.cmd_id2;
            itr_tmp1.first->second.cu_id = cmd_obj.cu_index;
            itr_tmp1.first->second.execbo_id = bo_idx;

            priv1->kernel_execbos[bo_idx].cu_cmd_id1 = tmp_int1;
            priv1->kernel_execbos[bo_idx].cu_cmd_id2 = cmd_obj.cmd_id2;
        }
    }

    //xma_logmsg(XMA_DEBUG_LOG, XMAPLUGIN_MOD, "2. Num of cmds in-progress = %lu\n", priv1->CU_cmds.size());
    priv1->execbo_locked = false;
    if (return_code) *return_code = XMA_SUCCESS;
    return cmd_obj;
}

int32_t xma_plg_cu_cmd_status(XmaSession s_handle, XmaCUCmdObj* cmd_obj_array, int32_t num_cu_objs, bool wait_for_cu_cmds)
{
    XmaHwSessionPrivate *priv1 = (XmaHwSessionPrivate*) s_handle.hw_session.private_do_not_use;
    if (priv1 == NULL) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "xma_plg_cu_cmd_status failed. XMASession is corrupted.\n");
        return XMA_ERROR;
    }
    if (s_handle.session_signature != (void*)(((uint64_t)priv1) | ((uint64_t)priv1->reserved))) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "xma_plg_cu_cmd_status failed. XMASession is corrupted.\n");
        return XMA_ERROR;
    }

    XmaHwKernel* kernel_tmp1 = priv1->kernel_info;
    XmaHwDevice *dev_tmp1 = priv1->device;
    if (dev_tmp1 == NULL) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "Session XMA private pointer is NULL-1\n");
        return XMA_ERROR;
    }
    if (s_handle.session_type != XMA_ADMIN && kernel_tmp1 == NULL) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "Session XMA private pointer is NULL-2\n");
        return XMA_ERROR;
    }
    if (priv1->using_work_item_done) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "xma_plg_cu_cmd_status & xma_plg_is_work_item_done both can not be used in same session\n");
        return XMA_ERROR;
    }
    priv1->using_cu_cmd_status = true;

    int32_t num_execbo = priv1->num_execbo_allocated;
    if (num_execbo <= 0) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "Session XMA private: No execbo allocated\n");
        return XMA_ERROR;
    }
    if (cmd_obj_array == NULL) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "cmd_obj_array is NULL\n");
        return XMA_ERROR;
    }
    if (num_cu_objs <= 0) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "num_cu_objs of %d is invalid\n", num_cu_objs);
        return XMA_ERROR;
    }

    bool expected = false;
    bool desired = true;
    bool all_done = true;
    std::vector<XmaCUCmdObj> cmd_vector(cmd_obj_array, cmd_obj_array+num_cu_objs);
    do {
        expected = false;
        if (priv1->execbo_locked.compare_exchange_weak(expected, desired)) {
            //kernel completion lock acquired

            if (xma_core::utils::check_all_execbo(s_handle) != XMA_SUCCESS) {
                xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "check_all-1: Unexpected error\n");
                //Release execbo lock
                priv1->execbo_locked = false;
                return XMA_ERROR;
            }
            all_done = true;
            for (auto& cmd: cmd_vector) {
                if (s_handle.session_type < XMA_ADMIN && cmd.cu_index != kernel_tmp1->cu_index) {
                    xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "cmd_obj_array is corrupted-1\n");
                    //Release completion lock
                    priv1->execbo_locked = false;
                    return XMA_ERROR;
                }
                if (cmd.cmd_id1 == 0 || cmd.cu_index == -1) {
                    xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "cmd_obj is invalid. Schedule_command may have  failed\n");
                    //Release completion lock
                    priv1->execbo_locked = false;
                    return XMA_ERROR;
                }
                auto itr_tmp1 = priv1->CU_cmds.find(cmd.cmd_id1);
                if (itr_tmp1 == priv1->CU_cmds.end()) {
                    cmd.cmd_finished = true;
                } else {
                    all_done = false;

                    if (itr_tmp1->second.cmd_id2 != cmd.cmd_id2) {
                        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "cmd_obj_array is corrupted-2\n");
                        //Release completion lock
                        priv1->execbo_locked = false;
                        return XMA_ERROR;
                    }
                    if (itr_tmp1->second.cu_id != cmd.cu_index) {
                        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "cmd_obj_array is corrupted-3\n");
                        //Release completion lock
                        priv1->execbo_locked = false;
                        return XMA_ERROR;
                    }
                }

                if (cmd.do_not_use1 != s_handle.session_signature) {
                    xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "cmd_obj_array is corrupted-5\n");
                    //Release completion lock
                    priv1->execbo_locked = false;
                    return XMA_ERROR;
                }
            }

            //Release completion lock
            priv1->execbo_locked = false;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
        }
        if (!wait_for_cu_cmds) {
            //Don't wait for all cu_cmds to finsh
            all_done = true;
        } else if (!all_done) {
            expected = false;
            if (priv1->execwait_locked.compare_exchange_weak(expected, desired)) {
                xclExecWait(priv1->dev_handle, 10);
                priv1->execwait_locked = false;
            }
        }
    } while(!all_done);

    for(int32_t i = 0; i < num_cu_objs; i++) {
        cmd_obj_array[i].cmd_finished = cmd_vector[i].cmd_finished;
    }

    return XMA_SUCCESS;
}

int32_t xma_plg_is_work_item_done(XmaSession s_handle, uint32_t timeout_ms)
{
    XmaHwSessionPrivate *priv1 = (XmaHwSessionPrivate*) s_handle.hw_session.private_do_not_use;
    if (priv1 == NULL) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "xma_plg_is_work_item_done failed. XMASession is corrupted.\n");
        return XMA_ERROR;
    }
    if (s_handle.session_signature != (void*)(((uint64_t)priv1) | ((uint64_t)priv1->reserved))) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "xma_plg_is_work_item_done failed. XMASession is corrupted.\n");
        return XMA_ERROR;
    }
    if (s_handle.session_type >= XMA_ADMIN) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "xma_plg_is_work_item_done can not be used for this XMASession type\n");
        return XMA_ERROR;
    }
    if (priv1->using_cu_cmd_status) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "xma_plg_is_work_item_done & xma_plg_cu_cmd_status both can not be used in same session\n");
        return XMA_ERROR;
    }
    priv1->using_work_item_done = true;

    XmaHwDevice *dev_tmp1 = priv1->device;
    if (dev_tmp1 == NULL) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "Session XMA private pointer is NULL\n");
        return XMA_ERROR;
    }
    int32_t num_execbo = priv1->num_execbo_allocated;
    if (num_execbo <= 0) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "Session XMA private: No execbo allocated\n");
        return XMA_ERROR;
    }

    int32_t count = 0;
    int32_t give_up = 0;
    bool expected = false;
    bool desired = true;
    uint32_t timeout1 = timeout_ms / 10;
    if (timeout1 < 10) {
        timeout1 = 10;
    }
    while (give_up < 20)
    {

        count = priv1->kernel_complete_count;

        if (count) {
            priv1->kernel_complete_count--;
            if (count > 255) {
                xma_logmsg(XMA_WARNING_LOG, XMAPLUGIN_MOD, "CU completion count is more than 256. Application maybe slow to process CU output\n");
            }
            return XMA_SUCCESS;
        }
        if (give_up > 0) {
            expected = false;
            if (priv1->execbo_locked.compare_exchange_weak(expected, desired)) {
                //kernel completion lock acquired

                if (xma_core::utils::check_all_execbo(s_handle) != XMA_SUCCESS) {
                    xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "check_all-2: Unexpected error\n");
                    //Release execbo lock
                    priv1->execbo_locked = false;
                    return XMA_ERROR;
                }
                //Release execbo lock
                priv1->execbo_locked = false;

                count = priv1->kernel_complete_count;

                if (count) {
                    priv1->kernel_complete_count--;
                    if (count > 255) {
                        xma_logmsg(XMA_WARNING_LOG, XMAPLUGIN_MOD, "CU completion count is more than 256. Application maybe slow to process CU output\n");
                    }
                    return XMA_SUCCESS;
                }
            }
        }

        // Wait for a notification
        if (give_up > 10) {
            expected = false;
            if (priv1->execwait_locked.compare_exchange_weak(expected, desired)) {
                xclExecWait(priv1->dev_handle, timeout1);
                priv1->execwait_locked = false;
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(3));
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
        }
        give_up++;
    }

    return XMA_ERROR;
}

int32_t xma_plg_channel_id(XmaSession s_handle) {
    XmaHwSessionPrivate *priv1 = (XmaHwSessionPrivate*) s_handle.hw_session.private_do_not_use;
    if (priv1 == NULL) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "xma_plg_channel_id failed. XMASession is corrupted.\n");
        return XMA_ERROR;
    }
    if (s_handle.session_signature != (void*)(((uint64_t)priv1) | ((uint64_t)priv1->reserved))) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "xma_plg_channel_id failed. XMASession is corrupted.\n");
        return XMA_ERROR;
    }
    if (s_handle.session_type >= XMA_ADMIN) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD, "xma_plg_channel_id can not be used for this XMASession type\n");
        return XMA_ERROR;
    }
    return s_handle.channel_id;
}

int32_t xma_plg_add_buffer_to_data_buffer(XmaDataBuffer *data, XmaBufferObj *dev_buf) {
    if (data == NULL) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD,
                "%s(): data XmaDataBuffer is NULL\n", __func__);
        return XMA_ERROR;
    }
    if (dev_buf == NULL) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD,
                "%s(): dev_buf XmaBufferObj is NULL\n", __func__);
        return XMA_ERROR;
    }
    if (xma_check_device_buffer(dev_buf) != XMA_SUCCESS) {
        return XMA_ERROR;
    }
    if (data->data.buffer_type != NO_BUFFER) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD,
                "%s(): Buffer already has assigned memory. Invalid XmaDataBuffer type\n", __func__);
        return XMA_ERROR;
    }
    data->data.buffer = dev_buf->data;
    data->data.xma_device_buf = dev_buf;
    if (dev_buf->device_only_buffer) {
        data->data.buffer_type = XMA_DEVICE_ONLY_BUFFER_TYPE;
    } else {
        data->data.buffer_type = XMA_DEVICE_BUFFER_TYPE;
    }
    data->alloc_size = dev_buf->size;
    data->data.is_clone = true;//so that others do not free the device buffer. Plugin owns device buffer

    return XMA_SUCCESS;
}

int32_t xma_plg_add_buffer_to_frame(XmaFrame *frame, XmaBufferObj **dev_buf_list, uint32_t num_dev_buf) {
    if (frame == NULL) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD,
                "%s(): frame XmaFrame is NULL\n", __func__);
        return XMA_ERROR;
    }
    if (dev_buf_list == NULL) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD,
                "%s(): dev_buf_list XmaBufferObj is NULL\n", __func__);
        return XMA_ERROR;
    }
    if (num_dev_buf > XMA_MAX_PLANES) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD,
                "%s(): num_dev_buf is more than max planes in frame\n", __func__);
        return XMA_ERROR;
    }
    if (num_dev_buf == 0) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD,
                "%s(): num_dev_buf is zero\n", __func__);
        return XMA_ERROR;
    }
    for (uint32_t i = 0; i < num_dev_buf; i++) {
        if (xma_check_device_buffer(dev_buf_list[i]) != XMA_SUCCESS) {
            return XMA_ERROR;
        }
    }
    if (frame->data[0].buffer_type != NO_BUFFER) {
        xma_logmsg(XMA_ERROR_LOG, XMAPLUGIN_MOD,
                "%s(): Frame already has assigned memory. Invalid frame buffer type\n", __func__);
        return XMA_ERROR;
    }
    for (uint32_t i = 0; i < num_dev_buf; i++) {
        if (frame->data[i].buffer_type != NO_BUFFER) {
            break;
        }
        frame->data[i].buffer = dev_buf_list[i]->data;
        frame->data[i].xma_device_buf = dev_buf_list[i];
        if (dev_buf_list[i]->device_only_buffer) {
            frame->data[i].buffer_type = XMA_DEVICE_ONLY_BUFFER_TYPE;
        } else {
            frame->data[i].buffer_type = XMA_DEVICE_BUFFER_TYPE;
        }
        //frame->data[i].alloc_size = dev_buf_list[i].size;
        frame->data[i].is_clone = true;//so that others do not free the device buffer. Plugin owns device buffer
    }

    return XMA_SUCCESS;
}

int32_t xma_plg_add_ref_cnt(XmaBufferObj *b_obj, int32_t num) {
    xma_logmsg(XMA_DEBUG_LOG, XMAPLUGIN_MOD,
               "%s(), line# %d\n", __func__, __LINE__);

    if (xma_check_device_buffer(b_obj) != XMA_SUCCESS) {
        return -999;
    }
    XmaBufferObjPrivate* b_obj_priv = (XmaBufferObjPrivate*) b_obj->private_do_not_touch;
    b_obj_priv->ref_cnt += num;
    return b_obj_priv->ref_cnt;
}


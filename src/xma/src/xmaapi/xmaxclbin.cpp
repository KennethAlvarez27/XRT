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
//#include <stdio.h>
#include <fstream>
#include <stdexcept>
//#include <string.h>
//#include <stdlib.h>
//#include <stdint.h>
#include "xclbin.h"
#include "app/xmaerror.h"
#include "app/xmalogger.h"
#include "lib/xmaxclbin.h"
#include "core/common/config_reader.h"
#include <regex>//Doesn't work on CentOS with older c++ lib
#include "app/xma_utils.hpp"
#include "lib/xma_utils.hpp"

#define XMAAPI_MOD "xmaxclbin"

/* Private function */
static int get_xclbin_iplayout(char *buffer, XmaXclbinInfo *xclbin_info);
static int get_xclbin_mem_topology(char *buffer, XmaXclbinInfo *xclbin_info);
static int get_xclbin_connectivity(char *buffer, XmaXclbinInfo *xclbin_info);

std::vector<char> xma_xclbin_file_open(const std::string& xclbin_name)
{
    xma_logmsg(XMA_INFO_LOG, XMAAPI_MOD, "Loading %s\n", xclbin_name.c_str());

    std::ifstream infile(xclbin_name, std::ios::binary | std::ios::ate);
    std::streamsize xclbin_size = infile.tellg();
    infile.seekg(0, std::ios::beg);

    std::vector<char> xclbin_buffer;
    try {
        xclbin_buffer.reserve(xclbin_size);
    } catch (const std::bad_alloc& ex) {
        xma_logmsg(XMA_ERROR_LOG, XMAAPI_MOD, "Could not allocate buffer for file %s\n", xclbin_name.c_str());
        xma_logmsg(XMA_ERROR_LOG, XMAAPI_MOD, "Buffer allocation error: %s\n", ex.what());
        throw;
    } catch (...) {
        xma_logmsg(XMA_ERROR_LOG, XMAAPI_MOD, "Could not allocate buffer for xclbin file %s\n", xclbin_name.c_str());
        throw;
    }
    infile.read(xclbin_buffer.data(), xclbin_size);
    if (infile.gcount() != xclbin_size) {
        xma_logmsg(XMA_ERROR_LOG, XMAAPI_MOD, "Unable to read full xclbin file %s\n", xclbin_name.c_str());
        throw std::runtime_error("Unable to read full xclbin file");
    }

    return xclbin_buffer;
}

static int32_t kernel_max_channel_id(const ip_data& ip, std::string kernel_channels)
{
  if (kernel_channels.empty())
    return -1;

  std::string knm = std::string(reinterpret_cast<const char*>(ip.m_name));
  knm = knm.substr(0,knm.find(":"));

  auto pos1 = kernel_channels.find("{"+knm+":");
  if (pos1 == std::string::npos)
    return -1;

  auto pos2 = kernel_channels.find("}",pos1);
  if (pos2 == std::string::npos || pos2 < pos1+knm.size()+2)
    return -2;

  auto ctxid_str = kernel_channels.substr(pos1+knm.size()+2,pos2);
  auto ctxid = std::stoi(ctxid_str);
  if (ctxid < 0 || ctxid > 31)
    return -3;
  
  return ctxid;
}

static int get_xclbin_iplayout(char *buffer, XmaXclbinInfo *xclbin_info)
{
    axlf *xclbin = reinterpret_cast<axlf *>(buffer);

    const axlf_section_header *ip_hdr = xclbin::get_axlf_section(xclbin, IP_LAYOUT);
    if (ip_hdr)
    {
        char *data = &buffer[ip_hdr->m_sectionOffset];
        const ip_layout *ipl = reinterpret_cast<ip_layout *>(data);
        //XmaIpLayout* layout = xclbin_info->ip_layout;
        xclbin_info->number_of_kernels = 0;
        xclbin_info->number_of_hardware_kernels = 0;
        std::string kernel_channels_info = xrt_core::config::get_kernel_channel_info();
        uint32_t j = 0;
        for (int i = 0; i < ipl->m_count; i++)
        {
            if (ipl->m_ip_data[i].m_type != IP_KERNEL)
                continue;

            if (j == MAX_XILINX_KERNELS) {
                xma_logmsg(XMA_ERROR_LOG, XMAAPI_MOD, "XMA supports max of only %d kernels per device\n", MAX_XILINX_KERNELS);
                return XMA_ERROR;
            }
            memset(xclbin_info->ip_layout[j].kernel_name, 0, MAX_KERNEL_NAME);
            std::string str_tmp1 = std::string((char*)ipl->m_ip_data[i].m_name);
            str_tmp1.copy((char*)xclbin_info->ip_layout[j].kernel_name, MAX_KERNEL_NAME-1);
            /*
            memcpy(xclbin_info->ip_layout[j].kernel_name,
                   ipl->m_ip_data[i].m_name, MAX_KERNEL_NAME);
            */
            //layout[j].base_addr = ipl->m_ip_data[i].m_base_address;

            xclbin_info->ip_layout[j].arg_start = -1;
            xclbin_info->ip_layout[j].regmap_size = -1;
            
            const axlf_section_header *xml_hdr = xclbin::get_axlf_section(xclbin, EMBEDDED_METADATA);
            if (xml_hdr) {
                char *xml_data = &buffer[xml_hdr->m_sectionOffset];
                uint64_t xml_size = xml_hdr->m_sectionSize;
                if (xml_size > 0 && xml_size < 500000) {
                    xma_core::utils::streambuf xml_streambuf(xml_data, xml_size);
                    std::istream xml_stream(&xml_streambuf);
                    std::string line;
                    bool found_kernel = false;
                    while(std::getline(xml_stream, line)) {
                        try {
                            if (!found_kernel) {
                                std::regex rgx1(R"(.*<kernel name=\"([^\s]+)\" .*>)");
                                std::smatch match1;
                                if (std::regex_match(line, match1, rgx1)) {
                                    std::string str_tmp2 = str_tmp1.substr(0,str_tmp1.find(":"));
                                    if (match1.size() == 2) {
                                        if (str_tmp2 == std::string(match1.str(1))) {
                                            found_kernel = true;
                                        }
                                    }
                                }
                            } else {
                                std::regex rgx1(R"(.*<arg name=\".*id=\"([0-9]+)\".*size=\"([xXa-fA-F0-9]+)\".*offset=\"([xXa-fA-F0-9]+)\".*>)");
                                std::smatch match1;
                                if (std::regex_match(line, match1, rgx1)) {
                                    if (match1.size() == 4) {
                                        if (std::stoi(match1.str(1)) == 0) {
                                            xclbin_info->ip_layout[j].arg_start = std::stoi(match1.str(3), 0, 16);
                                            xclbin_info->ip_layout[j].regmap_size = xclbin_info->ip_layout[j].arg_start;
                                            xclbin_info->ip_layout[j].regmap_size += std::stoi(match1.str(2), 0, 16);

                                            if (xclbin_info->ip_layout[j].arg_start < 0x10) {
                                                xma_logmsg(XMA_ERROR_LOG, XMAAPI_MOD, "kernel %s doesn't meet argument register map spec of HLS/RTL Wizard kernels\n", str_tmp1.c_str());
                                                return XMA_ERROR;
                                            }
                                        } else {
                                            int32_t tmp_int1 = std::stoi(match1.str(3), 0, 16) + std::stoi(match1.str(2), 0, 16);
                                            if (tmp_int1 > xclbin_info->ip_layout[j].regmap_size) {
                                                xclbin_info->ip_layout[j].regmap_size = tmp_int1;
                                            }
                                        }
                                    }
                                } else {
                                    std::regex rgx1(R"(.*(</kernel>).*)");
                                    std::smatch match1;
                                    if (std::regex_match(line, match1, rgx1)) {
                                        if (match1.size() == 2) {
                                            found_kernel = false;
                                            xma_logmsg(XMA_DEBUG_LOG, XMAAPI_MOD, "%s:- arg_start: 0x%x, regmap_size: 0x%x", str_tmp1.c_str(), xclbin_info->ip_layout[j].arg_start, xclbin_info->ip_layout[j].regmap_size);

                                            if (xclbin_info->ip_layout[j].regmap_size > MAX_KERNEL_REGMAP_SIZE) {
                                                xma_logmsg(XMA_ERROR_LOG, XMAAPI_MOD, "kernel %s register map size exceeds max limit. regmap_size: %d, max regmap_size: %d\n. Will use only max regmap_size", str_tmp1.c_str(), xclbin_info->ip_layout[j].regmap_size, MAX_KERNEL_REGMAP_SIZE);

                                                //DRM IPs have registers at high offset
                                                xclbin_info->ip_layout[j].regmap_size = MAX_KERNEL_REGMAP_SIZE;
                                                //return XMA_ERROR;
                                            }
                                            break;
                                        }
                                    }
                                }
                            }
                        } catch (std::regex_error& e) {
                            xma_logmsg(XMA_DEBUG_LOG, XMAAPI_MOD, "%s", line.c_str());
                            xma_logmsg(XMA_DEBUG_LOG, XMAAPI_MOD, "regex exception parsing above string: %s", e.what());
                            found_kernel = false;
                            xclbin_info->ip_layout[j].arg_start = -1;
                            xclbin_info->ip_layout[j].regmap_size = -1;
                        }
                    }
                }
            }

            xclbin_info->ip_layout[j].base_addr = ipl->m_ip_data[i].m_base_address;
            if (((ipl->m_ip_data[i].properties & IP_CONTROL_MASK) >> IP_CONTROL_SHIFT) == AP_CTRL_CHAIN) {
                int32_t max_channel_id = kernel_max_channel_id(ipl->m_ip_data[i], kernel_channels_info);
                if (max_channel_id >= 0) {
                    xma_logmsg(XMA_INFO_LOG, XMAAPI_MOD, "kernel \"%s\" is a dataflow kernel. channel_id will be handled by XMA. host app and plugins should not use reserved channle_id registers. Max channel_id is: %d\n", str_tmp1.c_str(), max_channel_id);
                    xclbin_info->ip_layout[j].kernel_channels = true;
                    xclbin_info->ip_layout[j].max_channel_id = (uint32_t)max_channel_id;
                } else {
                    if (max_channel_id == -1) {
                        xma_logmsg(XMA_WARNING_LOG, XMAAPI_MOD, "kernel \"%s\" is a dataflow kernel. Use kernel_channels xrt.ini setting to enable handling of channel_id by XMA. Treatng it as legacy dataflow kernel and channels to be managed by host app and plugins\n", str_tmp1.c_str());
                    } else if (max_channel_id == -2) {
                        xma_logmsg(XMA_ERROR_LOG, XMAAPI_MOD, "kernel \"%s\" is a dataflow kernel.  xrt.ini kernel_channels setting has incorrect format. setting found is: %s\n", str_tmp1.c_str(), kernel_channels_info.c_str());
                        return XMA_ERROR;
                    } else if (max_channel_id == -3) {
                        xma_logmsg(XMA_ERROR_LOG, XMAAPI_MOD, "kernel \"%s\" is a dataflow kernel.  xrt.ini kernel_channels setting only supports channel_ids from 0 to 31. setting found is: %s\n", str_tmp1.c_str(), kernel_channels_info.c_str());
                        return XMA_ERROR;
                    }
                    xclbin_info->ip_layout[j].kernel_channels = false;
                }
            } else {
                xma_logmsg(XMA_INFO_LOG, XMAAPI_MOD, "kernel \"%s\" is a legacy kernel. Channels to be managed by host app and plugins\n", str_tmp1.c_str());
                xclbin_info->ip_layout[j].kernel_channels = false;
            }
            xclbin_info->ip_layout[j].soft_kernel = false;

            /*            
            xma_logmsg(XMA_DEBUG_LOG, XMAAPI_MOD, "index = %d, kernel name = %s, base_addr = %lx\n",
                    j, layout[j].kernel_name, layout[j].base_addr);
            */
            xma_logmsg(XMA_DEBUG_LOG, XMAAPI_MOD, "index = %d, kernel name = %s, base_addr = %lx\n",
                    j, xclbin_info->ip_layout[j].kernel_name, xclbin_info->ip_layout[j].base_addr);
            j++;
        }
        xclbin_info->number_of_hardware_kernels = j;
        xma_logmsg(XMA_DEBUG_LOG, XMAAPI_MOD, "Num of hardware kernels on this device = %d\n", j);
        uint32_t num_soft_kernels = 0;
        //Handle soft kernels just like another hardware IP_Layout kernel
        //soft kernels to follow hardware kernels. so soft kenrel index will start after hardware kernels
        //const axlf_section_header *soft_kernel_hdr = nullptr;
        for (const axlf_section_header *soft_kernel_hdr = xclbin::get_axlf_section(xclbin, SOFT_KERNEL); soft_kernel_hdr != nullptr; soft_kernel_hdr = xclbin::get_axlf_section_next(xclbin, soft_kernel_hdr, SOFT_KERNEL)) {
            char *data = &buffer[soft_kernel_hdr->m_sectionOffset];
            const soft_kernel *sk_data = reinterpret_cast<soft_kernel *>(data);
            if (num_soft_kernels + sk_data->m_num_instances == MAX_XILINX_SOFT_KERNELS) {
                xma_logmsg(XMA_ERROR_LOG, XMAAPI_MOD, "XMA supports max of only %d soft kernels per device\n", MAX_XILINX_SOFT_KERNELS);
                return XMA_ERROR;
            }

            std::string str_tmp1 = std::string((char*)&buffer[soft_kernel_hdr->m_sectionOffset + sk_data->mpo_name]);
            std::string str_tmp2 = std::string((char*)&buffer[soft_kernel_hdr->m_sectionOffset + sk_data->mpo_version]);
            std::string str_tmp3 = std::string((char*)&buffer[soft_kernel_hdr->m_sectionOffset + sk_data->mpo_symbol_name]);
            xma_logmsg(XMA_DEBUG_LOG, XMAAPI_MOD, "soft kernel name = %s, version = %s, symbol name = %s, num of instances = %d\n", str_tmp1.c_str(), str_tmp2.c_str(), str_tmp3.c_str(), sk_data->m_num_instances);
            for (uint32_t i = 0; i < sk_data->m_num_instances; i++) {
                memset(xclbin_info->ip_layout[j].kernel_name, 0, MAX_KERNEL_NAME);
                str_tmp1 = std::string((char*)&buffer[soft_kernel_hdr->m_sectionOffset + sk_data->mpo_name]);
                str_tmp1 += "_";
                str_tmp1 += std::to_string(i);
                str_tmp1.copy((char*)xclbin_info->ip_layout[j].kernel_name, MAX_KERNEL_NAME-1);
                xclbin_info->ip_layout[j].soft_kernel = true;
                xclbin_info->ip_layout[j].base_addr = 0;
                xclbin_info->ip_layout[j].arg_start = -1;
                xclbin_info->ip_layout[j].regmap_size = -1;

                xma_logmsg(XMA_DEBUG_LOG, XMAAPI_MOD, "index = %d, soft kernel name = %s\n", j, xclbin_info->ip_layout[j].kernel_name);

                j++;
                num_soft_kernels++;
            }
        }
        xma_logmsg(XMA_DEBUG_LOG, XMAAPI_MOD, "Num of soft kernels on this device = %d\n", num_soft_kernels);

        xclbin_info->number_of_kernels = j;
        xma_logmsg(XMA_DEBUG_LOG, XMAAPI_MOD, "Num of total kernels on this device = %d\n", xclbin_info->number_of_kernels);

        xma_logmsg(XMA_DEBUG_LOG, XMAAPI_MOD, "  ");
        const axlf_section_header *xml_hdr = xclbin::get_axlf_section(xclbin, EMBEDDED_METADATA);
        if (xml_hdr) {
            char *xml_data = &buffer[xml_hdr->m_sectionOffset];
            uint64_t xml_size = xml_hdr->m_sectionSize;
            if (xml_size > 0 && xml_size < 500000) {
                xma_logmsg(XMA_DEBUG_LOG, XMAAPI_MOD, "XML MetaData is:");
                xma_core::utils::streambuf xml_streambuf(xml_data, xml_size);
                std::istream xml_stream(&xml_streambuf);
                std::string line;
                while(std::getline(xml_stream, line)) {
                    xma_logmsg(XMA_DEBUG_LOG, XMAAPI_MOD, "%s", line.c_str());
                }
            }
        } else {
            xma_logmsg(XMA_DEBUG_LOG, XMAAPI_MOD, "XML MetaData is missing");
        }
        xma_logmsg(XMA_DEBUG_LOG, XMAAPI_MOD, "  ");
        const axlf_section_header *kv_hdr = xclbin::get_axlf_section(xclbin, KEYVALUE_METADATA);
        if (kv_hdr) {
            char *kv_data = &buffer[kv_hdr->m_sectionOffset];
            uint64_t kv_size = kv_hdr->m_sectionSize;
            if (kv_size > 0 && kv_size < 200000) {
                xma_logmsg(XMA_DEBUG_LOG, XMAAPI_MOD, "Key-Value MetaData is:");
                xma_core::utils::streambuf kv_streambuf(kv_data, kv_size);
                std::istream kv_stream(&kv_streambuf);
                std::string line;
                while(std::getline(kv_stream, line)) {
                    xma_logmsg(XMA_DEBUG_LOG, XMAAPI_MOD, "%s", line.c_str());
                }
            }
        } else {
            xma_logmsg(XMA_DEBUG_LOG, XMAAPI_MOD, "Key-Value Data is not present in xclbin");
        }
        xma_logmsg(XMA_DEBUG_LOG, XMAAPI_MOD, "  ");
    }
    else
    {
        xma_logmsg(XMA_ERROR_LOG, XMAAPI_MOD, "Could not find IP_LAYOUT in xclbin ip_hdr=%p\n", ip_hdr);
        return XMA_ERROR;
    }

    uuid_copy(xclbin_info->uuid, xclbin->m_header.uuid); 

    return XMA_SUCCESS;
}

static int get_xclbin_mem_topology(char *buffer, XmaXclbinInfo *xclbin_info)
{
    //int rc = XMA_SUCCESS;
    axlf *xclbin = reinterpret_cast<axlf *>(buffer);

    const axlf_section_header *ip_hdr = xclbin::get_axlf_section(xclbin, MEM_TOPOLOGY);
    if (ip_hdr)
    {
        char *data = &buffer[ip_hdr->m_sectionOffset];
        const mem_topology *mem_topo = reinterpret_cast<mem_topology *>(data);
        XmaMemTopology *topology = xclbin_info->mem_topology;
        xclbin_info->number_of_mem_banks = mem_topo->m_count;
        xma_logmsg(XMA_DEBUG_LOG, XMAAPI_MOD, "MEM TOPOLOGY - %d banks\n",xclbin_info->number_of_mem_banks);
        if (xclbin_info->number_of_mem_banks > MAX_DDR_MAP) {
            xma_logmsg(XMA_ERROR_LOG, XMAAPI_MOD, "XMA supports max of only %d mem banks\n", MAX_DDR_MAP);
            return XMA_ERROR;
        }
        for (int i = 0; i < mem_topo->m_count; i++)
        {
            topology[i].m_type = mem_topo->m_mem_data[i].m_type;
            topology[i].m_used = mem_topo->m_mem_data[i].m_used;
            topology[i].m_size = mem_topo->m_mem_data[i].m_size;
            topology[i].m_base_address = mem_topo->m_mem_data[i].m_base_address;
            //m_tag is 16 chars
            memcpy(topology[i].m_tag, mem_topo->m_mem_data[i].m_tag, 16*sizeof(unsigned char));
            xma_logmsg(XMA_DEBUG_LOG, XMAAPI_MOD, "index=%d, tag=%s, type = %d, used = %d, size = %lx, base = %lx\n",
                   i,topology[i].m_tag, topology[i].m_type, topology[i].m_used,
                   topology[i].m_size, topology[i].m_base_address);
        }
    }
    else
    {
        printf("Could not find MEM_TOPOLOGY in xclbin ip_hdr=%p\n", ip_hdr);
        return XMA_ERROR;
    }

    return XMA_SUCCESS;
}

static int get_xclbin_connectivity(char *buffer, XmaXclbinInfo *xclbin_info)
{
    //int rc = XMA_SUCCESS;
    axlf *xclbin = reinterpret_cast<axlf *>(buffer);

    const axlf_section_header *ip_hdr = xclbin::get_axlf_section(xclbin, CONNECTIVITY);
    if (ip_hdr)
    {
        char *data = &buffer[ip_hdr->m_sectionOffset];
        const connectivity *axlf_conn = reinterpret_cast<connectivity *>(data);
        XmaAXLFConnectivity *xma_conn = xclbin_info->connectivity;
        xclbin_info->number_of_connections = axlf_conn->m_count;
        xma_logmsg(XMA_DEBUG_LOG, XMAAPI_MOD, "CONNECTIVITY - %d connections\n",xclbin_info->number_of_connections);
        for (int i = 0; i < axlf_conn->m_count; i++)
        {
            xma_conn[i].arg_index         = axlf_conn->m_connection[i].arg_index;
            xma_conn[i].m_ip_layout_index = axlf_conn->m_connection[i].m_ip_layout_index;
            xma_conn[i].mem_data_index    = axlf_conn->m_connection[i].mem_data_index;
            xma_logmsg(XMA_DEBUG_LOG, XMAAPI_MOD, "index = %d, arg_idx = %d, ip_idx = %d, mem_idx = %d\n",
                     i, xma_conn[i].arg_index, xma_conn[i].m_ip_layout_index,
                     xma_conn[i].mem_data_index);
        }
    }
    else
    {
        xma_logmsg(XMA_ERROR_LOG, XMAAPI_MOD, "Could not find CONNECTIVITY in xclbin ip_hdr=%p\n", ip_hdr);
        return XMA_ERROR;
    }

    return XMA_SUCCESS;
}

int xma_xclbin_info_get(char *buffer, XmaXclbinInfo *info)
{
    int rc = 0;
    rc = get_xclbin_mem_topology(buffer, info);
    if(rc == XMA_ERROR)
        return rc;
    rc = get_xclbin_connectivity(buffer, info);
    if(rc == XMA_ERROR)
        return rc;
    rc = get_xclbin_iplayout(buffer, info);
    if(rc == XMA_ERROR)
        return rc;

    memset(info->ip_ddr_mapping, 0, sizeof(info->ip_ddr_mapping));
    uint64_t tmp_ddr_map = 0;
    for(uint32_t c = 0; c < info->number_of_connections; c++)
    {
        XmaAXLFConnectivity *xma_conn = &info->connectivity[c];
        tmp_ddr_map = 1;
        tmp_ddr_map = tmp_ddr_map << (xma_conn->mem_data_index);
        info->ip_ddr_mapping[xma_conn->m_ip_layout_index] |= tmp_ddr_map;
        //info->ip_ddr_mapping[xma_conn->m_ip_layout_index] |= 1 << (xma_conn->mem_data_index);
    }
    xma_logmsg(XMA_DEBUG_LOG, XMAAPI_MOD, "CU DDR connections bitmap:");
    for(uint32_t i = 0; i < info->number_of_hardware_kernels; i++)
    {
        xma_logmsg(XMA_DEBUG_LOG, XMAAPI_MOD, "\t%s - 0x%016llx\n",info->ip_layout[i].kernel_name, (unsigned long long)info->ip_ddr_mapping[i]);
    }
    //For execbo:
    //info->num_ips = info->number_of_kernels;
    return XMA_SUCCESS;
}

int xma_xclbin_map2ddr(uint64_t bit_map, int32_t* ddr_bank)
{
    //64 bits based on MAX_DDR_MAP = 64
    int ddr_bank_idx = 0;
    while (bit_map != 0)
    {
        if (bit_map & 1)
        {
            *ddr_bank = ddr_bank_idx;
            return XMA_SUCCESS;
        }
        ddr_bank_idx++;
        bit_map = bit_map >> 1;
    }
    *ddr_bank = -1;
    return XMA_ERROR;
}

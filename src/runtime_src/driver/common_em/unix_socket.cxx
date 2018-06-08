/**
 * Copyright (C) 2016-2017 Xilinx, Inc
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

// Copyright 2014 Xilinx, Inc. All rights reserved.
//
// This file contains confidential and proprietary information
// of Xilinx, Inc. and is protected under U.S. and
// international copyright and other intellectual property
// laws.
//
// DISCLAIMER
// This disclaimer is not a license and does not grant any
// rights to the materials distributed herewith. Except as
// otherwise provided in a valid license issued to you by
// Xilinx, and to the maximum extent permitted by applicable
// law: (1) THESE MATERIALS ARE MADE AVAILABLE "AS IS" AND
// WITH ALL FAULTS, AND XILINX HEREBY DISCLAIMS ALL WARRANTIES
// AND CONDITIONS, EXPRESS, IMPLIED, OR STATUTORY, INCLUDING
// BUT NOT LIMITED TO WARRANTIES OF MERCHANTABILITY, NON-
// INFRINGEMENT, OR FITNESS FOR ANY PARTICULAR PURPOSE; and
// (2) Xilinx shall not be liable (whether in contract or tort,
// including negligence, or under any other theory of
// liability) for any loss or damage of any kind or nature
// related to, arising under or in connection with these
// materials, including for any direct, or any indirect,
// special, incidental, or consequential loss or damage
// (including loss of data, profits, goodwill, or any type of
// loss or damage suffered as a result of any action brought
// by a third party) even if such damage or loss was
// reasonably foreseeable or Xilinx had been advised of the
// possibility of the same.
//
// CRITICAL APPLICATIONS
// Xilinx products are not designed or intended to be fail-
// safe, or for use in any application requiring fail-safe
// performance, such as life-support or safety devices or
// systems, Class III medical devices, nuclear facilities,
// applications related to the deployment of airbags, or any
// other applications that could lead to death, personal
// injury, or severe property or environmental damage
// (individually and collectively, "Critical
// Applications"). Customer assumes the sole risk and
// liability of any use of Xilinx products in Critical
// Applications, subject only to applicable laws and
// regulations governing limitations on product liability.
//
// THIS COPYRIGHT NOTICE AND DISCLAIMER MUST BE RETAINED AS
// PART OF THIS FILE AT ALL TIMES.

#ifndef _WINDOWS
// TODO: Windows build support
// This seems to be a linux only file

#include <iostream>
#include "unix_socket.h"
#include <sys/stat.h>
#include <boost/algorithm/string.hpp>
#define STR_MAX_LEN 106

namespace systemUtil {

  inline void printErrorMessage(std::string command, int status)
  {
    if(!status)// no error
      return;
    std::cout<<"ERROR: [SDx 60-600] "<< command <<" failed with the error code "<< status<< ". PLEASE CHECK YOUR PERMISSIONS "<<std::endl;
  }

  
  void makeSystemCall (std::string &operand1, systemOperation operation, std::string operand2 )
  {
    switch (operation) {

      case CREATE :
        {
          std::stringstream mkdirCommand;
          mkdirCommand << "mkdir -p "<< operand1;
          struct stat statBuf;
          if ( stat(operand1.c_str(), &statBuf) == -1 )
          {
            int status = system(mkdirCommand.str().c_str());
            printErrorMessage(mkdirCommand.str(),status);
          }
          break;
        }
      case REMOVE :
        {
          std::stringstream removeDirCommand ;
          removeDirCommand << "rm -rf " << operand1;
          struct stat statBuf;
          if ( stat(operand1.c_str(), &statBuf) == 0 )
          {
            int status = system(removeDirCommand.str().c_str());
            printErrorMessage(removeDirCommand.str(),status);
          }
          break;
        }
      case COPY   :
        {
          std::stringstream copyCommand;
          copyCommand <<"cp "<<operand1<<" "<<operand2;
          struct stat statBuf;
          if ( stat(operand1.c_str(), &statBuf) == 0 )
          {
            int status = system(copyCommand.str().c_str());
            printErrorMessage(copyCommand.str(),status);
          }
          break;
        }
      case APPEND   :
        {
          std::stringstream appendCommand;
          appendCommand <<"cat "<<operand1<<">> "<<operand2;
          struct stat statBuf;
          if ( stat(operand1.c_str(), &statBuf) == 0 )
          {
            int status = system(appendCommand.str().c_str());
            printErrorMessage(appendCommand.str(),status);
          }
          break;
        }
      case UNZIP  :
        {
          std::stringstream unzipCommand;
          unzipCommand <<"unzip -q " << operand1 << " -d " << operand2;
          int status = system(unzipCommand.str().c_str());
          printErrorMessage(unzipCommand.str(),status);
          break;
        }
      case PERMISSIONS : 
        {
          std::stringstream permissionsCommand ;
          permissionsCommand << "chmod -R " << operand2 << " " << operand1;
          int status = system(permissionsCommand.str().c_str());
          printErrorMessage(permissionsCommand.str(),status);
          break;
        }
    }
  }
}

namespace xclemulation{

  DDRBank::DDRBank()
  {
    ddrSize = 0;
  }

  config* config::mInst= NULL;

  //get the instance of singleton class
  config* config::getInstance()
  {
    if( !mInst )
    {
      mInst = new config();
    }
    return mInst;
  }

  //destroy the singleton class
  void config::destroy()
  {
    delete mInst;
    mInst = NULL;
  }

  static bool getBoolValue(std::string& value,bool defaultValue)
  {
    if(value.empty())
      return defaultValue;
    if (boost::iequals(value,"true" ))
    {
      return true;
    }
    if (boost::iequals(value,"false" ))
    {
      return false;
    }
    return defaultValue;
  }

  void config::populateEnvironmentSetup(std::map<std::string,std::string>& mEnvironmentNameValueMap)
  {
    for (auto i : mEnvironmentNameValueMap)
    {
      std::string name  = i.first;
      std::string value = i.second;
      if(value.empty() || name.empty())
        continue;
      
      if(name == "diagnostics")
      {
        enableDiagnostics(getBoolValue(value,false));
      }
      else if(name == "enable_umr")
      {
        enableUMRChecks(getBoolValue(value,false));
      }
      else if(name == "enable_oob")
      {
        enableOOBChecks(getBoolValue(value,false));
      }
      else if (name == "enable_mem_logs")
      {
        enableMemLogs(getBoolValue(value,false));
      }
      else if(name == "suppress_infos")
      {
        suppressInfo(getBoolValue(value,false));
      }
      else if(name == "suppress_errors")
      {
        suppressErrors(getBoolValue(value,false));
      }
      else if(name == "suppress_warnings")
      {
        suppressWarnings(getBoolValue(value,false));
      }
      else if(name == "print_infos_in_console")
      {
        printInfosInConsole(getBoolValue(value,true));
      }
      else if(name == "print_warnings_in_console")
      {
        printWarningsInConsole(getBoolValue(value,true));
      }
      else if(name == "print_errors_in_console")
      {
        printErrorsInConsole(getBoolValue(value,true));
      }
      else if(name == "dont_run")
      {
        setDontRun(getBoolValue(value,false));
      }
      else if(name == "keep_run_dir")
      {
        setKeepRunDir(getBoolValue(value,false));
      }
      else if(name == "sim_dir")
      {
        setSimDir(value);
      }
      else if(name == "verbosity")
      {
        unsigned int verbosity = strtoll(value.c_str(),NULL,0);
        if(verbosity > 0 )
          setVerbosityLevel(verbosity);
      }
      else if(name == "packet_size")
      {
        unsigned int packetSize = strtoll(value.c_str(),NULL,0);
        if(packetSize > 0 )
          setPacketSize(packetSize);
      }
      else if(name == "max_trace_count")
      {
        unsigned int maxTraceCount = strtoll(value.c_str(),NULL,0);
        if(maxTraceCount > 0 )
          setMaxTraceCount(maxTraceCount);
      }
      else if (name == "padding_factor")
      {
        unsigned int paddingFactor = atoi(value.c_str());
        if(paddingFactor > 0)
          setPaddingFactor(paddingFactor);
      }
      else if(name == "launch_waveform")
      {
        if (boost::iequals(value,"gui" ))
        {
          setLaunchWaveform(LAUNCHWAVEFORM::GUI);
        }
        else if (boost::iequals(value,"batch" ))
        {
          setLaunchWaveform(LAUNCHWAVEFORM::BATCH);
        }
        else
        {
          setLaunchWaveform(LAUNCHWAVEFORM::OFF);
        }
      }
      else if(name == "Debug.sdx_server_port")
      {
        unsigned int serverPort = strtoll(value.c_str(),NULL,0);
        if(serverPort> 0 )
          setServerPort(serverPort);
      }
      else if(name == "enable_arbitration")
      {
        //Nothing to do
      }
      else if(name.find("Debug.") == std::string::npos)
      {
        std::cout<<"WARNING: [SDx-EM 08] Invalid option '"<<name<<"` specified in sdaccel.ini"<<std::endl;
      }
    }
    //this code has to be removed once gui generates ini file by adding launch_waveform property
    const char* simMode = getenv("HW_EM_LAUNCH_WAVEFORM");
    if(simMode)
    {
      std::string simulationMode = simMode;
      if (boost::iequals(simulationMode,"gui" ))
      {
        setLaunchWaveform(LAUNCHWAVEFORM::GUI);
      }
      else if (boost::iequals(simulationMode,"batch" ))
      {
        setLaunchWaveform(LAUNCHWAVEFORM::BATCH);
      }
    }
    
  }


  static std::string getSelfPath() 
  {
    char buff[PATH_MAX];
    ssize_t len = ::readlink("/proc/self/exe", buff, sizeof(buff)-1);
    if (len != -1) 
    {
      buff[len] = '\0';
      return std::string(buff);
    }
    return "";
  }
  
  static const char* valueOrEmpty(const char* cstr)
  {
    return cstr ? cstr : "";
  }

  static std::string getExecutablePath()
  {
    std::string hostBinaryPath = getSelfPath();
    if(hostBinaryPath.empty())
    {
      std::cout<<"unable to findout the host binary path in emulation driver "<<std::endl;
    }
    std::string directory;
    const size_t last_slash_idx = hostBinaryPath.rfind("/");
    if (std::string::npos != last_slash_idx)
    {
      directory = hostBinaryPath.substr(0, last_slash_idx);
    }
    return directory;
  }
  
  static std::string getEmConfigFilePath()
  {
    std::string executablePath = getExecutablePath();
    std::string emConfigPath = valueOrEmpty(std::getenv("EMCONFIG_PATH"));
    if (!emConfigPath.empty()) {
      executablePath = emConfigPath;
    }
    std::string xclEmConfigfile = executablePath.empty()? "emconfig.json" :executablePath+ "/emconfig.json";
    return xclEmConfigfile;
  }
  
  bool isXclEmulationModeHwEmuOrSwEmu()
  {
    static auto xem = std::getenv("XCL_EMULATION_MODE");
    if(xem)
    {
      if((std::strcmp(xem,"hw_emu") == 0) || (std::strcmp(xem,"sw_emu") == 0))
      {
        return true;
      }
    }
    return false;
  }

  std::string getEmDebugLogFile()
  {
    std::string executablePath = getExecutablePath();
    std::string xclEmConfigfile = executablePath.empty()? "emulation_debug.log" :executablePath+ "/emulation_debug.log";
    return xclEmConfigfile;
  }
 
  std::string getRunDirectory()
  {
    std::string executablePath = getExecutablePath();
    std::string sRunDir = executablePath.empty()? ".run" :executablePath+ "/.run";
    return sRunDir;
  }


  static std::string getIniFile()
  {
    std::string executablePath = getExecutablePath();
    std::string sdaccelIniPath = valueOrEmpty(std::getenv("SDACCEL_INI_PATH"));
    if (!sdaccelIniPath.empty()) {
      executablePath = sdaccelIniPath;
    }
    std::string iniFile = executablePath.empty()? "sdaccel.ini" :executablePath+ "/sdaccel.ini";
    struct stat buffer;
    return (stat(iniFile.c_str(), &buffer) == 0) ? iniFile : "";
  }

  std::map<std::string,std::string> getEnvironmentByReadingIni()
  {
    std::map<std::string,std::string> environmentNameValueMap;
    std::string iniFile = getIniFile();
    if(iniFile.empty())
      return environmentNameValueMap;

    boost::property_tree::ptree m_tree;
    boost::property_tree::ini_parser::read_ini(iniFile.c_str(), m_tree);
    for(auto& section : m_tree)
    {
      std::string sectionName = section.first;
      if(sectionName == "Emulation") 
      {
        for (auto& key:section.second) 
        {
          environmentNameValueMap[key.first] = key.second.get_value<std::string>();
        }
      }
      else if(sectionName == "Debug") 
      {
        for (auto& key:section.second) 
        {
          environmentNameValueMap["Debug."+key.first] = key.second.get_value<std::string>();
        }
      }
    }
    return environmentNameValueMap;
  }

  //initialize memMap
  static void initializeMemMap(std::map<std::string, uint64_t>& memMap)
  {
    memMap["1K"]    = xclemulation::MEMSIZE_1K;
    memMap["4K"]    = xclemulation::MEMSIZE_4K;
    memMap["8K"]    = xclemulation::MEMSIZE_8K;
    memMap["16K"]   = xclemulation::MEMSIZE_16K;
    memMap["32K"]   = xclemulation::MEMSIZE_32K;
    memMap["64K"]   = xclemulation::MEMSIZE_64K;
    memMap["128K"]  = xclemulation::MEMSIZE_128K;
    memMap["256K"]  = xclemulation::MEMSIZE_256K;
    memMap["512K"]  = xclemulation::MEMSIZE_512K;

    memMap["1M"]    = xclemulation::MEMSIZE_1M;
    memMap["2M"]    = xclemulation::MEMSIZE_2M;
    memMap["4M"]    = xclemulation::MEMSIZE_4M;
    memMap["8M"]    = xclemulation::MEMSIZE_8M;
    memMap["16M"]   = xclemulation::MEMSIZE_16M;
    memMap["32M"]   = xclemulation::MEMSIZE_32M;
    memMap["64M"]   = xclemulation::MEMSIZE_64M;
    memMap["128M"]  = xclemulation::MEMSIZE_128M;
    memMap["256M"]  = xclemulation::MEMSIZE_256M;
    memMap["512M"]  = xclemulation::MEMSIZE_512M;

    memMap["1G"]    = xclemulation::MEMSIZE_1G;
    memMap["2G"]    = xclemulation::MEMSIZE_2G;
    memMap["4G"]    = xclemulation::MEMSIZE_4G;
    memMap["8G"]    = xclemulation::MEMSIZE_8G;
    memMap["16G"]   = xclemulation::MEMSIZE_16G;
    memMap["32G"]   = xclemulation::MEMSIZE_32G;
    memMap["64G"]   = xclemulation::MEMSIZE_64G;
    memMap["128G"]  = xclemulation::MEMSIZE_128G;
    memMap["256G"]  = xclemulation::MEMSIZE_256G;
    memMap["512G"]  = xclemulation::MEMSIZE_512G;

    memMap["1T"]    = xclemulation::MEMSIZE_1T;
    memMap["2T"]    = xclemulation::MEMSIZE_2T;
    memMap["4T"]    = xclemulation::MEMSIZE_4T;
    memMap["8T"]    = xclemulation::MEMSIZE_8T;
    memMap["16T"]   = xclemulation::MEMSIZE_16T;
    memMap["32T"]   = xclemulation::MEMSIZE_32T;
    memMap["64T"]   = xclemulation::MEMSIZE_64T;
    memMap["128T"]  = xclemulation::MEMSIZE_128T;
    memMap["256T"]  = xclemulation::MEMSIZE_256T;
    memMap["512T"]  = xclemulation::MEMSIZE_512T;

    memMap["1KB"]    = xclemulation::MEMSIZE_1K;
    memMap["4KB"]    = xclemulation::MEMSIZE_4K;
    memMap["8KB"]    = xclemulation::MEMSIZE_8K;
    memMap["16KB"]   = xclemulation::MEMSIZE_16K;
    memMap["32KB"]   = xclemulation::MEMSIZE_32K;
    memMap["64KB"]   = xclemulation::MEMSIZE_64K;
    memMap["128KB"]  = xclemulation::MEMSIZE_128K;
    memMap["256KB"]  = xclemulation::MEMSIZE_256K;
    memMap["512KB"]  = xclemulation::MEMSIZE_512K;

    memMap["1MB"]    = xclemulation::MEMSIZE_1M;
    memMap["2MB"]    = xclemulation::MEMSIZE_2M;
    memMap["4MB"]    = xclemulation::MEMSIZE_4M;
    memMap["8MB"]    = xclemulation::MEMSIZE_8M;
    memMap["16MB"]   = xclemulation::MEMSIZE_16M;
    memMap["32MB"]   = xclemulation::MEMSIZE_32M;
    memMap["64MB"]   = xclemulation::MEMSIZE_64M;
    memMap["128MB"]  = xclemulation::MEMSIZE_128M;
    memMap["256MB"]  = xclemulation::MEMSIZE_256M;
    memMap["512MB"]  = xclemulation::MEMSIZE_512M;

    memMap["1GB"]    = xclemulation::MEMSIZE_1G;
    memMap["2GB"]    = xclemulation::MEMSIZE_2G;
    memMap["4GB"]    = xclemulation::MEMSIZE_4G;
    memMap["8GB"]    = xclemulation::MEMSIZE_8G;
    memMap["16GB"]   = xclemulation::MEMSIZE_16G;
    memMap["32GB"]   = xclemulation::MEMSIZE_32G;
    memMap["64GB"]   = xclemulation::MEMSIZE_64G;
    memMap["128GB"]  = xclemulation::MEMSIZE_128G;
    memMap["256GB"]  = xclemulation::MEMSIZE_256G;
    memMap["512GB"]  = xclemulation::MEMSIZE_512G;

    memMap["1TB"]    = xclemulation::MEMSIZE_1T;
    memMap["2TB"]    = xclemulation::MEMSIZE_2T;
    memMap["4TB"]    = xclemulation::MEMSIZE_4T;
    memMap["8TB"]    = xclemulation::MEMSIZE_8T;
    memMap["16TB"]   = xclemulation::MEMSIZE_16T;
    memMap["32TB"]   = xclemulation::MEMSIZE_32T;
    memMap["64TB"]   = xclemulation::MEMSIZE_64T;
    memMap["128TB"]  = xclemulation::MEMSIZE_128T;
    memMap["256TB"]  = xclemulation::MEMSIZE_256T;

  }

  static void populateDDRBankInfo(boost::property_tree::ptree const& ddrBankTree, xclDeviceInfo2& info, std::list<DDRBank>& DDRBankList, std::map<std::string, uint64_t>& memMap)
  {
    info.mDDRSize = 0;
    info.mDDRBankCount = 0;
    DDRBankList.clear();
    using boost::property_tree::ptree;
    for (auto& prop : ddrBankTree)
    {
      for (auto& prop1 : prop.second)//we have only one property as of now which is Size of each DDRBank
      {
        std::string name = prop1.first;
        std::string value = prop1.second.get_value<std::string>();
        if(name == "Size")
        {
          uint64_t size =  0;
          std::map<std::string,uint64_t>::iterator it = memMap.find(value);
          if(it != memMap.end())
          {
            size = (*it).second;
          }
          info.mDDRSize = info.mDDRSize + size; 
          DDRBank bank;
          bank.ddrSize = size; 
          DDRBankList.push_back(bank);
        }
      }

      info.mDDRBankCount = info.mDDRBankCount + 1;
    }
  }

  static void populateHwDevicesOfSingleBoard(boost::property_tree::ptree & deviceTree, std::vector<std::tuple<xclDeviceInfo2,std::list<DDRBank> ,bool, bool> >& devicesInfo,std::map<std::string, uint64_t>& memMap, bool bUnified, bool bXPR)
  {

    for (auto& device : deviceTree)
    {
      xclDeviceInfo2 info;

      //fill info with default values 
      info.mMagic = 0X586C0C6C;
      //info.mHALMajorVersion = XCLHAL_MAJOR_VER;
      //info.mHALMinorVersion= XCLHAL_MINOR_VER;
      info.mVendorId = 0x10ee;
      info.mSubsystemVendorId = 0x0000;
      info.mDeviceVersion = 0x0000;
      info.mDDRSize = MEMSIZE_4G;
      info.mDataAlignment = DDR_BUFFER_ALIGNMENT;
      info.mDDRBankCount = 1;
      for(unsigned int i = 0; i < 4 ;i++)
        info.mOCLFrequency[i] = 300;
      unsigned numDevices = 1;
      std::list<DDRBank> DDRBankList;
      DDRBank bank;
      bank.ddrSize = MEMSIZE_4G;
      DDRBankList.push_back(bank);

      //iterate over all the properties of device and fill the info structure. This info object gets used to create  device object
      for (auto& prop : device.second)
      {
        if(prop.first == "Name")
        {
          std::string mName = prop.second.get_value<std::string>();
          if(mName.empty() == false)
          {
            if(strlen(mName.c_str()) < 256)//info.mName is static array of size 256
              std::strcpy(info.mName, mName.c_str());
          }
        }
        else if(prop.first == "HalMajorVersion")
        {
          unsigned short halMajorVersion = prop.second.get_value<unsigned short>();
          info.mHALMajorVersion = halMajorVersion;
        }
        else if(prop.first == "HalMinorVersion")
        {
          unsigned short halMinorVersion = prop.second.get_value<unsigned short>();
          info.mHALMinorVersion = halMinorVersion;
        }
        else if(prop.first == "VendorId")
        {
          unsigned short vendorId = prop.second.get_value<unsigned short>();
          info.mVendorId = vendorId;
        }
        else if(prop.first == "SubsystemVendorId")
        {
          unsigned short subsystemVendorId = prop.second.get_value<unsigned short>();
          info.mSubsystemVendorId = subsystemVendorId;
        }
        else if(prop.first == "DeviceVersion")
        {
          unsigned deviceVersion = prop.second.get_value<unsigned>();
          info.mDeviceVersion = deviceVersion;
        }
        else if(prop.first == "DataAlignment")
        {
          size_t dataAlignment = prop.second.get_value<unsigned>();
          info.mDataAlignment = dataAlignment;
        }
        else if(prop.first == "DdrBanks")
        {
          boost::property_tree::ptree ddrBankTree = prop.second;
          populateDDRBankInfo(ddrBankTree, info, DDRBankList,memMap);
        }
        else if(prop.first == "OclFreqency")
        {
          unsigned oclFrequency = prop.second.get_value<unsigned>();
          info.mOCLFrequency[0] = oclFrequency;
        }
        else if(prop.first == "NumDevices")
        {
          numDevices = prop.second.get_value<unsigned>();
        }

      }
      //get the number of times this device is instantiated using numDevices variable.
      //iterate using this variable and create that many number of devices.
      for(unsigned int i = 0; i < numDevices;i++)
      {
        devicesInfo.push_back(make_tuple(info,DDRBankList,bUnified, bXPR));
      }
    }
    return;
  }

  //create all the devices If devices child is present in this tree otherwise call this function recursively for all the child trees
  //iterate over devices tree and create all the device objects.
  static void populateHwEmDevices(boost::property_tree::ptree const& platformTree,std::vector<std::tuple<xclDeviceInfo2,std::list<DDRBank> ,bool, bool> >& devicesInfo,std::map<std::string, uint64_t>& memMap)
  {
    using boost::property_tree::ptree;
    ptree::const_iterator platformEnd = platformTree.end();
    bool bUnified = false;
    bool bXPR = false;
    for (ptree::const_iterator it = platformTree.begin(); it != platformEnd; ++it) 
    {
      if(it->first == "UnifiedPlatform")
      {
        std::string unified = it->second.get_value<std::string>();
        bUnified = getBoolValue(unified,bUnified);
      }
      else if(it->first == "ExpandedPR")
      { 
        std::string expandedPR = it->second.get_value<std::string>();
        bXPR = getBoolValue(expandedPR,bXPR);
      }
    }

    if(platformTree.count("Boards") != 0)// Boards child is present
    {
      for (auto& board : platformTree.get_child("Boards"))
      {
        unsigned int numBoards = 1;
        boost::property_tree::ptree deviceTree;
        //iterate over all the properties of device and fill the info structure. This info object gets used to create  device object
        for (auto& prop : board.second)
        {
          if(prop.first == "NumBoards")
          {
            numBoards = prop.second.get_value<unsigned>();
          }
          else if(prop.first == "Devices")
          {
            deviceTree = prop.second;
          }
        }
        for(unsigned int i = 0; i < numBoards; i++)
          populateHwDevicesOfSingleBoard(deviceTree,devicesInfo,memMap, bUnified, bXPR);
      }
    }
  }

  static bool validateVersions(boost::property_tree::ptree const& versionTree)
  {
    using boost::property_tree::ptree;
    ptree::const_iterator end = versionTree.end();
    for (ptree::const_iterator it = versionTree.begin(); it != end; ++it) 
    {
      if(it->first == "FileVersion")
      {
        std::string fileVersion = it->second.get_value<std::string>();
        if(fileVersion != "2.0")
        {
          std::cout<<"incompatible version of emconfig.json found.Please regenerate this file"<<std::endl;
          return false;
        }
      }
      else if(it->first == "ToolVersion")
      {
        //std::string toolVersion= it->second.get_value<std::string>();
      }
    }
    return true;
  }

  void getDevicesInfo(std::vector<std::tuple<xclDeviceInfo2,std::list<DDRBank> ,bool, bool > >& devicesInfo)
  {
    std::string emConfigFile =  getEmConfigFilePath();
    std::ifstream ifs;
    ifs.open(emConfigFile, std::ifstream::in);
    if(!ifs)
    {
      return;
    }

    if(ifs.is_open())
    {
      //  std::cout<<emConfigFile<<" is used for the platform configuration "<<std::endl;
    }

    std::map<std::string, uint64_t> memMap;
    initializeMemMap(memMap);
    boost::property_tree::ptree configTree;
    boost::property_tree::read_json(ifs, configTree);//read the config file and stores in configTree
    ifs.close(); 

    using boost::property_tree::ptree;
    ptree::const_iterator end = configTree.end();
    boost::property_tree::ptree versionTree;
    boost::property_tree::ptree platformTree;

    //iterate over configTree and  check whether file version is  1.0 or not. If not return 1.
    //get both platform and environment tree
    for (ptree::const_iterator it = configTree.begin(); it != end; ++it) 
    {
      if(it->first == "Version")
      {
        versionTree = it->second;//get the version tree
      }
      else if(it->first == "Platform")
      {
        platformTree = it->second; //get the platform tree
      }
    }

    bool success = validateVersions(versionTree);
    if(!success)
      return;//validation of Versions failed.
    populateHwEmDevices(platformTree,devicesInfo,memMap);
  }

  bool copyLogsFromOneFileToAnother(const std::string &logFile, std::ofstream &ofs) {
    std::ifstream ifs(logFile.c_str());
    if (!ifs.is_open() || !ofs.is_open())
      return true;

    ofs << ifs.rdbuf() << std::endl;
    ifs.close();
    return false;
  }
}

unix_socket::unix_socket()
{
  server_started = false;
  fd = -1;
  std::string sock_id = "";
  if(getenv("USER") != NULL) {
    std::string user = getenv("USER");
    if(getenv("EMULATION_SOCKETID")) {
      sock_id = getenv("EMULATION_SOCKETID");
    } else {
      sock_id = "xcl_sock";
    }
    std::string pathname =  "/tmp/" + user;
    name = pathname + "/" + sock_id;
    systemUtil::makeSystemCall(pathname, systemUtil::systemOperation::CREATE);
  } else {
    name = "/tmp/xcl_socket";
  }
  start_server(name);
}

void unix_socket::start_server(const std::string sk_desc)
{
  int sock= -1;
  struct sockaddr_un server;

  //unlink(sk_desc);
  sock = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sock < 0) {
    perror("opening stream socket");
    exit(1);
  }
  server.sun_family = AF_UNIX;
  //Coverity
  strncpy(server.sun_path, sk_desc.c_str(),STR_MAX_LEN);
  if (connect(sock, (struct sockaddr*)&server, sizeof(server)) >= 0){
    fd = sock;
    //Removing debug printf statement from user view
    //printf("Socket connected with name %s\n", server.sun_path);
    server_started = true;
    return;
  }
  unlink(server.sun_path);
  if (bind(sock, (struct sockaddr *) &server, sizeof(struct sockaddr_un))) {
    close(sock);
    perror("binding stream socket");
    exit(1);
  }
  int status = listen(sock, 5);
  (void) status; // For Coverity

  fd = accept(sock, 0, 0);
  close(sock);
  if (fd == -1){
    perror("socket acceptance failed");
    exit(1);
  } else {
    server_started = true;
    //Removing debug printf statement from user view
    //printf("Socket started with name %s\n", server.sun_path);
  } 
  return;
}

size_t unix_socket::sk_write(const void *wbuf, size_t count)
{
  ssize_t r;
  ssize_t wlen = 0;
  const unsigned char *buf = (const unsigned char*)(wbuf);
  do {
    if ((r = write(fd, buf + wlen, count - wlen)) < 0) {
      if (errno == EINTR || errno == EAGAIN) {
        continue;
      } 
      return -1;
    }
    wlen += r;
  } while (wlen < static_cast<unsigned int>(count));
  return wlen;
}
size_t unix_socket::sk_read(void *rbuf, size_t count)
{
  ssize_t r;
  ssize_t rlen = 0;
  unsigned char *buf = (unsigned char*)(rbuf);

  do {
    if ((r = read(fd, buf + rlen, count - rlen)) < 0) {
      if (errno == EINTR || errno == EAGAIN)
        continue;
      return -1;
    }
    rlen += r;
  } while ((rlen < static_cast<unsigned int>(count)) );

  return rlen;
}

#endif



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

#ifndef _WIN32

#include "lop.h"
#include "core/common/module_loader.h"
#include "core/common/utils.h"
#include "core/common/dlfcn.h"

namespace xdplop {

  // The loading of the function should only happen once.  Since it 
  //  could theoretically be called from two user threads at once, we
  //  use an internal struct constructor that is thread safe to ensure
  //  it only happens once
  void load_xdp_lop()
  {
    // Thread safe per C++-11
    static xrt_core::module_loader xdp_lop_loader("xdp_lop_plugin",
						  register_lop_functions,
						  lop_warning_function) ;
  }

  // All of the function pointers that will be dynamically linked from
  //  the XDP Plugin side
  std::function<void (const char*, long long int, unsigned int)> function_start_cb;
  std::function<void (const char*, long long int, unsigned int)> function_end_cb;
  std::function<void (unsigned int, bool)> read_cb ;
  std::function<void (unsigned int, bool)> write_cb ;
  std::function<void (unsigned int, bool)> enqueue_cb ;

  void register_lop_functions(void* handle)
  {
    typedef void (*ftype)(const char*, long long int, unsigned int) ;
    function_start_cb = (ftype)(xrt_core::dlsym(handle, "lop_function_start")) ;
    if (xrt_core::dlerror() != NULL) function_start_cb = nullptr ;    

    function_end_cb = (ftype)(xrt_core::dlsym(handle, "lop_function_end"));
    if (xrt_core::dlerror() != NULL) function_end_cb = nullptr ;

    typedef void (*btype)(unsigned int, bool) ;

    read_cb = (btype)(xrt_core::dlsym(handle, "lop_read")) ;
    if (xrt_core::dlerror() != NULL) read_cb = nullptr ;
    
    write_cb = (btype)(xrt_core::dlsym(handle, "lop_write")) ;
    if (xrt_core::dlerror() != NULL) write_cb = nullptr ;

    enqueue_cb = (btype)(xrt_core::dlsym(handle, "lop_kernel_enqueue")) ;
    if (xrt_core::dlerror() != NULL) enqueue_cb = nullptr ;
  }

  void lop_warning_function()
  {
    if (xrt_xocl::config::get_profile() || xrt_xocl::config::get_opencl_summary())
    {
      xrt_xocl::message::send(xrt_xocl::message::severity_level::warning,
			 "Both low overhead profiling and OpenCL profile summary generation are enabled.  The trace generated by low overhead profiling will reflect the higher overhead associated with profile summary generation.  For best performance of low overhead profiling, please disable standard OpenCL profiling.\nAlso, this combination will result in multiple run_summary files being generated, one for OpenCL profiling and one for low overhead profiling.") ;
    }
  }

  LOPFunctionCallLogger::LOPFunctionCallLogger(const char* function) :
    LOPFunctionCallLogger(function, 0)
  {    
  }

  LOPFunctionCallLogger::LOPFunctionCallLogger(const char* function, 
					       long long int address) :
    m_name(function), m_address(address)
  {
    // Load the LOP plugin if not already loaded
    static bool s_load_lop = false ;
    if (!s_load_lop)
    {
      s_load_lop = true ;
      if (xrt_core::config::get_lop_trace()) 
	load_xdp_lop() ;
    }

    // Log the stats for this function
    m_funcid = xrt_core::utils::issue_id() ;
    if (function_start_cb)
      function_start_cb(m_name, m_address, m_funcid) ;
  }

  LOPFunctionCallLogger::~LOPFunctionCallLogger()
  {
    if (function_end_cb)
      function_end_cb(m_name, m_address, m_funcid) ;
  }

} // end namespace xdplop

namespace xocl {
  namespace lop {

    // Create lambda functions that will be attached and triggered
    //  by events when their status changes
    std::function<void (xocl::event*, cl_int)> 
    action_read()
    {
      return [](xocl::event* e, cl_int status) 
	{
	  if (!xdplop::read_cb) return ;

	  // Only keep track of the start and stop
	  if (status == CL_RUNNING)
	    xdplop::read_cb(e->get_uid(), true) ;
	  else if (status == CL_COMPLETE) 
	    xdplop::read_cb(e->get_uid(), false) ;
	} ;
    }

    std::function<void (xocl::event*, cl_int)> 
    action_write()
    {
      return [](xocl::event* e, cl_int status)
	{
	  if (!xdplop::write_cb) return ;

	  // Only keep track of the start and stop
	  if (status == CL_RUNNING)
	    xdplop::write_cb(e->get_uid(), true) ;
	  else if (status == CL_COMPLETE) 
	    xdplop::write_cb(e->get_uid(), false) ;
	} ;
    }

    std::function<void (xocl::event*, cl_int)> 
    action_migrate(cl_mem_migration_flags flags) 
    {
      if (flags & CL_MIGRATE_MEM_OBJECT_HOST)
      {
	return [](xocl::event* e, cl_int status)
	  {
	    if (!xdplop::read_cb) return ;

	    if (status == CL_RUNNING)
	      xdplop::read_cb(e->get_uid(), true) ;
	    else if (status == CL_COMPLETE)
	      xdplop::read_cb(e->get_uid(), false) ;
	  } ;
      }
      else
      {
	return [](xocl::event* e, cl_int status)
	  {
	    if (!xdplop::write_cb) return ;

	    if (status == CL_RUNNING)
	      xdplop::write_cb(e->get_uid(), true) ;
	    else if (status == CL_COMPLETE)
	      xdplop::write_cb(e->get_uid(), false) ;
	  } ;
      }
    }

    std::function<void (xocl::event*, cl_int)> 
    action_ndrange()
    {
      return [](xocl::event* e, cl_int status)
	{
	  if (!xdplop::enqueue_cb) return ;

	  if (status == CL_RUNNING)
	    xdplop::enqueue_cb(e->get_uid(), true) ;
	  else if (status == CL_COMPLETE)
	    xdplop::enqueue_cb(e->get_uid(), false) ;
	} ;
    }

    std::function<void (xocl::event*, cl_int)>
    action_ndrange_migrate(cl_kernel kernel)
    {
      // Only check to see if any of the memory objects are going
      //  to move.
      bool writeWillHappen = false ;
      for (auto& arg : xocl::xocl(kernel)->get_xargument_range())
      {
	auto mem = arg->get_memory_object() ;
	if (mem != nullptr && !(mem->is_resident()))
	{
	  writeWillHappen = true ;
	  break ;
	}
      }
      
      if (writeWillHappen)
      {
	return [](xocl::event* e, cl_int status)
	{
	  if (!xdplop::write_cb) return ;
	  
	  if (status == CL_RUNNING)
	    xdplop::write_cb(e->get_uid(), true) ;
	  else if (status == CL_COMPLETE)
	    xdplop::write_cb(e->get_uid(), false) ;
	} ;	
      }
      else
      {
	return [](xocl::event* e, cl_int status)
	  {
	    return ;
	  } ;
      }
    }

  } // end namespace lop
} // end namespace xocl

#else 
// LOP is initially only supported on Linux

#endif

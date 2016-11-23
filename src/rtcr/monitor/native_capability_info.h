/*
 * \brief  Monitoring PD::alloc_rpc_cap and PD::free_rpc_cap
 * \author Denis Huber
 * \date   2016-10-06
 */

#ifndef _RTCR_NATIVE_CAPABILITY_INFO_H_
#define _RTCR_NATIVE_CAPABILITY_INFO_H_

/* Genode includes */
#include <util/list.h>
#include <base/native_capability.h>

namespace Rtcr {
	struct Native_capability_info;
}

/**
 * List element to store a capability which is created by the pd session
 * They are usually created by client's entrypoint and therefore require
 * a Cpu_thread_capability
 */
struct Rtcr::Native_capability_info : Normal_obj_info, Genode::List<Native_capability_info>::Element
{
	Genode::Native_capability native_cap;
	Genode::Native_capability ep_cap;

	Native_capability_info(Genode::Native_capability native_cap,
			Genode::Native_capability ep_cap, bool bootstrapped)
	:
		Normal_obj_info (bootstrapped),
		native_cap      (native_cap),
		ep_cap          (ep_cap)
	{ }

	Native_capability_info *find_by_native_cap(Genode::Native_capability cap)
	{
		if(cap == native_cap)
			return this;
		Native_capability_info *info = next();
		return info ? info->find_by_native_cap(cap) : 0;
	}

	void print(Genode::Output &output) const
	{
		using Genode::Hex;

		Genode::print(output, "native ", native_cap, ", ep ", ep_cap, ", ");
		Normal_obj_info::print(output);
	}
};


#endif /* _RTCR_NATIVE_CAPABILITY_INFO_H_ */

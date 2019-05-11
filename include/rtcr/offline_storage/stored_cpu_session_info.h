/*
 * \brief  Structure for storing CPU session information
 * \author Denis Huber
 * \date   2016-10-26
 */

#ifndef _RTCR_STORED_CPU_SESSION_INFO_H_
#define _RTCR_STORED_CPU_SESSION_INFO_H_

/* Genode includes */
#include <util/list.h>

/* Rtcr includes */
#include "../intercept/cpu_session.h"
#include "../offline_storage/stored_cpu_thread_info.h"
#include "../offline_storage/stored_info_structs.h"

namespace Rtcr {
	struct Stored_cpu_session_info;
}


struct Rtcr::Stored_cpu_session_info : Stored_session_info, Genode::List<Stored_cpu_session_info>::Element
{
	Genode::uint16_t sigh_badge;
	Genode::List<Stored_cpu_thread_info> stored_cpu_thread_infos;

	Stored_cpu_session_info(Cpu_session_component &cpu_session)
	:
		Stored_session_info(cpu_session.parent_state().creation_args.string(),
				cpu_session.parent_state().upgrade_args.string(),
				cpu_session.cap().local_name(),
				cpu_session.parent_state().bootstrapped),
		sigh_badge(cpu_session.parent_state().sigh.local_name()),
		stored_cpu_thread_infos()
	{ }

	Stored_cpu_session_info(const char* creation_args,
					const char* upgrade_args,
                                        Genode::uint16_t local_name,
                                        bool bootstrapped,
					Genode::uint16_t sigh_badge)
	:
		Stored_session_info(creation_args,upgrade_args,local_name,bootstrapped),
		sigh_badge(sigh_badge),
		stored_cpu_thread_infos()
	{ }

	Stored_cpu_session_info *find_by_badge(Genode::uint16_t badge)
	{
		if(badge == this->badge)
			return this;
		Stored_cpu_session_info *info = next();
		return info ? info->find_by_badge(badge) : 0;
	}

	void print(Genode::Output &output) const
	{
		using Genode::Hex;

		Stored_session_info::print(output);
		Genode::print(output, ", sigh_badge=", sigh_badge);
	}

};

#endif /* _RTCR_STORED_CPU_SESSION_INFO_H_ */

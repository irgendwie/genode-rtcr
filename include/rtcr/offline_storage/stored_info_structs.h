/*
 * \brief  Structs for storing monitored objects
 * \author Denis Huber
 * \date   2016-11-23
 */

#ifndef _RTCR_STORED_INFO_STRUCTS_H_
#define _RTCR_STORED_INFO_STRUCTS_H_

/* Genode includes */
#include <util/list.h>
#include <util/string.h>

namespace Rtcr {
	struct Stored_general_info;
	struct Stored_session_info;
	struct Stored_normal_info;
}

struct Rtcr::Stored_general_info
{
	/**
	 * Genode's system-global capability identifier
	 */
	Genode::uint16_t const badge;
	/**
	 * Indicates whether the RPC object to store was created during component's bootstrap phase
	 */
	bool             const bootstrapped;

	Stored_general_info(Genode::uint16_t badge, bool bootstrapped)
	:
		badge(badge), bootstrapped(bootstrapped)
	{ }

	void print(Genode::Output &output) const
	{
		using Genode::Hex;

		Genode::print(output, "<", badge, ">", " bootstrapped=", bootstrapped);
	}
};


/**
 * Struct to monitor session RPC objects
 */
struct Rtcr::Stored_session_info : Stored_general_info
{
	Genode::String<160> const creation_args;
	Genode::String<160> upgrade_args;

	Stored_session_info(const char* creation_args, const char* upgrade_args, Genode::uint16_t badge, bool bootstrapped)
	:
		Stored_general_info (badge, bootstrapped),
		creation_args (creation_args),
		upgrade_args  (upgrade_args)
	{ }

	void print(Genode::Output &output) const
	{
		using Genode::Hex;

		Stored_general_info::print(output);
		Genode::print(output, ", cargs='", creation_args, "', uargs='", upgrade_args, "'");
	}
};


/**
 * Struct to monitor normal RPC object (this is, no session or root RPC objects)
 */
struct Rtcr::Stored_normal_info : Stored_general_info
{
	// Insert common members

	Stored_normal_info(Genode::uint16_t badge, bool bootstrapped)
	:
		Stored_general_info(badge, bootstrapped)
	{ }

	void print(Genode::Output &output) const
	{
		using Genode::Hex;

		Stored_general_info::print(output);
	}
};

#endif /* _RTCR_STORED_INFO_STRUCTS_H_ */

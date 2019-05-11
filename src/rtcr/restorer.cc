/*
 * \brief  Restorer of Target_child
 * \author Denis Huber
 * \date   2016-10-26
 */

#include "rtcr/restorer.h"
#include "rtcr/util/sort.h"
#include "rtcr/util/debug.h"

using namespace Rtcr;

template<typename T>
void Restorer::_destroy_list(Genode::List<T> &list)
{
	while(T *elem = list.first())
	{
		list.remove(elem);
		Genode::destroy(_alloc, elem);
	}
}
template void Restorer::_destroy_list(Genode::List<Badge_translation_info> &list);
template void Restorer::_destroy_list(Genode::List<Dataspace_translation_info> &list);
template void Restorer::_destroy_list(Genode::List<Ref_badge_info> &list);

void Restorer::_destroy_list(Genode::List<Simplified_managed_dataspace_info> &list)
{
	while(Simplified_managed_dataspace_info *smd_info = list.first())
	{
		list.remove(smd_info);

		while(Simplified_managed_dataspace_info::Simplified_designated_ds_info *sdd_info =
				smd_info->designated_dataspaces.first())
		{
			smd_info->designated_dataspaces.remove(sdd_info);
			Genode::destroy(_alloc, sdd_info);
		}

		Genode::destroy(_alloc, smd_info);
	}
}


Genode::List<Ref_badge_info> Restorer::_create_region_map_dataspaces_list(
		Genode::List<Stored_pd_session_info> &stored_pd_sessions, Genode::List<Stored_rm_session_info> &stored_rm_sessions)
{
	if(verbose_debug) Genode::log("Resto::\033[33m", __func__, "\033[0m(...)");

	Genode::List<Ref_badge_info> result;

	// Get region maps from PD sessions
	Stored_pd_session_info *stored_pd_session = stored_pd_sessions.first();
	while(stored_pd_session)
	{
		result.insert(new (_alloc) Ref_badge_info(stored_pd_session->stored_address_space.ds_badge));
		result.insert(new (_alloc) Ref_badge_info(stored_pd_session->stored_stack_area.ds_badge));
		result.insert(new (_alloc) Ref_badge_info(stored_pd_session->stored_linker_area.ds_badge));

		stored_pd_session = stored_pd_session->next();
	}

	// Get region maps from RM sessions
	Stored_rm_session_info *stored_rm_session = stored_rm_sessions.first();
	while(stored_rm_session)
	{
		Stored_region_map_info *stored_region_map = stored_rm_session->stored_region_map_infos.first();
		while(stored_region_map)
		{
			result.insert(new (_alloc) Ref_badge_info(stored_region_map->ds_badge));

			stored_region_map = stored_region_map->next();
		}

		stored_rm_session = stored_rm_session->next();
	}


	return result;
}


void Restorer::_identify_recreate_pd_sessions(Pd_root &pd_root, Genode::List<Stored_pd_session_info> &stored_pd_sessions)
{
	if(verbose_debug) Genode::log("Resto::\033[33m", __func__, "\033[0m(...)");

	// There shall be only one PD session by now (created during bootstrap)
	Pd_session_component *bootstrapped_pd_session = pd_root.session_infos().first();

	// Error: No PD session found
	if(!bootstrapped_pd_session)
	{
		Genode::error("There is no bootstrapped PD session");
		throw Genode::Exception();
	}
	// Warning: More than one PD session found
	if(bootstrapped_pd_session->next()) Genode::warning("There are more than 1 bootstrapped PD sessions");

	Stored_pd_session_info *stored_pd_session = stored_pd_sessions.first();
	while(stored_pd_session)
	{
		Pd_session_component *pd_session = nullptr;
		if(stored_pd_session->bootstrapped)
		{
			// Identify bootstrapped PD session
			pd_session = bootstrapped_pd_session;
		}
		else
		{
			// Recreate PD session
			Genode::Rpc_in_buffer<160> creation_args(stored_pd_session->creation_args.string());
			Genode::Session_capability pd_session_cap = pd_root.session(creation_args, Genode::Affinity());
			pd_session = _child.custom_services().pd_root->session_infos().first();
			if(pd_session) pd_session = pd_session->find_by_badge(pd_session_cap.local_name());
			if(!pd_session)
			{
				Genode::error("Could not find newly created PD session for ", pd_session_cap);
				throw Genode::Exception();
			}
		}

		// Remember the association of the stored RPC object to the new RPC object
		_rpcobject_translations.insert(new (_alloc) Badge_translation_info(stored_pd_session->badge, pd_session->cap()));

		// Remember the associations of the stored region maps and their dataspaces of the PD session to
		// their corresponding new ones
		// Address space
		_rpcobject_translations.insert(new (_alloc) Badge_translation_info(
				stored_pd_session->stored_address_space.badge, pd_session->address_space_component().cap()));
		_rpcobject_translations.insert(new (_alloc) Badge_translation_info(
				stored_pd_session->stored_address_space.ds_badge, pd_session->address_space_component().parent_state().ds_cap));
		// Stack area
		_rpcobject_translations.insert(new (_alloc) Badge_translation_info(
				stored_pd_session->stored_stack_area.badge, pd_session->stack_area_component().cap()));
		_rpcobject_translations.insert(new (_alloc) Badge_translation_info(
				stored_pd_session->stored_stack_area.ds_badge, pd_session->stack_area_component().parent_state().ds_cap));
		// Linker area
		_rpcobject_translations.insert(new (_alloc) Badge_translation_info(
				stored_pd_session->stored_linker_area.badge, pd_session->linker_area_component().cap()));
		_rpcobject_translations.insert(new (_alloc) Badge_translation_info(
				stored_pd_session->stored_linker_area.ds_badge, pd_session->linker_area_component().parent_state().ds_cap));

		_recreate_signal_sources(*pd_session, stored_pd_session->stored_source_infos);
		_recreate_signal_contexts(*pd_session, stored_pd_session->stored_context_infos);
		// XXX postpone native caps creation, because it needs capabilities from CPU thread

		stored_pd_session = stored_pd_session->next();
	}

}


void Restorer::_recreate_signal_sources(Pd_session_component &pd_session,
		Genode::List<Stored_signal_source_info> &stored_signal_sources)
{
	if(verbose_debug) Genode::log("Resto::\033[33m", __func__, "\033[0m(...)");

	// Warning: There are already signal sources in the PD session
	if(pd_session.parent_state().signal_sources.first())
		Genode::warning("There are already signal sources in the PD session ", pd_session.cap());

	Signal_source_info *signal_source = nullptr;
	Stored_signal_source_info *stored_signal_source = stored_signal_sources.first();
	while(stored_signal_source)
	{
		// Create signal source
		Genode::Capability<Genode::Signal_source> cap = pd_session.alloc_signal_source();
		signal_source = pd_session.parent_state().signal_sources.first();
		if(signal_source) signal_source = signal_source->find_by_badge(cap.local_name());
		if(!signal_source)
		{
			Genode::error("Could not find newly created signal source for ", cap);
			throw Genode::Exception();
		}
		// Remember the association of the stored RPC object to the new RPC object
		_rpcobject_translations.insert(new (_alloc) Badge_translation_info(stored_signal_source->badge, signal_source->cap));

		stored_signal_source = stored_signal_source->next();
	}
}


void Restorer::_recreate_signal_contexts(Pd_session_component &pd_session,
		Genode::List<Stored_signal_context_info> &stored_signal_contexts)
{
	if(verbose_debug) Genode::log("Resto::\033[33m", __func__, "\033[0m(...)");

	// Warning: There are already signal sources in the PD session
	if(pd_session.parent_state().signal_sources.first())
	{
		Genode::warning("There are already signal contexts in the PD session ", pd_session.cap());
		Signal_source_info *info = pd_session.parent_state().signal_sources.first();
		while(info)
		{
			Genode::log(" ", *info);

			info = info->next();
		}
	}

	Signal_context_info *signal_context = nullptr;
	Stored_signal_context_info *stored_signal_context = stored_signal_contexts.first();
	while(stored_signal_context)
	{
		// Create signal context
		Genode::Capability<Genode::Signal_source> ss_cap;
		if(stored_signal_context->signal_source_badge != 0)
		{
			Badge_translation_info *info = _rpcobject_translations.first();
			if(info) info = info->find_by_ckpt_badge(stored_signal_context->signal_source_badge);
			ss_cap = Genode::reinterpret_cap_cast<Genode::Signal_source>(info->resto_cap);
		}

		Genode::Capability<Genode::Signal_context> cap = pd_session.alloc_context(ss_cap, stored_signal_context->imprint);
		signal_context = pd_session.parent_state().signal_contexts.first();
		if(signal_context) signal_context = signal_context->find_by_badge(cap.local_name());
		if(!signal_context)
		{
			Genode::error("Could not find newly created signal context for ", cap);
			throw Genode::Exception();
		}

		// Remember the association of the stored RPC object to the new RPC object
		_rpcobject_translations.insert(new (_alloc) Badge_translation_info(stored_signal_context->badge, signal_context->cap));

		stored_signal_context = stored_signal_context->next();
	}
}


/*void Restorer::_identify_recreate_ram_sessions(Ram_root &ram_root, Genode::List<Stored_ram_session_info> &stored_ram_sessions)
{
	if(verbose_debug) Genode::log("Resto::\033[33m", __func__, "\033[0m(...)");

	// There shall be only one RAM session by now (created during bootstrap)
	Ram_session_component *bootstrapped_ram_session = ram_root.session_infos().first();

	// Error: No RAM session found
	if(!bootstrapped_ram_session)
	{
		Genode::error("There is no bootstrapped RAM session");
		throw Genode::Exception();
	}
	// Warning: More than one RAM session found
	if(bootstrapped_ram_session->next()) Genode::warning("There are more than 1 bootstrapped RAM sessions");

	Stored_ram_session_info *stored_ram_session = stored_ram_sessions.first();
	while(stored_ram_session)
	{
		Ram_session_component *ram_session = nullptr;
		if(stored_ram_session->bootstrapped)
		{
			// Identify bootstrapped RAM session
			ram_session = bootstrapped_ram_session;
		}
		else
		{
			// Create RAM session
			Genode::Rpc_in_buffer<160> creation_args(stored_ram_session->creation_args.string());
			Genode::Session_capability ram_session_cap = ram_root.session(creation_args, Genode::Affinity());
			ram_session = _child.custom_services().ram_root->session_infos().first();
			if(ram_session) ram_session = ram_session->find_by_badge(ram_session_cap.local_name());
			if(!ram_session)
			{
				Genode::error("Could not find newly created RAM session for ", ram_session_cap);
				throw Genode::Exception();
			}
		}

		// Remember the association of the stored RPC object to the new RPC object
		_rpcobject_translations.insert(new (_alloc) Badge_translation_info(stored_ram_session->badge, ram_session->cap()));

		_recreate_ram_dataspaces(*ram_session, stored_ram_session->stored_ramds_infos);

		stored_ram_session = stored_ram_session->next();
	}
}*/


void Restorer::_recreate_ram_dataspaces(Ram_session_component &ram_session,
		Genode::List<Stored_ram_dataspace_info> &stored_ram_dataspaces)
{
	if(verbose_debug) Genode::log("Resto::\033[33m", __func__, "\033[0m(...)");

	// Warning: There are already RAM dataspaces in the RAM session
	if(ram_session.parent_state().ram_dataspaces.first())
		Genode::warning("There are already RAM dataspaces in the RAM session ", ram_session.cap());

	Ram_dataspace_info *ramds = nullptr;
	Stored_ram_dataspace_info *stored_ramds = stored_ram_dataspaces.first();
	while(stored_ramds)
	{
		// Recreate
		Genode::Ram_dataspace_capability cap = ram_session.alloc(stored_ramds->size, stored_ramds->cached);
		ramds = ram_session.parent_state().ram_dataspaces.first();
		if(ramds) ramds = ramds->find_by_badge(cap.local_name());
		if(!ramds)
		{
			Genode::error("Could not find newly created RAM dataspace for ", cap);
			throw Genode::Exception();
		}

		// Remember the association of the stored RPC object to the new RPC object
		_rpcobject_translations.insert(new (_alloc) Badge_translation_info(stored_ramds->badge, ramds->cap));

		stored_ramds = stored_ramds->next();
	}
}

void Restorer::_identify_recreate_cpu_sessions(Cpu_root &cpu_root, Genode::List<Stored_cpu_session_info> &stored_cpu_sessions,
		Genode::List<Pd_session_component> &pd_sessions)
{
	if(verbose_debug) Genode::log("Resto::\033[33m", __func__, "\033[0m(...)");

	// There shall be only one CPU session by now (created during bootstrap)
	Cpu_session_component *bootstrapped_cpu_session = cpu_root.session_infos().first();

	// Error: No CPU session found
	if(!bootstrapped_cpu_session)
	{
		Genode::error("There is no bootstrapped CPU session");
		throw Genode::Exception();
	}
	// Warning: More than one CPU session found
	if(bootstrapped_cpu_session->next()) Genode::warning("There are more than 1 bootstrapped CPU sessions");

	Cpu_session_component *cpu_session = nullptr;
	Stored_cpu_session_info *stored_cpu_session = stored_cpu_sessions.first();
	while(stored_cpu_session)
	{
		if(stored_cpu_session->bootstrapped)
		{
			// Identify
			cpu_session = bootstrapped_cpu_session;
		}
		else
		{
			// Recreate
			Genode::Rpc_in_buffer<160> creation_args(stored_cpu_session->creation_args.string());
			Genode::Session_capability cpu_session_cap = cpu_root.session(creation_args, Genode::Affinity());
			cpu_session = _child.custom_services().cpu_root->session_infos().first();
			if(cpu_session) cpu_session = cpu_session->find_by_badge(cpu_session_cap.local_name());
			if(!cpu_session)
			{
				Genode::error("Could not find newly created RAM session for ", cpu_session_cap);
				throw Genode::Exception();
			}
		}
		// Remember the association of the stored RPC object to the new RPC object
		_rpcobject_translations.insert(new (_alloc) Badge_translation_info(stored_cpu_session->badge, cpu_session->cap()));

		_identify_recreate_cpu_threads(*cpu_session, stored_cpu_session->stored_cpu_thread_infos, pd_sessions);


		stored_cpu_session = stored_cpu_session->next();
	}
}


void Restorer::_identify_recreate_cpu_threads(Cpu_session_component &cpu_session, Genode::List<Stored_cpu_thread_info> &stored_cpu_threads,
		Genode::List<Pd_session_component> &pd_sessions)
{
	if(verbose_debug) Genode::log("Resto::\033[33m", __func__, "\033[0m(...)");

	// There shall be only one CPU session by now (created during bootstrap)
	Cpu_thread_component *bootstrapped_cpu_thread = cpu_session.parent_state().cpu_threads.first();

	// Error: No CPU thread found
	if(!bootstrapped_cpu_thread)
	{
		Genode::error("There is no bootstrapped CPU thread");
		throw Genode::Exception();
	}
	// Warning: More than one CPU thread found
	if(bootstrapped_cpu_thread->next()) Genode::warning("There are more than 1 bootstrapped CPU threads");

	Cpu_thread_component *cpu_thread = nullptr;
	Stored_cpu_thread_info *stored_cpu_thread = stored_cpu_threads.first();
	while(stored_cpu_thread)
	{
		if(stored_cpu_thread->bootstrapped)
		{
			// Identify
			cpu_thread = bootstrapped_cpu_thread;
		}
		else
		{
			// Recreate
			// First, find translation of PD session badge used for creating the CPU thread
			Badge_translation_info *trans_info = _rpcobject_translations.first();
			if(trans_info) trans_info = trans_info->find_by_ckpt_badge(stored_cpu_thread->pd_session_badge);
			if(!trans_info)
			{
				Genode::error("Could not find translation for stored PD session badge=", stored_cpu_thread->pd_session_badge);
				throw Genode::Exception();
			}
			// Find PD session corresponding to the found PD session badge
			Pd_session_component *pd_session = pd_sessions.first();
			if(pd_session) pd_session = pd_session->find_by_badge(trans_info->resto_cap.local_name());
			if(!pd_session)
			{
				Genode::error("Could not find PD session for ", trans_info->resto_cap);
				throw Genode::Exception();
			}
			// Create CPU thread
			Genode::Cpu_thread_capability cpu_thread_cap =
					cpu_session.create_thread(pd_session->cap(), stored_cpu_thread->name, stored_cpu_thread->affinity,
							stored_cpu_thread->weight, stored_cpu_thread->utcb);
			cpu_thread = cpu_session.parent_state().cpu_threads.first();
			if(cpu_thread) cpu_thread = cpu_thread->find_by_badge(cpu_thread_cap.local_name());
			if(!cpu_thread)
			{
				Genode::error("Could not find newly created CPU thread for ", cpu_thread_cap);
				throw Genode::Exception();
			}
		}
		// Remember the association of the stored RPC object to the new RPC object
		_rpcobject_translations.insert(new (_alloc) Badge_translation_info(stored_cpu_thread->badge, cpu_thread->cap()));

		stored_cpu_thread = stored_cpu_thread->next();
	}
}


void Restorer::_recreate_rm_sessions(Rm_root &rm_root, Genode::List<Stored_rm_session_info> &stored_rm_sessions)
{
	if(verbose_debug) Genode::log("Resto::\033[33m", __func__, "\033[0m(...)");

	// Warning: There are already RM sessions
	if(rm_root.session_infos().first())
		Genode::warning("There are already RM sessions created");

	Rm_session_component *rm_session = nullptr;
	Stored_rm_session_info *stored_rm_session = stored_rm_sessions.first();
	while(stored_rm_session)
	{
		// Recreate
		Genode::Rpc_in_buffer<160> creation_args(stored_rm_session->creation_args.string());
		Genode::Session_capability rm_session_cap = rm_root.session(creation_args, Genode::Affinity());
		rm_session = _child.custom_services().rm_root->session_infos().first();
		if(rm_session) rm_session = rm_session->find_by_badge(rm_session_cap.local_name());
		if(!rm_session)
		{
			Genode::error("Could not find newly created RM session for ", rm_session_cap);
			throw Genode::Exception();
		}
		// Remember the association of the stored RPC object to the new RPC object
		_rpcobject_translations.insert(new (_alloc) Badge_translation_info(stored_rm_session->badge, rm_session->cap()));

		_recreate_region_maps(*rm_session, stored_rm_session->stored_region_map_infos);

		stored_rm_session = stored_rm_session->next();
	}
}


void Restorer::_recreate_region_maps(
		Rm_session_component &rm_session, Genode::List<Stored_region_map_info> &stored_region_maps)
{
	if(verbose_debug) Genode::log("Resto::\033[33m", __func__, "\033[0m(...)");

	// Warning: There are already region maps in the RM session
	if(rm_session.parent_state().region_maps.first())
		Genode::warning("There are already region maps in RM session ", rm_session.cap());

	Region_map_component *region_map = nullptr;
	Stored_region_map_info *stored_region_map = stored_region_maps.first();
	while(stored_region_map)
	{
		// Recreate
		Genode::Capability<Genode::Region_map> cap = rm_session.create(stored_region_map->size);
		region_map = rm_session.parent_state().region_maps.first();
		if(region_map) region_map = region_map->find_by_badge(cap.local_name());
		if(!region_map)
		{
			Genode::error("Could not find newly created region map for ", cap);
			throw Genode::Exception();
		}
		// Remember the association of the stored RPC object to the new RPC object
		_rpcobject_translations.insert(new (_alloc) Badge_translation_info(stored_region_map->badge, region_map->cap()));
		// Remember the association of the region map's dataspace to the new region map's dataspace
		_rpcobject_translations.insert(new (_alloc) Badge_translation_info(stored_region_map->ds_badge, region_map->parent_state().ds_cap));

		stored_region_map = stored_region_map->next();
	}
}


void Restorer::_recreate_log_sessions(Log_root &log_root, Genode::List<Stored_log_session_info> &stored_log_sessions)
{
	if(verbose_debug) Genode::log("Resto::\033[33m", __func__, "\033[0m(...)");

	// Warning: There are already LOG sessions
	if(log_root.session_infos().first())
		Genode::warning("There are already LOG sessions created");

	Log_session_component *log_session = nullptr;
	Stored_log_session_info *stored_log_session = stored_log_sessions.first();
	while(stored_log_session)
	{
		// Recreate
		Genode::Rpc_in_buffer<160> creation_args(stored_log_session->creation_args.string());
		Genode::Session_capability log_session_cap = log_root.session(creation_args, Genode::Affinity());
		log_session = _child.custom_services().log_root->session_infos().first();
		if(log_session) log_session = log_session->find_by_badge(log_session_cap.local_name());
		if(!log_session)
		{
			Genode::error("Could not find newly created LOG session for ", log_session_cap);
			throw Genode::Exception();
		}
		// Remember the association of the stored RPC object to the new RPC object
		_rpcobject_translations.insert(new (_alloc) Badge_translation_info(stored_log_session->badge, log_session->cap()));

		stored_log_session = stored_log_session->next();
	}
}


void Restorer::_recreate_timer_sessions(Timer_root &timer_root, Genode::List<Stored_timer_session_info> &stored_timer_sessions)
{
	if(verbose_debug) Genode::log("Resto::\033[33m", __func__, "\033[0m(...)");

	// Warning: There are already Timer sessions
	if(timer_root.session_infos().first())
		Genode::warning("There are already Timer sessions created");

	Timer_session_component *timer_session = nullptr;
	Stored_timer_session_info *stored_timer_session = stored_timer_sessions.first();
	while(stored_timer_session)
	{
		// Recreate
		Genode::Rpc_in_buffer<160> creation_args(stored_timer_session->creation_args.string());
		Genode::Session_capability timer_session_cap = timer_root.session(creation_args, Genode::Affinity());
		timer_session = _child.custom_services().timer_root->session_infos().first();
		if(timer_session) timer_session = timer_session->find_by_badge(timer_session_cap.local_name());
		if(!timer_session)
		{
			Genode::error("Could not find newly created Timer session for ", timer_session_cap);
			throw Genode::Exception();
		}
		// Remember the association of the stored RPC object to the new RPC object
		_rpcobject_translations.insert(new (_alloc) Badge_translation_info(stored_timer_session->badge, timer_session->cap()));

		stored_timer_session = stored_timer_session->next();
	}
}


void Restorer::_restore_state_pd_sessions(Pd_root &pd_root, Genode::List<Stored_pd_session_info> &stored_pd_sessions)
{
	if(verbose_debug) Genode::log("Resto::\033[33m", __func__, "\033[0m(...)");

	Stored_pd_session_info *stored_pd_session = stored_pd_sessions.first();
	while(stored_pd_session)
	{
		// Find corresponding stored PD session
		Pd_session_component *pd_session = _find_child_object(stored_pd_session->badge, pd_root.session_infos());
		if(!pd_session)
		{
			Genode::error("Could not find child RAM session for ckpt badge ", stored_pd_session->badge);
			throw Genode::Exception();
		}

		// Restore state
		Genode::size_t ram_quota = Genode::Arg_string::find_arg(stored_pd_session->upgrade_args.string(), "ram_quota").ulong_value(0);
		if(ram_quota != 0) pd_root.upgrade(pd_session->cap(), stored_pd_session->upgrade_args.string());

		// Wrap Region_maps of child's and checkpointer's PD session in lists for using _restore_state_region_maps
		// The linked list pointers of the three regions maps are usually not used gloablly, thus they can be put into a new list
		// (hint: putting an object in Genode's lists overrides the next-pointer, if the object was in another list)
		Genode::List<Stored_region_map_info> temp_stored;
		temp_stored.insert(&stored_pd_session->stored_linker_area);
		temp_stored.insert(&stored_pd_session->stored_stack_area);
		temp_stored.insert(&stored_pd_session->stored_address_space);
		Genode::List<Region_map_component> temp_child;
		temp_child.insert(&pd_session->linker_area_component());
		temp_child.insert(&pd_session->stack_area_component());
		temp_child.insert(&pd_session->address_space_component());
		_restore_state_region_maps(temp_child, temp_stored, pd_root.session_infos());

		stored_pd_session = stored_pd_session->next();
	}
}


/*void Restorer::_restore_state_ram_sessions(Pd_root &ram_root, Genode::List<Stored_ram_session_info> &stored_ram_sessions)
{
	if(verbose_debug) Genode::log("Resto::\033[33m", __func__, "\033[0m(...)");

	Stored_ram_session_info *stored_ram_session = stored_ram_sessions.first();
	while(stored_ram_session)
	{
		// Find corresponding child RAM session
		Ram_session_component *ram_session = _find_child_object(stored_ram_session->badge, ram_root.session_infos());
		if(!ram_session)
		{
			Genode::error("Could not find child RAM session for ckpt badge ", stored_ram_session->badge);
			throw Genode::Exception();
		}

		// Restore state
		Genode::size_t ram_quota = Genode::Arg_string::find_arg(stored_ram_session->upgrade_args.string(), "ram_quota").ulong_value(0);
		if(ram_quota != 0) ram_root.upgrade(ram_session->cap(), stored_ram_session->upgrade_args.string());

		_restore_state_ram_dataspaces(*ram_session, stored_ram_session->stored_ramds_infos);

		stored_ram_session = stored_ram_session->next();
	}
}*/


void Restorer::_restore_state_ram_dataspaces(
		Ram_session_component &ram_session, Genode::List<Stored_ram_dataspace_info> &stored_ram_dataspaces)
{
	if(verbose_debug) Genode::log("Resto::\033[33m", __func__, "\033[0m(...)");

	Stored_ram_dataspace_info *stored_ram_dataspace = stored_ram_dataspaces.first();
	while(stored_ram_dataspace)
	{
		// Find corresponding child RAM dataspace
		Ram_dataspace_info *ram_dataspace = _find_child_object(stored_ram_dataspace->badge, ram_session.parent_state().ram_dataspaces);
		if(!ram_dataspace)
		{
			Genode::error("Could not find child RAM dataspace for ckpt badge ", stored_ram_dataspace->badge);
			throw Genode::Exception();
		}

		// Restore state
		// Postpone memory copy to a latter time
		Dataspace_translation_info *trans_info = new (_alloc) Dataspace_translation_info(
				stored_ram_dataspace->memory_content, ram_dataspace->cap, stored_ram_dataspace->size);
		_dataspace_translations.insert(trans_info);

		stored_ram_dataspace = stored_ram_dataspace->next();
	}

}


void Restorer::_restore_state_cpu_sessions(Cpu_root &cpu_root, Genode::List<Stored_cpu_session_info> &stored_cpu_sessions,
		Genode::List<Pd_session_component> &pd_sessions)
{
	if(verbose_debug) Genode::log("Resto::\033[33m", __func__, "\033[0m(...)");

	Stored_cpu_session_info *stored_cpu_session = stored_cpu_sessions.first();
	while(stored_cpu_session)
	{
		// Find corresponding child CPU session
		Cpu_session_component *cpu_session = _find_child_object(stored_cpu_session->badge, cpu_root.session_infos());
		if(!cpu_session)
		{
			Genode::error("Could not find child RAM session for ckpt badge ", stored_cpu_session->badge);
			throw Genode::Exception();
		}

		// Restore state
		// sigh
		if(stored_cpu_session->sigh_badge != 0)
		{
			// Restore sigh
			// Find sigh's resto cap
			Signal_context_info *context = nullptr;
			Pd_session_component *pd_session = pd_sessions.first();
			while(pd_session)
			{
				context = _find_child_object(stored_cpu_session->sigh_badge, pd_session->parent_state().signal_contexts);
				if(context) break;

				pd_session = pd_session->next();
			}
			if(!context)
			{
				Genode::error("Could not find child signal context for ckpt badge ", stored_cpu_session->sigh_badge);
				throw Genode::Exception();
			}
			cpu_session->exception_sigh(context->cap);
		}
		// Upgrade session if necessary
		Genode::size_t ram_quota = Genode::Arg_string::find_arg(stored_cpu_session->upgrade_args.string(), "ram_quota").ulong_value(0);
		if(ram_quota != 0) cpu_root.upgrade(cpu_session->cap(), stored_cpu_session->upgrade_args.string());

		_restore_state_cpu_threads(*cpu_session, stored_cpu_session->stored_cpu_thread_infos, pd_sessions);

		stored_cpu_session = stored_cpu_session->next();
	}
}


void Restorer::_restore_state_cpu_threads(Cpu_session_component &cpu_session, Genode::List<Stored_cpu_thread_info> &stored_cpu_threads,
		Genode::List<Pd_session_component> &pd_sessions)
{
	if(verbose_debug) Genode::log("Resto::\033[33m", __func__, "\033[0m(...)");

	Stored_cpu_thread_info *stored_cpu_thread = stored_cpu_threads.first();
	while(stored_cpu_thread)
	{
		// Find corresponding child CPU thread
		Cpu_thread_component *cpu_thread = _find_child_object(stored_cpu_thread->badge, cpu_session.parent_state().cpu_threads);
		if(!cpu_thread)
		{
			Genode::error("Could not find child CPU thread for ckpt badge ", stored_cpu_thread->badge);
			throw Genode::Exception();
		}

		// Restore state
		// sigh
		if(stored_cpu_thread->sigh_badge != 0)
		{
			// Restore sigh
			// Find sigh's resto cap
			Signal_context_info *context = nullptr;
			Pd_session_component *pd_session = pd_sessions.first();
			while(pd_session)
			{
				context = _find_child_object(stored_cpu_thread->sigh_badge, pd_session->parent_state().signal_contexts);
				if(context) break;

				pd_session = pd_session->next();
			}
			if(!context)
			{
				Genode::error("Could not find child signal context for ckpt badge ", stored_cpu_thread->sigh_badge);
				throw Genode::Exception();
			}
			cpu_thread->exception_sigh(context->cap);
		}
		// started
		if(stored_cpu_thread->started && !cpu_thread->parent_state().started)
		{
			//cpu_thread->start(stored_cpu_thread->ts.ip, stored_cpu_thread->ts.sp);
		}
		// paused
		if(stored_cpu_thread->paused && !cpu_thread->parent_state().paused)
		{
			//cpu_thread->pause();
		}
		// single_step
		if(stored_cpu_thread->single_step && !cpu_thread->parent_state().single_step)
		{
			cpu_thread->single_step(true);
		}
		// Thread state
		cpu_thread->state(stored_cpu_thread->ts);


		stored_cpu_thread = stored_cpu_thread->next();
	}
}


void Restorer::_restore_state_rm_sessions(Rm_root &rm_root, Genode::List<Stored_rm_session_info> &stored_rm_sessions,
		Genode::List<Pd_session_component> &pd_sessions)
{
	if(verbose_debug) Genode::log("Resto::\033[33m", __func__, "\033[0m(...)");

	Stored_rm_session_info *stored_rm_session = stored_rm_sessions.first();
	while(stored_rm_session)
	{
		// Find corresponding child RM session
		Rm_session_component *rm_session = _find_child_object(stored_rm_session->badge, rm_root.session_infos());
		if(!rm_session)
		{
			Genode::error("Could not find child RM session for ckpt badge ", stored_rm_session->badge);
			throw Genode::Exception();
		}

		// Restore state
		Genode::size_t ram_quota = Genode::Arg_string::find_arg(stored_rm_session->upgrade_args.string(), "ram_quota").ulong_value(0);
		if(ram_quota != 0) rm_root.upgrade(rm_session->cap(), stored_rm_session->upgrade_args.string());

		_restore_state_region_maps(rm_session->parent_state().region_maps, stored_rm_session->stored_region_map_infos, pd_sessions);

		stored_rm_session = stored_rm_session->next();
	}
}


void Restorer::_restore_state_region_maps(Genode::List<Region_map_component> &region_maps, Genode::List<Stored_region_map_info> &stored_region_maps,
		Genode::List<Pd_session_component> &pd_sessions)
{
	if(verbose_debug) Genode::log("Resto::\033[33m", __func__, "\033[0m(...)");

	Stored_region_map_info *stored_region_map = stored_region_maps.first();
	while(stored_region_map)
	{
		// Find corresponding child region map
		Region_map_component *region_map = _find_child_object(stored_region_map->badge, region_maps);
		if(!region_map)
		{
			Genode::error("Could not find child region map for ckpt badge ", stored_region_map->badge);
			throw Genode::Exception();
		}

		// Restore state
		// sigh
		if(stored_region_map->sigh_badge != 0)
		{
			// Restore sigh
			// Find sigh's resto cap
			Signal_context_info *context = nullptr;
			Pd_session_component *pd_session = pd_sessions.first();
			while(pd_session)
			{
				context = _find_child_object(stored_region_map->sigh_badge, pd_session->parent_state().signal_contexts);
				if(context) break;

				pd_session = pd_session->next();
			}
			if(!context)
			{
				Genode::error("Could not find child signal context for ckpt badge ", stored_region_map->sigh_badge);
				throw Genode::Exception();
			}
			region_map->fault_handler(context->cap);
		}
		// Attached regions
		_restore_state_attached_regions(*region_map, stored_region_map->stored_attached_region_infos);

		stored_region_map = stored_region_map->next();
	}
}


void Restorer::_restore_state_attached_regions(Region_map_component &region_map,
		Genode::List<Stored_attached_region_info> &stored_attached_regions)
{
	if(verbose_debug) Genode::log("Resto::\033[33m", __func__, "\033[0m(...)");

	Stored_attached_region_info *stored_attached_region = stored_attached_regions.first();
	while(stored_attached_region)
	{
		Attached_region_info *attached_region = nullptr;

		// Find out, whether the attached dataspace was recreated in the previous phase
		// and, thus, has a translation in the RPC object translation list. If not, it is recreated here.
		// This new RPC object has to be mapped into the capability space/map and stored to the RPC object translation list.
		Genode::Dataspace_capability ds_cap;
		Badge_translation_info  *trans_info = _rpcobject_translations.first();
		if(trans_info) trans_info = trans_info->find_by_ckpt_badge(stored_attached_region->attached_ds_badge);
		if(trans_info)
		{
			ds_cap = Genode::reinterpret_cap_cast<Genode::Dataspace>(trans_info->resto_cap);
		}
		else
		{
			// XXX Optimization: Let the native RAM session recreate the dataspace, thus, it can be integrated
			// into incremental checkpointing
			ds_cap = _child._env.ram().alloc(stored_attached_region->size);

			// Remember the association of the stored RPC object to the new RPC object
			_rpcobject_translations.insert(new (_alloc) Badge_translation_info(
					stored_attached_region->attached_ds_badge, ds_cap));
		}

		// Attach the dataspace to the region map
		//using Genode::Hex;
		//Genode::Dataspace_client ds_client(ds_cap);
		//Genode::log(Hex(ds_client.size()), " ", Hex(ds_client.phys_addr()), " ", ds_client.writable());

		try {
		region_map.attach(ds_cap, stored_attached_region->size, 0,
				true, stored_attached_region->rel_addr, stored_attached_region->executable);
		} catch(Genode::Region_map::Region_conflict) {
			/**
			 * if address already in use, try with next region…
			 * temporary workaround to get further in c/r process
			 */
			Genode::error("Address already in use");
			stored_attached_region = stored_attached_region->next();
			continue;
		}

		// Find the attached dataspace in the region map
		attached_region = region_map.parent_state().attached_regions.first();
		if(attached_region) attached_region = attached_region->find_by_addr(stored_attached_region->rel_addr);
		if(!attached_region)
		{
			Genode::error("Could not find recreated attached region for attached region ",
					stored_attached_region->badge, " at address ", stored_attached_region->rel_addr);
			throw Genode::Exception();
		}

		// Find out, whether the attached dataspace is a known region map. If it is not a region map, and if it is not already
		// stored in the dataspace translation list (meaning it has to be copied), then store the dataspace and its corresponding
		// checkpointed dataspace in the dataspace translation list
		Ref_badge_info *badge_info = _region_maps.first();
		if(badge_info) badge_info = badge_info->find_by_badge(stored_attached_region->attached_ds_badge);
		if(!badge_info)
		{
			Dataspace_translation_info *trans_info = _dataspace_translations.first();
			if(trans_info) trans_info = trans_info->find_by_ckpt_badge(stored_attached_region->memory_content.local_name());
			if(!trans_info)
			{
				trans_info = new (_alloc) Dataspace_translation_info(
						stored_attached_region->memory_content, attached_region->attached_ds_cap, stored_attached_region->size);
				_dataspace_translations.insert(trans_info);
			}
		}

		stored_attached_region = stored_attached_region->next();
	}
}


void Restorer::_restore_state_log_sessions(Log_root &log_root, Genode::List<Stored_log_session_info> &stored_log_sessions)
{
	if(verbose_debug) Genode::log("Resto::\033[33m", __func__, "\033[0m(...)");

	Stored_log_session_info *stored_log_session = stored_log_sessions.first();
	while(stored_log_session)
	{
		// Find corresponding child LOG session
		Log_session_component *log_session = _find_child_object(stored_log_session->badge, log_root.session_infos());

		// Restore state
		Genode::size_t ram_quota = Genode::Arg_string::find_arg(stored_log_session->upgrade_args.string(), "ram_quota").ulong_value(0);
		if(ram_quota != 0) log_root.upgrade(log_session->cap(), stored_log_session->upgrade_args.string());

		stored_log_session = stored_log_session->next();
	}
}


void Restorer::_restore_state_timer_sessions(Timer_root &timer_root, Genode::List<Stored_timer_session_info> &stored_timer_sessions,
		Genode::List<Pd_session_component> &pd_sessions)
{
	if(verbose_debug) Genode::log("Resto::\033[33m", __func__, "\033[0m(...)");

	Stored_timer_session_info *stored_timer_session = stored_timer_sessions.first();
	while(stored_timer_session)
	{
		// Find corresponding child Timer session
		Timer_session_component *timer_session = _find_child_object(stored_timer_session->badge, timer_root.session_infos());

		// Restore state
		// sigh
		if(stored_timer_session->sigh_badge != 0)
		{
			// Find Signal context info
			Signal_context_info *context = nullptr;
			Pd_session_component *pd_session = pd_sessions.first();
			while(pd_session)
			{
				context = _find_child_object(stored_timer_session->sigh_badge, pd_session->parent_state().signal_contexts);
				if(context) break;

				pd_session = pd_session->next();
			}
			if(!context)
			{
				Genode::error("Could not find child signal context for ckpt badge ", stored_timer_session->sigh_badge);
				throw Genode::Exception();
			}
			timer_session->sigh(context->cap);
		}
		// timeout and periodic
		if(stored_timer_session->timeout != 0 && stored_timer_session->periodic)
		{
			timer_session->trigger_periodic(stored_timer_session->timeout);
		}
		else if(stored_timer_session->timeout != 0 && !stored_timer_session->periodic)
		{
			timer_session->trigger_once(stored_timer_session->timeout);
		}
		// Upgrade session if necessary
		Genode::size_t ram_quota = Genode::Arg_string::find_arg(stored_timer_session->upgrade_args.string(), "ram_quota").ulong_value(0);
		if(ram_quota != 0) timer_root.upgrade(timer_session->cap(), stored_timer_session->upgrade_args.string());

		stored_timer_session = stored_timer_session->next();
	}
}


void Restorer::_create_managed_dataspace_list(Genode::List<Pd_session_component> &ram_sessions)
{
	if(verbose_debug) Genode::log("Resto::\033[33m", __func__, "\033[0m(...)");

	typedef Simplified_managed_dataspace_info::Simplified_designated_ds_info Sim_dd_info;

	Pd_session_component *ram_session = ram_sessions.first();
	while(ram_session)
	{
		Ram_dataspace_info *ramds_info = ram_session->parent_state().ram_dataspaces.first();
		while(ramds_info)
		{
			// RAM dataspace is managed
			if(ramds_info->mrm_info)
			{
				Genode::List<Sim_dd_info> sim_dd_infos;
				Designated_dataspace_info *dd_info = ramds_info->mrm_info->dd_infos.first();
				while(dd_info)
				{
					Genode::Ram_dataspace_capability dd_info_cap =
							Genode::reinterpret_cap_cast<Genode::Ram_dataspace>(dd_info->cap);

					sim_dd_infos.insert(new (_alloc) Sim_dd_info(dd_info_cap, dd_info->rel_addr, dd_info->size, dd_info->attached));

					dd_info = dd_info->next();
				}

				_managed_dataspaces.insert(new (_alloc) Simplified_managed_dataspace_info(ramds_info->cap, sim_dd_infos));
			}

			ramds_info = ramds_info->next();
		}

		ram_session = ram_session->next();
	}
}


void Restorer::_restore_cap_space()
{
	if(verbose_debug) Genode::log("Resto::\033[33m", __func__, "\033[0m(...)");

	// Install capabilities to the child's cap space
	Genode::Pd_session_client pd_client(_child.pd().parent_cap());
}


void Restorer::_restore_dataspaces()
{
	if(verbose_debug) Genode::log("Resto::\033[33m", __func__, "\033[0m(...)");

	Dataspace_translation_info *memory_info = _dataspace_translations.first();
	while(memory_info)
	{
		if(!memory_info->processed)
		{
			// Resolve managed dataspace of the incremental checkpointing mechanism
			Simplified_managed_dataspace_info *smd_info = _managed_dataspaces.first();
			if(smd_info) smd_info = smd_info->find_by_badge(memory_info->resto_ds_cap.local_name());
			// Dataspace is managed
			if(smd_info)
			{
				Simplified_managed_dataspace_info::Simplified_designated_ds_info *sdd_info =
						smd_info->designated_dataspaces.first();
				while(sdd_info)
				{
					_restore_dataspace_content(sdd_info->dataspace_cap, memory_info->ckpt_ds_cap, sdd_info->addr, sdd_info->size);

					sdd_info = sdd_info->next();
				}

			}
			// Dataspace is not managed
			else
			{
				_restore_dataspace_content(memory_info->resto_ds_cap, memory_info->ckpt_ds_cap, 0, memory_info->size);
			}

			memory_info->processed = true;
		}

		memory_info = memory_info->next();
	}
}


void Restorer::_restore_dataspace_content(Genode::Dataspace_capability dst_ds_cap,
		Genode::Dataspace_capability src_ds_cap, Genode::addr_t src_offset, Genode::size_t size)
{
	if(verbose_debug) Genode::log("Resto::\033[33m", __func__, "\033[0m(dst ", dst_ds_cap,
			", src ", src_ds_cap, ", src offset=", Genode::Hex(src_offset),
			", size=", Genode::Hex(size), ")");

	char *dst_start_addr = _state._env.rm().attach(dst_ds_cap);
	char *src_start_addr = _state._env.rm().attach(src_ds_cap);

	Genode::memcpy(dst_start_addr, src_start_addr + src_offset, size);

	_state._env.rm().detach(src_start_addr);
	_state._env.rm().detach(dst_start_addr);
}

void Restorer::_start_threads(Cpu_root &cpu_root, Genode::List<Stored_cpu_session_info> &stored_cpu_sessions)
{
	if(verbose_debug) Genode::log("Resto::\033[33m", __func__, "\033[0m(...)");

	// Iterate through all stored CPU sessions
	Stored_cpu_session_info *stored_cpu_session = stored_cpu_sessions.first();
	while(stored_cpu_session) {
		Cpu_session_component *cpu_session = _find_child_object(stored_cpu_session->badge, cpu_root.session_infos());
		Genode::log("--- session: ", cpu_session->cap());
		Stored_cpu_thread_info *stored_cpu_thread = stored_cpu_session->stored_cpu_thread_infos.first();
		while(stored_cpu_thread) {
			Cpu_thread_component *cpu_thread = _find_child_object(stored_cpu_thread->badge, cpu_session->parent_state().cpu_threads);
			Genode::log("------ thread: ", cpu_thread->cap());
			Genode::Thread_state ts = cpu_thread->state();
			print_thread_state(ts, true);
			stored_cpu_thread = stored_cpu_thread->next();
		}

		stored_cpu_session = stored_cpu_session->next();
	}
	Genode::log("done");

	// Iterate through all stored CPU sessions
	stored_cpu_session = stored_cpu_sessions.first();
	while(stored_cpu_session)
	{
		// Find corresponding recreated CPU session
		Cpu_session_component *cpu_session = _find_child_object(stored_cpu_session->badge, cpu_root.session_infos());
		if(!cpu_session)
		{
			Genode::error("Could not find child CPU session for ckpt badge ", stored_cpu_session->badge);
			throw Genode::Exception();
		}

		// Iterate through all stored CPU threads of the stored CPU session
		Stored_cpu_thread_info *stored_cpu_thread = stored_cpu_session->stored_cpu_thread_infos.first();
		while(stored_cpu_thread)
		{
			// Find corresponding recreated CPU thread
			Cpu_thread_component *cpu_thread = _find_child_object(stored_cpu_thread->badge, cpu_session->parent_state().cpu_threads);
			if(!cpu_thread)
			{
				Genode::error("Could not find child CPU thread for ckpt badge ", stored_cpu_thread->badge);
				throw Genode::Exception();
			}

			if(stored_cpu_thread->started)
			{
				if(true or !Genode::strcmp(cpu_thread->parent_state().name.string(), "signal handler"))
				{
					Genode::Thread_state ts = cpu_thread->state();
					print_thread_state(ts, true);

					cpu_thread->start(stored_cpu_thread->ts.ip, stored_cpu_thread->ts.sp);
				}
			}

			stored_cpu_thread = stored_cpu_thread->next();
		}

		stored_cpu_session = stored_cpu_session->next();
	}

}


Restorer::Restorer(Genode::Allocator &alloc, Target_child &child, Target_state &state)
: _alloc(alloc), _child(child), _state(state) { }


Restorer::~Restorer()
{
	_destroy_list(_rpcobject_translations);
	_destroy_list(_dataspace_translations);
	_destroy_list(_region_maps);
	_destroy_list(_managed_dataspaces);
}


void Restorer::restore()
{
	if(verbose_debug) Genode::log("Resto::\033[33m", __func__, "\033[0m()");

	Genode::log("Before: \n", _child);

	// Create list of region maps
	_region_maps = _create_region_map_dataspaces_list(_state._stored_pd_sessions, _state._stored_rm_sessions);

	if(verbose_debug)
	{
		Genode::log("Region map dataspaces (from ckpt):");
		Ref_badge_info *info = _region_maps.first();
		if(!info) Genode::log(" <empty>\n");
		while(info)
		{
			Genode::log(" ", *info);
			info = info->next();
		}
	}

	/***********************************
	 ** Phase 1: Recreate RPC objects **
	 ***********************************/

	// Identify or recreate RPC objects (caution: PD <- CPU thread <- Native cap ( "<-" means requires))
	// During the iterations _rpcobject_translations are filled for the next steps
	//_identify_recreate_ram_sessions(*_child.custom_services().ram_root, _state._stored_ram_sessions);
	_identify_recreate_pd_sessions(*_child.custom_services().pd_root, _state._stored_pd_sessions);
	_identify_recreate_cpu_sessions(*_child.custom_services().cpu_root, _state._stored_cpu_sessions,
			_child.custom_services().pd_root->session_infos());
	if(_state._stored_rm_sessions.first())
	{
		_child.custom_services().find("RM"); // Implicitly creates root object
		_recreate_rm_sessions(*_child.custom_services().rm_root, _state._stored_rm_sessions);
	}
	if(_state._stored_log_sessions.first())
	{
		_child.custom_services().find("LOG"); // Implicitly creates root object
		_recreate_log_sessions(*_child.custom_services().log_root, _state._stored_log_sessions);
	}
	if(_state._stored_timer_sessions.first())
	{
		_child.custom_services().find("Timer"); // Implicitly creates root object
		_recreate_timer_sessions(*_child.custom_services().timer_root, _state._stored_timer_sessions);
	}

	if(verbose_debug)
	{
		Genode::log("RPC object translations:");
		Badge_translation_info *info = _rpcobject_translations.first();
		if(!info) Genode::log(" <empty>\n");
		while(info)
		{
			Genode::log(" ", *info);
			info = info->next();
		}
	}

	/*******************************************
	 ** Phase 2: Restore state of RPC objects **
	 *******************************************/

	// Restore state of all objects using the translation of old badges to new badges
	// (caution: RAM <- Region map (incl. PD session) ( "<-" means requires))
	//   Also make a mapping of memory to restore
	//_restore_state_ram_sessions(*_child.custom_services().ram_root, _state._stored_ram_sessions);
	_restore_state_pd_sessions(*_child.custom_services().pd_root, _state._stored_pd_sessions);
	_restore_state_cpu_sessions(*_child.custom_services().cpu_root, _state._stored_cpu_sessions,
			_child.custom_services().pd_root->session_infos());
	if(_child.custom_services().rm_root)
	{
		_restore_state_rm_sessions(*_child.custom_services().rm_root, _state._stored_rm_sessions,
				_child.custom_services().pd_root->session_infos());
	}
	if(_child.custom_services().log_root)
	{
		_restore_state_log_sessions(*_child.custom_services().log_root, _state._stored_log_sessions);
	}
	if(_child.custom_services().timer_root)
	{
		_restore_state_timer_sessions(*_child.custom_services().timer_root, _state._stored_timer_sessions,
				_child.custom_services().pd_root->session_infos());
	}

	// Create a list of managed dataspaces
	_create_managed_dataspace_list(_child.custom_services().pd_root->session_infos());

	if(verbose_debug)
	{
		Genode::log("Memory to restore:");
		Dataspace_translation_info *info = _dataspace_translations.first();
		if(!info) Genode::log(" <empty>\n");
		while(info)
		{
			Genode::log(" ", *info);
			info = info->next();
		}
	}

	if(verbose_debug)
	{
		Genode::log("Managed dataspaces:");
		Simplified_managed_dataspace_info const *smd_info = _managed_dataspaces.first();
		if(!smd_info) Genode::log(" <empty>\n");
		while(smd_info)
		{
			Genode::log(" ", *smd_info);

			Simplified_managed_dataspace_info::Simplified_designated_ds_info const *sdd_info =
					smd_info->designated_dataspaces.first();
			if(!sdd_info) Genode::log("  <empty>\n");
			while(sdd_info)
			{
				Genode::log("  ", *sdd_info);

				sdd_info = sdd_info->next();
			}

			smd_info = smd_info->next();
		}
	}

	// Insert capabilities of all objects into capability space
	_restore_cap_space();

	// Copy stored dataspaces' content to child dataspaces' content
	_restore_dataspaces();

	Genode::log("After: \n", _child);

	// Start threads
	_start_threads(*_child.custom_services().cpu_root, _state._stored_cpu_sessions);

	// Clean up
	_destroy_list(_rpcobject_translations);
	_destroy_list(_dataspace_translations);
	_destroy_list(_region_maps);
	_destroy_list(_managed_dataspaces);


}

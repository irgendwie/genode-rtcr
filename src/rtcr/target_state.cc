/*
 * \brief  Target's state
 * \author Denis Huber
 * \date   2016-10-25
 */

#include "target_state.h"

using namespace Rtcr;


template <typename T>
void Target_state::_delete_list(Genode::List<T> &infos)
{
	while(T* info = infos.first())
	{
		infos.remove(info);
		Genode::destroy(_alloc, info);
	}
}
template void Target_state::_delete_list(Genode::List<Stored_log_session_info> &infos);
template void Target_state::_delete_list(Genode::List<Stored_timer_session_info> &infos);
template void Target_state::_delete_list(Genode::List<Stored_thread_info> &infos);
template void Target_state::_delete_list(Genode::List<Stored_attached_region_info> &infos);
template void Target_state::_delete_list(Genode::List<Stored_signal_context_info> &infos);
template void Target_state::_delete_list(Genode::List<Stored_signal_source_info> &infos);


void Target_state::_delete_list(Genode::List<Stored_rm_session_info> &infos)
{
	while(Stored_rm_session_info* info = infos.first())
	{
		infos.remove(info);
		_delete_list(info->stored_region_map_infos);
		Genode::destroy(_alloc, info);
	}
}


void Target_state::_delete_list(Genode::List<Stored_region_map_info> &infos)
{
	while(Stored_region_map_info* info = infos.first())
	{
		infos.remove(info);
		_delete_list(info->stored_attached_region_infos);
		Genode::destroy(_alloc, info);
	}
}


void Target_state::_delete_list(Genode::List<Stored_dataspace_info> &infos)
{
	while(Stored_dataspace_info* info = infos.first())
	{
		infos.remove(info);
		_env.ram().free(Genode::static_cap_cast<Genode::Ram_dataspace>(info->ds_cap));
		Genode::destroy(_alloc, info);
	}
}

template <typename T>
Genode::List<T> Target_state::_copy_list(Genode::List<T> &from_infos)
{
	Genode::List<T> to_infos;

	// Copy list elements from from_infos
	T *from_info = from_infos.first();
	while(from_info)
	{
		T *to_info = new (_alloc) T(*from_info);
		to_infos.insert(to_info);
		from_info = from_info->next();
	}

	return to_infos;
}
template Genode::List<Stored_log_session_info>     Target_state::_copy_list(Genode::List<Stored_log_session_info> &from_infos);
template Genode::List<Stored_timer_session_info>   Target_state::_copy_list(Genode::List<Stored_timer_session_info> &from_infos);
template Genode::List<Stored_thread_info>          Target_state::_copy_list(Genode::List<Stored_thread_info> &from_infos);
template Genode::List<Stored_attached_region_info> Target_state::_copy_list(Genode::List<Stored_attached_region_info> &from_infos);
template Genode::List<Stored_signal_context_info>  Target_state::_copy_list(Genode::List<Stored_signal_context_info> &from_infos);
template Genode::List<Stored_signal_source_info>   Target_state::_copy_list(Genode::List<Stored_signal_source_info> &from_infos);


Genode::List<Stored_rm_session_info> Target_state::_copy_list(Genode::List<Stored_rm_session_info> &from_infos)
{
	Genode::List<Stored_rm_session_info> to_infos;

	// Copy list elements from from_infos
	Stored_rm_session_info *from_info = from_infos.first();
	while(from_info)
	{
		Stored_rm_session_info *to_info = new (_alloc) Stored_rm_session_info(*from_info);
		to_info->stored_region_map_infos = _copy_list(from_info->stored_region_map_infos);
		to_infos.insert(to_info);
		from_info = from_info->next();
	}

	return to_infos;
}


Genode::List<Stored_region_map_info> Target_state::_copy_list(Genode::List<Stored_region_map_info> &from_infos)
{
	Genode::List<Stored_region_map_info> to_infos;

	// Copy list elements from from_infos
	Stored_region_map_info *from_info = from_infos.first();
	while(from_info)
	{
		Stored_region_map_info *to_info = new (_alloc) Stored_region_map_info(*from_info);
		to_info->stored_attached_region_infos = _copy_list(from_info->stored_attached_region_infos);

		to_infos.insert(to_info);
		from_info = from_info->next();
	}

	return to_infos;
}


Genode::List<Stored_dataspace_info> Target_state::_copy_list(Genode::List<Stored_dataspace_info> &from_infos)
{
	Genode::List<Stored_dataspace_info> to_infos;

	// Copy list elements from from_infos
	Stored_dataspace_info *from_info = from_infos.first();
	while(from_info)
	{
		// Create new dataspace and copy the contents of from_info to it
		Stored_dataspace_info *to_info = new (_alloc) Stored_dataspace_info(*from_info);
		to_info->ds_cap = _copy_dataspace(from_info->ds_cap, from_info->size);;

		to_infos.insert(to_info);

		from_info = from_info->next();
	}

	return to_infos;
}


Genode::Dataspace_capability Target_state::_copy_dataspace(Genode::Dataspace_capability source_ds_cap, Genode::size_t size, Genode::off_t dest_offset)
{
	Genode::Dataspace_capability dest_ds_cap = _env.ram().alloc(size);

	char *source = _env.rm().attach(source_ds_cap);
	char *dest   = _env.rm().attach(dest_ds_cap);

	Genode::memcpy(dest + dest_offset, source, size);

	_env.rm().detach(dest);
	_env.rm().detach(source);

	return dest_ds_cap;
}


Target_state::Target_state(Genode::Env &env, Genode::Allocator &alloc)
:
	_env   (env),
	_alloc (alloc)
{ }


Target_state::Target_state(Target_state &other)
:
	_env   (other._env),
	_alloc (other._alloc)
{
	// Known RM sessions, their region maps, and their attached dataspaces
	_stored_rm_sessions =     _copy_list(other._stored_rm_sessions);
	// LOG session
	_stored_log_sessions =    _copy_list(other._stored_log_sessions);
	// Timer session
	_stored_timer_sessions =  _copy_list(other._stored_timer_sessions);
	// Primary CPU session
	_stored_threads =         _copy_list(other._stored_threads);
	// Dataspaces
	_stored_dataspaces =      _copy_list(other._stored_dataspaces);
	// Signal contexts
	_stored_signal_contexts = _copy_list(other._stored_signal_contexts);
	// Signal sources
	_stored_signal_sources =  _copy_list(other._stored_signal_sources);

	// Address space
	_stored_address_space = other._stored_address_space;
	_stored_address_space.stored_attached_region_infos = _copy_list(other._stored_address_space.stored_attached_region_infos);
	// Stack area
	_stored_stack_area = other._stored_stack_area;
	_stored_stack_area.stored_attached_region_infos = _copy_list(other._stored_stack_area.stored_attached_region_infos);
	// Linker area
	_stored_linker_area = other._stored_linker_area;
	_stored_linker_area.stored_attached_region_infos =   _copy_list(other._stored_linker_area.stored_attached_region_infos);
}


Target_state::~Target_state()
{
	_delete_list(_stored_rm_sessions);
	_delete_list(_stored_log_sessions);
	_delete_list(_stored_timer_sessions);
	_delete_list(_stored_threads);
	_delete_list(_stored_address_space.stored_attached_region_infos);
	_delete_list(_stored_stack_area.stored_attached_region_infos);
	_delete_list(_stored_linker_area.stored_attached_region_infos);
	_delete_list(_stored_dataspaces);
}

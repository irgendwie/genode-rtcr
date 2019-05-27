#include <base/component.h>
#include <timer_session/connection.h>
#include <base/log.h>

Genode::size_t Component::stack_size() { return 16*1024; }

struct Main
{
	Genode::Env &_env;

	Timer::Connection _timer { _env };

	unsigned int n = 0;

	void _count()
	{
		if(n == 1)
			Genode::log("1 sheep. zzZ");
		else
			Genode::log(n, " sheeps. zzZ");
		n++;
	}

	Genode::Signal_handler<Main> _timeout_handler
	{
		_env.ep(), *this, &Main::_count
	};

	Main(Genode::Env &env) : _env(env)
	{
		_timer.sigh(_timeout_handler);
		_timer.trigger_periodic(1000 * 1000);
	}
};

void Component::construct(Genode::Env &env)
{
	static Main main(env);
}
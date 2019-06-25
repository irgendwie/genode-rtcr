#include <base/component.h>
#include <base/log.h>
#include <base/heap.h>

//#include <timer_session/connection.h>

Genode::size_t Component::stack_size() { return 16*1024; }

namespace Rtcr {
    struct SheepThread;
}

struct Rtcr::SheepThread : public Genode::Thread
{
    private:
    Genode::Env &_env;
    unsigned int n = 0;

    //Timer::Connection _timer { _env };

    public:
    SheepThread(Genode::Env &env, Genode::String<64> name, Genode::size_t stack_size) :
        Genode::Thread(env, name, stack_size),
        _env(env)
    { }

    void entry()
    {
        while(true)
        {
            if(n == 1)
                Genode::log("1 sheep. zzZ");
            else
                Genode::log(n, " sheeps. zzZ");

            //_timer.msleep(1000);
            for(unsigned i = 0; i < 500 * 1000 * 1000; i++)
                __asm__("nop");

            n++;
        }
    }
};

struct Main
{
    Genode::Env &_env;
    Genode::Heap _heap{_env.ram(), _env.rm()};

	Main(Genode::Env &env) : _env(env)
	{
        Genode::log("Starting sheep counter thread");

        auto sheep_thread = new (_heap) Rtcr::SheepThread(env, "SheepCounter", 16 * 1024);
        sheep_thread->start();

        Genode::log("Done. Exiting.");
	}
};

void Component::construct(Genode::Env &env)
{
	static Main main(env);
}
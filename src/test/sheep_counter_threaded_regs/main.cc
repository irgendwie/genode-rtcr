#include <base/component.h>

Genode::size_t Component::stack_size() { return 16*1024; }

namespace Rtcr {
    struct SheepThread;
}

struct Rtcr::SheepThread : public Genode::Thread
{
    private:
    Genode::Env &_env;
    unsigned int n = 0;

    public:
    SheepThread(Genode::Env &env, Genode::String<64> name, Genode::size_t stack_size) :
        Genode::Thread(env, name, stack_size),
        _env(env)
    { }

    void entry()
    {
        while(true)
        {
            /* sleep some time */
            for(unsigned i = 0; i < 500 * 1000 * 1000; i++)
                __asm__("nop");

            /* increase counter */
            n++;

            /* save to register */
            __asm__("mov r6, %[input]" : : [input] "r" (n));
        }
    }
};

struct Main
{
    Genode::Env &_env;

	Main(Genode::Env &env) : _env(env)
	{
        static Rtcr::SheepThread sheep_thread(env, "SheepCounter", 16 * 1024);
        sheep_thread.start();
	}
};

void Component::construct(Genode::Env &env)
{
	static Main main(env);
}
#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"
#include <utils/hook.hpp>
#include "console.hpp"
#include "scheduler.hpp"
#include "demo_playback.h"

namespace timing
{
	game::dvar_t* timescale;
	utils::hook::detour com_timescale_msec_hook;

	void frame() {
		*(float*)0x147B754EC = timescale->current.value;
		*(float*)0x147B74C78 = timescale->current.value;

		*(char*)0x147B74C60 = 0;
		*(char*)0x147B74C7C = 0;

		//game::Com_SetSlowMotion(timescale->current.value, timescale->current.value, 100);
	}

	int com_timescale_msec_stub(int msec) {

		if (demo_playback::is_paused()) {
			return 0;
		}

		return com_timescale_msec_hook.invoke<int>(msec);
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			if (game::environment::is_mp()) {
				timescale = game::Dvar_RegisterFloat("timescale", 1.0f, 0.1f, 10.0f, game::DVAR_FLAG_NONE, "Control the speed of gameplay");
				scheduler::loop(frame, scheduler::pipeline::main);
			
				com_timescale_msec_hook.create(game::Com_TimeScaleMsec, com_timescale_msec_stub);
			}
		}
	};
}

REGISTER_COMPONENT(timing::component)
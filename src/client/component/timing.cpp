#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"
#include <utils/hook.hpp>
#include "console.hpp"
#include "scheduler.hpp"

namespace timing
{
	game::dvar_t* timescale;

	void frame() {
		*(float*)0x147B754EC = timescale->current.value;
		*(float*)0x147B74C78 = timescale->current.value;

		*(char*)0x147B74C60 = 0;
		*(char*)0x147B74C7C = 0;

		//game::Com_SetSlowMotion(timescale->current.value, timescale->current.value, 100);
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			if (game::environment::is_mp()) {
				timescale = game::Dvar_RegisterFloat("timescale", 1.0f, 0.1f, 10.0f, game::DVAR_FLAG_NONE, "Control the speed of gameplay");
				scheduler::loop(frame, scheduler::pipeline::main);
			}
		}
	};
}

REGISTER_COMPONENT(timing::component)
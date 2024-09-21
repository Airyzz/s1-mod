#include <std_include.hpp>
#include "loader/component_loader.hpp"
#include "game/game.hpp"

#include "command.hpp"
#include "console.hpp"
#include "filesystem.hpp"
#include "network.hpp"
#include "party.hpp"
#include "scheduler.hpp"
#include "server_list.hpp"
#include "dvars.hpp"
#include <utils/hook.hpp>
#include <utils/string.hpp>
#include "theater_camera.h"
#include "demo_playback.h"

namespace theater_rendering
{
	namespace
	{
		const game::dvar_t* r_disable_boost_fx;
		const game::dvar_t* r_disable_specific;
		const game::dvar_t* r_disable_shellshock;

		utils::hook::detour cl_renderscene_hook;
		utils::hook::detour should_draw_post_fx_type_hook;
		utils::hook::detour cg_draw_shellshock_blend_hook;

		uint64_t cl_render_scene_stub(int localClientNum, game::refdef_t* refdef, void* u1, void* u2) {
			auto result = cl_renderscene_hook.invoke<uint64_t>(localClientNum, refdef, u1, u2);
			console::info("Rendering scene with refdef: %llx  %llx  %llx, (%f, %f, %f)\n", refdef, u1, u2, refdef->origin[0], refdef->origin[1], refdef->origin[2]);
			return result;
		}

		uint64_t cg_draw_shellshock_blend_stub(uint64_t a1) {

			if (demo_playback::is_playing()) {
				if ( theater_camera::get_current_mode() != THEATER_CAMERA_FIRST_PERSON || r_disable_shellshock->current.enabled) {
					return 0;
				}
			}

			return cg_draw_shellshock_blend_hook.invoke<uint64_t>(a1);
		}

		bool should_draw_post_fx_type_stub(void* a1, int type) {

			// console::info("Should draw fx %d: \n", type);

			if (demo_playback::is_playing()) {
				if (theater_camera::get_current_mode() != THEATER_CAMERA_FIRST_PERSON || r_disable_boost_fx->current.enabled) {
					if (type == 28) {
						return false;
					}
				}
			}

			if (type == r_disable_specific->current.integer) {
				return false;
			}

			bool result = should_draw_post_fx_type_hook.invoke<bool>(a1, type);

			return result;
		}
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{

			cl_renderscene_hook.create(0x1401FD680, cl_render_scene_stub);
			should_draw_post_fx_type_hook.create(0x1405FAE90, should_draw_post_fx_type_stub);
			cg_draw_shellshock_blend_hook.create(0x1401D5420, cg_draw_shellshock_blend_stub);

			r_disable_boost_fx = game::Dvar_RegisterBool("r_disable_boost_fx", false, game::DVAR_FLAG_NONE, "Disable screen fx");

			r_disable_shellshock = game::Dvar_RegisterBool("r_disableShellshock", false, game::DVAR_FLAG_NONE, "Disable shellshock fx");

			r_disable_specific = game::Dvar_RegisterInt("r_disable_specific", -1, -1, 1000, game::DVAR_FLAG_NONE, "Disable some fx");
		}
	};
}

REGISTER_COMPONENT(theater_rendering::component)

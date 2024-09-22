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

namespace theater_camera
{

	const char* modes[] =
	{
		"first_person",
		"third_person",
		"freecam",
		(char*)0,
	};

	const game::dvar_t* demo_camera_mode;
	utils::hook::detour bg_is_thirdperson_hook;
	utils::hook::detour cg_calc_view_values_hook;
	utils::hook::detour cl_renderscene_hook;
	utils::hook::detour r_update_lod_parms_hook;

	game::vec3_t freecam_pos;
	game::vec4_t freecam_quat;


	bool bg_is_thirdperson_stub(game::playerState_s* state) {
		if (demo_playback::is_playing() == false) {
			return bg_is_thirdperson_hook.invoke<bool>(state);
		}

		if (demo_camera_mode->current.integer == THEATER_CAMERA_FIRST_PERSON) {
			return false;
		}

		return true;
	}

	uint64_t calc_view_values_stub(int localClientNum, void* a2) {
		auto result = cg_calc_view_values_hook.invoke<uint64_t>(localClientNum, a2);

		if (demo_camera_mode->current.integer == THEATER_CAMERA_FREECAM) {
			

			for (int i = 0; i < 3; i++) {
				game::refdef->origin[i] = freecam_pos[i];
				//refdef->viewOffset[i] = 0;
				//refdef->viewOffsetPrev[i] = 0;

				game::QuatToAxis(freecam_quat, game::refdef->axis);
			}
		}

		return result;
	}

	void r_update_lod_parms_stub(void* a1, void* a2, float a3) {

		if (demo_playback::is_playing() && demo_camera_mode->current.integer == THEATER_CAMERA_FREECAM) {
			game::refdef->origin[0] = freecam_pos[0];
			game::refdef->origin[1] = freecam_pos[1];
			game::refdef->origin[2] = freecam_pos[2];
		}

		r_update_lod_parms_hook.invoke(a1, a2, a3);
	}

	void set_immediate_mode_camera_pos(game::vec3_t pos)
	{
		freecam_pos[0] = pos[0];
		freecam_pos[1] = pos[1];
		freecam_pos[2] = pos[2];
	}

	void set_immediate_mode_camera_quat(game::vec4_t quat)
	{
		for (int i = 0; i < 4; i++) {
			freecam_quat[i] = quat[i];
		}
	}

	camera_mode get_current_mode()
	{
		return (camera_mode)demo_camera_mode->current.integer;
	}



	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			bg_is_thirdperson_hook.create(0x14013C360, bg_is_thirdperson_stub);
			cg_calc_view_values_hook.create(0x1401DC450, calc_view_values_stub);
			r_update_lod_parms_hook.create(0x01405D3CA0, r_update_lod_parms_stub);
			demo_camera_mode = game::Dvar_RegisterEnum("demo_camera", modes, 0, game::DVAR_FLAG_NONE, "");
		}
	};


}

REGISTER_COMPONENT(theater_camera::component)

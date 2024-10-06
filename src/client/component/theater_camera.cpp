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
		"dolly",
		(char*)0,
	};

	const game::dvar_t* demo_camera_mode;
	utils::hook::detour bg_is_thirdperson_hook;
	utils::hook::detour cg_calc_view_values_hook;
	utils::hook::detour cl_renderscene_hook;
	utils::hook::detour r_update_lod_parms_hook;
	utils::hook::detour cg_cinematic_camera_get_fov_hook;

	game::vec3_t freecam_pos;
	game::vec4_t freecam_quat;
	float freecam_fov = 90;

	std::optional<std::vector<camera_keyframe_t>> dolly_frames;


	bool bg_is_thirdperson_stub(game::playerState_s* state) {
		if (demo_playback::is_playing() == false) {
			return bg_is_thirdperson_hook.invoke<bool>(state);
		}

		if (demo_camera_mode->current.integer == THEATER_CAMERA_FIRST_PERSON) {
			return false;
		}

		return true;
	}

	std::tuple<std::optional<camera_keyframe_t>, std::optional<camera_keyframe_t>> find_lerp_markers(int time) {
		if (dolly_frames.has_value() == false) {
			return { std::nullopt, std::nullopt };
		}

		auto first = (*dolly_frames).at(0);

		if (time < first.frame_time) {
			return { first, std::nullopt };
		}
		
		for (int i = 0; i < (*dolly_frames).size() - 1; i++) {
			auto marker = (*dolly_frames).at(i);
			auto marker_next = (*dolly_frames).at(i + 1);


			if (marker.frame_time < time && marker_next.frame_time > time) {
				return { marker, marker_next };
			}
		}

		return { std::nullopt, std::nullopt };
	}

	camera_keyframe_t lerp_cameras(camera_keyframe_t a, camera_keyframe_t b, int current_time) {
		if (a.frame_time >= b.frame_time) {
			return a;
		}

		camera_keyframe_t result = a;

		float start = (float)a.frame_time;
		float end = (float)b.frame_time;
		float now = (float)current_time;
		float diff = end - start;
		float offset = current_time - start;

		float alpha = std::clamp(offset / diff, 0.0f, 1.0f);

		for (int i = 0; i < 3; i++) {
			result.camera.pos[i] = std::lerp(a.camera.pos[i], b.camera.pos[i], alpha);
		}

		game::QuatLerp(&a.camera.quat[0], &b.camera.quat[0], alpha, &result.camera.quat[0]);

		return result;
	}

	uint64_t calc_view_values_stub(int localClientNum, void* a2) {
		auto result = cg_calc_view_values_hook.invoke<uint64_t>(localClientNum, a2);


		if (demo_camera_mode->current.integer == THEATER_CAMERA_DOLLY) {
			auto player = demo_playback::get_current_demo_reader();
			if (player) {
				int time = (*player)->get_time();
				auto markers = find_lerp_markers(time);
				auto current_camera = std::get<0>(markers);

				if (current_camera) {
					auto marker = *current_camera;

					auto second = std::get<1>(markers);
					if (second) {
						marker = lerp_cameras(marker, *second, time);
					}

					set_camera_immediate_mode(marker.camera);
				}
			}
		}

		if (demo_camera_mode->current.integer == THEATER_CAMERA_FREECAM || demo_camera_mode->current.integer == THEATER_CAMERA_DOLLY) {
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

		if (demo_playback::is_playing() && demo_camera_mode->current.integer == THEATER_CAMERA_FREECAM || demo_camera_mode->current.integer == THEATER_CAMERA_DOLLY) {
			game::refdef->origin[0] = freecam_pos[0];
			game::refdef->origin[1] = freecam_pos[1];
			game::refdef->origin[2] = freecam_pos[2];
		}

		r_update_lod_parms_hook.invoke(a1, a2, a3);
	}


	bool cg_is_cinematic_camera_active_stub(int clientNum) {
		if (demo_playback::is_playing) {
			if (theater_camera::get_current_mode() > THEATER_CAMERA_THIRD_PERSON) {
				return true;
			}
		}

		return 0;
	}

	bool cinematic_camera_check_stub() {
		return 0;
	}

	float cg_cinematic_camera_get_fov_stub(int num, double result) {
		console::info("getting fov: %llx  %f   ->   %f\n", num, result, freecam_fov);
		return freecam_fov;
	}

	camera_mode get_current_mode()
	{
		return (camera_mode)demo_camera_mode->current.integer;
	}

	void set_camera_immediate_mode(camera_data_t camera)
	{
		freecam_pos[0] = camera.pos[0];
		freecam_pos[1] = camera.pos[1];
		freecam_pos[2] = camera.pos[2];

		for (int i = 0; i < 4; i++) {
			freecam_quat[i] = camera.quat[i];
		}

		freecam_fov = camera.fov;
	}

	void set_dolly_markers(std::vector<camera_keyframe_t> markers)
	{
		dolly_frames = markers;
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			bg_is_thirdperson_hook.create(game::BG_IsThirdPerson, bg_is_thirdperson_stub);
			cg_calc_view_values_hook.create(0x1401DC450, calc_view_values_stub);
			r_update_lod_parms_hook.create(0x01405D3CA0, r_update_lod_parms_stub);

			// Override cinematic camera check
			utils::hook::call(0x1401D5CF9, cg_is_cinematic_camera_active_stub);
			utils::hook::call(0x1401D5D04, cinematic_camera_check_stub);

			cg_cinematic_camera_get_fov_hook.create(0x14001BC10, cg_cinematic_camera_get_fov_stub);

			demo_camera_mode = game::Dvar_RegisterEnum("demo_camera", modes, 0, game::DVAR_FLAG_NONE, "");
		}
	};


}

REGISTER_COMPONENT(theater_camera::component)

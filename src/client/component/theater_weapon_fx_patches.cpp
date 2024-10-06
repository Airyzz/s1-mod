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
#include "scripting.hpp"

namespace theater_weapon_fx_patches
{
	namespace
	{
		utils::hook::detour cg_prep_fire_params_hook;

		bool can_use_stored_orientation;
		game::orientation_t orientation_buffer;
		uint64_t cg_prep_fire_params_stub(uint64_t localClientNum, uint64_t entity, uint64_t weapon, uint64_t a4, game::scr_string_t bone_name, uint64_t a6, bool isPlayer, float a8, game::orientation_t* orientation, float* spread, game::vec3_t* position, game::vec3_t* a12, bool* a13, uint64_t a14) {

			auto result = cg_prep_fire_params_hook.invoke<uint64_t>(localClientNum, entity, weapon, a4, bone_name, a6, isPlayer, a8, orientation, spread, position, a12, a13, a14);

			if (isPlayer == false) {
				return result;
			}

			auto state = game::SV_GetPlayerstateForClientNum(localClientNum);
			if (game::BG_IsThirdPerson(state)) {
				game::orientation_t orient;

				// It would be ideal to do a proper trace to get the most accurate direction, but this is good enough for me
				game::BG_GetPlayerViewDirection(0, (void*)0x01414C1700, &orient.axis1, &orient.axis2, &orient.axis3);

				orient.origin[0] = orientation_buffer.origin[0];
				orient.origin[1] = orientation_buffer.origin[1];
				orient.origin[2] = orientation_buffer.origin[2];

				memcpy(orientation, &orient, sizeof(game::orientation_t));

				*a13 = 1;
			}

			return result;
		}
	}

	bool bg_is_third_person_stub(game::playerState_s* state) {
		return false;
	}

	bool fx_get_bone_orientation_stub(int localClientNum, int dobjIndex, int boneIndex, game::orientation_t* orientation) {
		bool result = game::FX_GetBoneOrientation(localClientNum, dobjIndex, boneIndex, orientation);

		if (localClientNum != 0) {
			return result;
		}

		auto state = game::SV_GetPlayerstateForClientNum(localClientNum);
		if (game::BG_IsThirdPerson(state) && can_use_stored_orientation) {
			memcpy(orientation, &orientation_buffer, sizeof(game::orientation_t));
			return true;
		}

		return result;
	}

	// The game is not prepared to calculate bone positions during CG_PrepFireParams, and trying to do so causes stuttering players
	// Instead we store the bone position on each frame, and reuse that
	void loop() {
		auto dobj = game::Com_GetClientDobj(0);

		if (!dobj) {
			return;
		}

		auto name = game::SL_FindString("tag_flash");

		if (name) {
			uint8_t index = -2;
			if (game::DobjGetBoneIndex(dobj, name, &index)) {
				can_use_stored_orientation = game::FX_GetBoneOrientation(0, 0, (int)index, &orientation_buffer);
			}
			else {
				console::info("Failed to find tag %d\n", index);
			}
		}
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			utils::hook::call(0x1401EBEF3, fx_get_bone_orientation_stub);

			// Force game to use first person logic for bullet params, to avoid the game applying extra transform
			utils::hook::call(0x1401EBD18, bg_is_third_person_stub);

			cg_prep_fire_params_hook.create(0x01401EBBA0, cg_prep_fire_params_stub);

			scripting::on_init([]() {
				scheduler::loop(loop, scheduler::main);
			});
		}
	};
}

REGISTER_COMPONENT(theater_weapon_fx_patches::component)

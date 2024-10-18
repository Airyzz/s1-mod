#include <std_include.hpp>
#include "loader/component_loader.hpp"
#include "game/game.hpp"

#include "command.hpp"
#include "dvars.hpp"
#include "demo_playback.h"
#include "theater_camera.h"
#include "console.hpp"
#include "theater_autofocus.h"
#include "scheduler.hpp"

namespace theater_autofocus
{

	int selected_player_index = 0;
	int selected_bone_index = 0;
	game::dvar_t* r_autofocus_enabled;
	game::dvar_t* focus_distance;

	bool theater_autofocus::is_enabled()
	{
		return r_autofocus_enabled->current.enabled;
	}

	float distance(float* a, float* b) {
		float dx = a[0] - b[0];
		float dy = a[1] - b[1];
		float dz = a[2] - b[2];

		float result = sqrtf((dx * dx) + (dy * dy) + (dz * dz));

		console::info("[autofocus] (%f, %f, %f) (%f, %f, %f)  --> %f\n", a[0], a[1], a[2], b[0], b[1], b[2], result);

		return result;
	}

	void loop() {
		if (!demo_playback::is_playing()) {
			return;
		}

		void* dobj = game::Com_GetClientDobj(selected_player_index);
		if (dobj == nullptr) {
			return;
		}

		if (r_autofocus_enabled->current.enabled == false) {
			return;
		}

		game::vec3_t pos;
		theater_camera::get_current_position(&pos);

		game::orientation_t orient;
		game::FX_GetBoneOrientation(0, selected_player_index, selected_bone_index, &orient);

		float dist = distance((float*)&pos, (float*)&orient.origin);

		auto focus = game::Dvar_FindVar("r_dof_physical_focusDistance");
		if (!focus) {
			console::warn("Unable to find dvar 'r_dof_physical_focusDistance'\n");
		}

		game::Dvar_SetFloat(focus, dist);
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			r_autofocus_enabled = game::Dvar_RegisterBool("r_autofocus_enabled", false, game::DVAR_FLAG_NONE, "");

			command::add("autofocus_next", [](const command::params& params)
			{
				selected_player_index += 1;

				auto* const sv_maxclients = game::Dvar_FindVar("sv_maxclients");
				if (sv_maxclients) {
					selected_player_index = selected_player_index % sv_maxclients->current.integer;
				}

				console::info("Selected player: %d\n", selected_player_index);
			});

			command::add("autofocus_bone", [](const command::params& params)
			{
				if (params.size() == 2)
				{
					auto bone_name = params.get(1);
					auto dobj = game::Com_GetClientDobj(selected_player_index);
					if (dobj == nullptr) {
						return;
					}

					auto token = game::SL_FindString(bone_name);
					if (token) {
						console::info("Looking for bone: %s (d)\n", bone_name, token);

						uint8_t index = -2;
						if (game::DobjGetBoneIndex(dobj, token, &index)) {
							console::info("Found bone!\n");
							selected_bone_index = index;
						}
						else {
							console::info("Couldnt find bone '%s'\n", bone_name);
						}
					}
				}
			});

			scheduler::loop(loop, scheduler::main);
		}
	};
}

REGISTER_COMPONENT(theater_autofocus::component)

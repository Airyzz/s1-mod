#pragma once
#include "game/structs.hpp"

namespace demo_data
{
	enum DemoPacketType {
		SERVER_MESSAGE = 0x1,
		CLIENT_DATA = 0x2,
	};

	struct demo_client_command_t {
		int serverTime;
		int buttons;
		int angles_0;
		int angles_1;
		int angles_2;
	};

	struct demo_client_data_t {

		demo_client_command_t cmd;
		demo_client_command_t prevCmd;

		int commandTime;
		int pm_type;
		int pm_time;
		int pm_flags;
		int otherflags;
		int serverTime;
		game::vec3_t origin;
		game::vec3_t velocity;
		game::vec3_t DeltaAngles;
		game::vec3_t viewAngles;
	};
}
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
		int predictedDataServerTime;
		game::vec3_t origin;
		game::vec3_t velocity;
		game::vec3_t viewAngles;
		int bobCycle;
		int movementDir;
	};
}
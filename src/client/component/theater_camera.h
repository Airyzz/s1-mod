#pragma once
#pragma once

#include <optional>
#include <string>

enum camera_mode {
	THEATER_CAMERA_FIRST_PERSON,
	THEATER_CAMERA_THIRD_PERSON,
	THEATER_CAMERA_FREECAM,
};

namespace theater_camera
{
	camera_mode get_current_mode();

	void set_immediate_mode_camera_pos(game::vec3_t pos);
	void set_immediate_mode_camera_quat(game::vec4_t quat);
}
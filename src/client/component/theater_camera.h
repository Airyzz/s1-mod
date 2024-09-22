#pragma once
#pragma once

#include <optional>
#include <string>

enum camera_mode {
	THEATER_CAMERA_FIRST_PERSON,
	THEATER_CAMERA_THIRD_PERSON,
	THEATER_CAMERA_FREECAM,
	THEATER_CAMERA_DOLLY,
};

struct camera_data_t {
	game::vec3_t pos;
	game::vec4_t quat;
	float fov;
	bool use_dof;
	float dof_focal_length;
	float dof_fstop;
	float dof_focal_distance;
};

struct camera_keyframe_t {
	int frame_time;
	camera_data_t camera;
};

namespace theater_camera
{
	camera_mode get_current_mode();

	void set_camera_immediate_mode(camera_data_t camera);

	void set_dolly_markers(std::vector<camera_keyframe_t> markers);
}
#include <std_include.hpp>
#include "loader/component_loader.hpp"
#include "game/game.hpp"
#include <game/demonware/byte_buffer.hpp>
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
#include <fcntl.h>
#include <utils/hexdump.hpp>

namespace theater_blender
{

	enum message_type {
		BLENDER_MSG_HELLO = 0,
		BLENDER_MSG_SNAPSHOT = 1,
		BLENDER_MSG_SET_CAMERA_IMMEDIATE_MODE = 2,
		BLENDER_MSG_BAKE_DATA = 3,
	};

	namespace
	{
		SOCKET server_socket;
		sockaddr_in server_address;


		std::optional<sockaddr> client;
		socklen_t client_len;

		float make_big_endian(float value)
		{
			int val = *reinterpret_cast<int*>(&value);

			val = (val << 24) |
				((val << 8) & 0x00ff0000) |
				((val >> 8) & 0x0000ff00) |
				((val >> 24) & 0x000000ff);

			return  *reinterpret_cast<float*>(&val);
		}

		int make_big_endian_int(int val)
		{
			return  (val << 24) |
				((val << 8) & 0x00ff0000) |
				((val >> 8) & 0x0000ff00) |
				((val >> 24) & 0x000000ff);
		}

		void quaternion_mult(game::vec4_t in1, game::vec4_t in2, game::vec4_t out)
		{
			out[0] = (((in1[0] * in2[3]) + (in1[3] * in2[0])) + (in2[1] * in1[2])) - (in2[2] * in1[1]);
			out[1] = (((in1[1] * in2[3]) - (in1[2] * in2[0])) + (in1[3] * in2[1])) + (in2[2] * in1[0]);
			out[2] = (((in1[1] * in2[0]) + (in1[2] * in2[3])) - (in2[1] * in1[0])) + (in1[3] * in2[2]);
			out[3] = (((in1[3] * in2[3]) - (in1[0] * in2[0])) - (in2[1] * in1[1])) - (in2[2] * in1[2]);
		}

		void blender_quat_to_cod_quat(game::vec4_t quat, game::vec4_t out)
		{

			game::vec4_t rotation;
			rotation[0] = -0.5;
			rotation[1] = 0.5;
			rotation[2] = 0.5;
			rotation[3] = 0.5;

			game::vec4_t rotated_rot;

			quaternion_mult(quat, rotation, rotated_rot);

			float x = rotated_rot[0];
			float y = rotated_rot[1];
			float z = rotated_rot[2];
			float w = rotated_rot[3];

			out[0] = -w;
			out[1] = x;
			out[2] = y;
			out[3] = -z;
		}

		void blender_coord_to_cod_coord(game::vec3_t in, game::vec3_t out) {
			out[0] = in[0] * 39.37008;
			out[1] = in[1] * 39.37008;
			out[2] = in[2] * 39.37008;
		}

		camera_data_t read_camera_data(demonware::byte_buffer *buffer) {
			camera_data_t camera;

			for (int i = 0; i < 3; i++) {
				float x;
				buffer->read_float(&x);
				camera.pos[i] = make_big_endian(x);
			}

			for (int i = 0; i < 4; i++) {
				/// wxyz
				float x;
				buffer->read_float(&x);
				camera.quat[i] = make_big_endian(x);
			}

			buffer->read_float(&camera.fov);
			camera.fov = make_big_endian(camera.fov);
			buffer->read_bool(&camera.use_dof);

			if (camera.use_dof) {
				buffer->read_float(&camera.dof_focal_length);
				buffer->read_float(&camera.dof_fstop);
				buffer->read_float(&camera.dof_focal_distance);

				camera.dof_focal_length = make_big_endian(camera.dof_focal_length);
				camera.dof_fstop = make_big_endian(camera.dof_fstop);
				camera.dof_focal_distance = make_big_endian(camera.dof_focal_distance);
			}

			game::vec3_t converted_pos;
			blender_coord_to_cod_coord(&camera.pos[0], converted_pos);

			game::vec4_t converted_rot;
			blender_quat_to_cod_quat(&camera.quat[0], converted_rot);

			for (int i = 0; i < 3; i++) {
				camera.pos[i] = converted_pos[i];
			}

			for (int i = 0; i < 4; i++) {
				camera.quat[i] = converted_rot[i];
			}

			return camera;
		}

		void handle_immediate_mode(demonware::byte_buffer buffer) {
			auto cam = read_camera_data(&buffer);
			if (demo_playback::is_playing()) {
				if (theater_camera::get_current_mode() == THEATER_CAMERA_FREECAM) {
					theater_camera::set_camera_immediate_mode(cam);
				}
			}
		}

		void handle_bake_data(demonware::byte_buffer buffer) {
			int num_frames = 0;
			buffer.read_int32(&num_frames);
			num_frames = make_big_endian_int(num_frames);

			std::vector<camera_keyframe_t> frames;
			
			for (int i = 0; i < num_frames; i++) {

				camera_keyframe_t frame;

				int frame_time;
				buffer.read_int32(&frame_time);
				frame_time = make_big_endian_int(frame_time);
				frame_time *= 20;

				frame.frame_time = frame_time;

				frame.camera = read_camera_data(&buffer);
				console::info("Got baked camera frame: %f     %f    %f\n", frame.camera.pos[0], frame.camera.pos[1], frame.camera.pos[2]);
				frames.push_back(frame);
			}

			theater_camera::set_dolly_markers(frames);
		}

		void send_snapshot() {

			if (demo_playback::is_playing() && client.has_value()) {

				int time = (*demo_playback::get_current_demo_reader())->get_time();
				if (time <= 0) {
					return;
				}

				auto cl = *client;
				demonware::byte_buffer buffer;
				buffer.set_use_data_types(false);
				buffer.write_byte(BLENDER_MSG_SNAPSHOT);

				time /= 20;
				time = make_big_endian_int(time);

				buffer.write_uint32(time);
				buffer.write_float(make_big_endian(0.0f));

				auto data = buffer.get_buffer();

				sendto(server_socket, data.data(), data.length(), 0, &cl, client_len);
			}

		}

		void on_message(std::string data) {
			demonware::byte_buffer buffer(data);
			buffer.set_use_data_types(false);

			uint8_t type;
			buffer.read_byte(&type);

			switch (type)
			{
			case BLENDER_MSG_SET_CAMERA_IMMEDIATE_MODE:
				handle_immediate_mode(buffer);
				break;
			case BLENDER_MSG_BAKE_DATA:
				handle_bake_data(buffer);
				break;
			default:
				console::info("Received data on blender server!\n");
				utils::hexdump::dump_hex_to_stdout(data);
				return;
			}
		}

		int last_snapshot_frame = 0;
		char buffer[USHRT_MAX];

		void loop() {
			int frame = *game::com_frameTime;

			memset(buffer, 0, sizeof(buffer));

			sockaddr from;
			memset(&from, 0, sizeof(from));
			socklen_t len = sizeof(from);

			if (frame - last_snapshot_frame > 10) {
				last_snapshot_frame = frame;
				if (client.has_value() && demo_playback::is_paused() == false) {
					send_snapshot();
				}
			}

			while (true) {
				auto n = recvfrom(server_socket, buffer, sizeof(buffer), 0, &from, &len);

				if (n > 0) {
					auto data = std::string(buffer, n);

					client = from;
					client_len = len;

					on_message(data);
				}
				else {
					int error = WSAGetLastError();
					if (error != 10035) {
						console::info("No data received :(  %d\n", error);
					}
					return;
				}
			}
		}
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			server_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
			if (server_socket < 0) {
				console::info("Failed to create blender server socket\n");
				return;
			}

			u_long mode = 1;  // 1 to enable non-blocking socket
			ioctlsocket(server_socket, FIONBIO, &mode);

			memset(&server_address, 0, sizeof(server_address));

			server_address.sin_family = AF_INET;
			server_address.sin_addr.S_un.S_addr = INADDR_ANY;
			server_address.sin_port = htons(25565);

			if (bind(server_socket, (const struct sockaddr*)&server_address, sizeof(server_address)) < 0) {
				console::info("Failed to bind address\n");
				return;
			}

			scheduler::loop(loop, scheduler::main);
		}
	};


}

REGISTER_COMPONENT(theater_blender::component)

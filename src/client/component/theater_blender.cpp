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

		float make_big_endian(float value)
		{
			int val = *reinterpret_cast<int*>(&value);

			val = (val << 24) |
				((val << 8) & 0x00ff0000) |
				((val >> 8) & 0x0000ff00) |
				((val >> 24) & 0x000000ff);

			return  *reinterpret_cast<float*>(&val);
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


		void handle_immediate_mode(demonware::byte_buffer buffer) {
			game::vec3_t pos;
			game::vec4_t quat;
			float fov;

			bool useDof;

			float focalLength;
			float fstop;
			float focalDistance;

			for (int i = 0; i < 3; i++) {
				buffer.read_float(&pos[i]);
				pos[i] = make_big_endian(pos[i]);
			}

			for (int i = 0; i < 4; i++) {
				/// wxyz
				buffer.read_float(&quat[i]);
				quat[i] = make_big_endian(quat[i]);
			}

			buffer.read_float(&fov);
			fov = make_big_endian(fov);

			buffer.read_bool(&useDof);

			if (useDof) {
				buffer.read_float(&focalLength);
				buffer.read_float(&fstop);
				buffer.read_float(&focalDistance);

				focalLength = make_big_endian(focalLength);
				fstop = make_big_endian(fstop);
				focalDistance = make_big_endian(focalDistance);
			}

			game::vec3_t converted_pos;
			blender_coord_to_cod_coord(pos, converted_pos);

			game::vec4_t converted_rot;
			blender_quat_to_cod_quat(quat, converted_rot);

			theater_camera::set_immediate_mode_camera_pos(converted_pos);
			theater_camera::set_immediate_mode_camera_quat(converted_rot);
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
			default:
				console::info("Received data on blender server!\n");
				utils::hexdump::dump_hex_to_stdout(data);
				return;
			}
		}

		void loop() {
			char buffer[USHRT_MAX];
			memset(buffer, 0, sizeof(buffer));

			sockaddr from;
			memset(&from, 0, sizeof(from));
			socklen_t len = sizeof(from);

			while (true) {
				auto n = recvfrom(server_socket, buffer, sizeof(buffer), 0, &from, &len);

				if (n > 0) {
					auto data = std::string(buffer, n);
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

			scheduler::loop(loop);
		}
	};


}

REGISTER_COMPONENT(theater_blender::component)

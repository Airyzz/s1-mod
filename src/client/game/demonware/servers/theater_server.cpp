#include <std_include.hpp>
#include "theater_server.hpp"
#include "../../../component/console.hpp"
#include "../byte_buffer.hpp"
#include <utils/hexdump.hpp>
#include "../../game.hpp"

namespace demonware
{

	void theater_server::handle_get_info(const endpoint_data& endpoint, const std::string& packet) {
		auto args = *game::sv_cmd_args;

		byte_buffer buffer;
		buffer.set_use_data_types(false);

		buffer.write_int32(-1);

		console::info("Handling getInfo request");

		if (args.argc[args.nesting] >= 2) {
			console::info("Got arg: %s\n", args.argv[args.nesting][0]);
			console::info("Got arg: %s\n", args.argv[args.nesting][1]);
		}

		std::string challenge = std::string(args.argv[args.nesting][1]);

		std::string response = "infoResponse\n";
		response = response + "\\challenge" + "\\" + challenge;
		response += "\\isPrivate\\0";
		response += "\\playmode\\2";
		response += "\\hostname\\Demo_Server";
		response += "\\gamename\\S1";
		response += "\\sv_maxclients\\18";
		response += "\\gametype\\dm";
		response += "\\sv_motd\\Loading_Demo";
		response += "\\xuid\\110000139E3FC15";
		response += "\\mapname\\mp_refraction";
		response += "\\clients\\0";
		response += "\\bots\\0";
		response += "\\protocol\\1";
		response += "\\sv_running\\1";
		response += "\\dedicated\\1";
		response += "\\shortversion\\0.0.1";


		
		buffer.write_string(response);

		console::info("responding to getInfo = %s\n", utils::hexdump::dump_hex(response.data()).data());

		this->send(endpoint, buffer.get_buffer());
	}

	void theater_server::handle_connect(const endpoint_data& endpoint, const std::string& packet) {

	}

	void theater_server::handle_get_challenge(const endpoint_data& endpoint, const std::string& packet) {
		byte_buffer buffer;
		buffer.set_use_data_types(false);

		buffer.write_int32(-1);
		buffer.write_string("challengeResponse");
		buffer.write_int32(rand());

		auto buf = buffer.get_buffer();
		console::info("Reponding to challenge: %s", utils::hexdump::dump_hex(buf).data());

		this->send(endpoint, buffer.get_buffer());
	}

	void theater_server::handle_connectionless_packet(const endpoint_data& endpoint, const std::string& packet) {
		byte_buffer buffer(packet);
		buffer.set_use_data_types(false);

		int tick;
		std::string str;


		buffer.read_int32(&tick);
		buffer.read_string(&str);

		console::info("Received connectionless packet: %s\n", str.data());

		game::SV_Cmd_TokenizeString(str.c_str());

		auto args = *game::sv_cmd_args;
		if (args.argc[args.nesting] <= 0) {
			console::info("No Args!\n");
		}
		else {
			auto arg = *args.argv[args.nesting];
			console::info("Got arg: %s\n", arg);

			if (strcmp("getInfo", arg) == 0) {
				this->handle_get_info(endpoint, packet);
			}
			else if (strcmp("getchallenge", arg) == 0) {
				this->handle_get_challenge(endpoint, packet);
			}
			else if (strcmp("connect", arg) == 0) {
				this->handle_connect(endpoint, packet);
			}
		}

		game::SV_Cmd_EndTokenizedString();
	}

	void theater_server::handle(const endpoint_data& endpoint, const std::string& packet)
	{
		console::info("demo dw_server handling packet: %s\n", utils::hexdump::dump_hex(packet).data());

		byte_buffer buffer(packet);
		buffer.set_use_data_types(false);

		int tick;
		buffer.read_int32(&tick);

		if (tick == -1) {
			handle_connectionless_packet(endpoint, packet);
		}
	}
}

#include <std_include.hpp>
#include "theater_server.hpp"
#include "../../../component/console.hpp"
#include "../byte_buffer.hpp"
#include <utils/hexdump.hpp>
#include "../../game.hpp"
#include "component/scheduler.hpp"
#include "component/timing.h"
#include "component/demo_playback.h";

namespace demonware
{

	static theater::theater_server* instance;

	namespace theater {


		void theater::theater_server::handle_get_info(const endpoint_data& endpoint, const std::string& packet) {
			auto args = *game::sv_cmd_args;

			auto reader = demo_playback::get_current_demo_reader();

			if (!*reader) {
				console::warn("No demo reader was ready to use to get info");
				return;
			}


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
			response += "\\gametype\\" + (*reader)->get_mode();
			response += "\\sv_motd\\Loading Demo";
			response += "\\xuid\\110000139E3FC15";
			response += "\\mapname\\" + (*reader)->get_map_name();
			response += "\\clients\\1";
			response += "\\bots\\0";
			response += "\\protocol\\61";
			response += "\\sv_running\\1";
			response += "\\dedicated\\0";
			response += "\\shortversion\\1.22";


			buffer.write_string(response);

			console::info("responding to getInfo = %s\n", utils::hexdump::dump_hex(response.data()).data());

			this->send(endpoint, buffer.get_buffer());
		}


		int firstMessageTime = -1;
		int timeOffset = 0;

		int serverTime = 0;
		int msecsToProcess = 0;
		int prevFrameTime = 0;

		std::optional<demonware::udp_server::endpoint_data> client_endpoint;

		bool is_running() {
			return client_endpoint && demo_playback::get_current_demo_reader()->has_value();
		}

		void theater_server::server_frame() {
			if (!is_running()) {
				return;
			}


			auto reader = demo_playback::get_current_demo_reader();
			
			while (true) {
				auto msg = (*reader)->dequeue_server_message();
				if (!msg) {
					return;
				}

				this->send(*client_endpoint, *msg);
			}
			
			console::info("Theater server time: [%d]\n", serverTime);
		}


		theater_server::theater_server(std::string name) : udp_server(name)
		{
			console::info("THEATER SERVER CREATED!!!");
			instance = this;
		}

		void theater_server::frame() {
			udp_server::frame();

			if (!is_running()) {
				return;
			}

			int time = *game::com_frameTime;
			int delta = time - prevFrameTime;

			delta = game::Com_TimeScaleMsec(delta);

			prevFrameTime = time;

			msecsToProcess += delta;

			const int rate = 50;

			while (msecsToProcess > rate) {
				msecsToProcess -= rate;
				serverTime += rate;
				this->server_frame();
			}
		}


		void theater_server::stop()
		{
			client_endpoint = std::nullopt;
		}

		bool theater_server::connected()
		{
			return client_endpoint != std::nullopt;
		}

		void theater_server::handle_connect(const endpoint_data& endpoint, const std::string& packet) {
			client_endpoint = endpoint;
			prevFrameTime = *game::com_frameTime;
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

			console::info("Received connectionless packet: %s CURRENT STATUS: %d\n", str.data(), *game::connectionStatus);

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
			byte_buffer buffer(packet);
			buffer.set_use_data_types(false);

			int tick;
			buffer.read_int32(&tick);

			if (tick == -1) {
				handle_connectionless_packet(endpoint, packet);
			}
		}
	}
}
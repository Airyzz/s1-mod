#include <std_include.hpp>
#include "theater_server.hpp"
#include "../../../component/console.hpp"
#include "../byte_buffer.hpp"
#include <utils/hexdump.hpp>
#include "../../game.hpp"
#include "component/scheduler.hpp"

namespace demonware
{
	std::mutex m;

	class DemoReader {

	private:
		std::ifstream stream;

	public:
		std::string readNextMessage() {
			int time;

			int size;
			int splitSize;
			m.lock();
			
			stream.read(reinterpret_cast<char*>(&time), sizeof(time));
			stream.read(reinterpret_cast<char*>(&size), sizeof(size));

			std::string data;
			data.resize(size);

			stream.read(&data[0], size);


			stream.read(reinterpret_cast<char*>(&splitSize), sizeof(splitSize));
			std::string splitData;
			splitData.resize(splitSize);

			stream.read(&splitData[0], splitSize);

			m.unlock();

			std::string result = data + splitData;

			//console::info("Demo Server Read:\n");
			//utils::hexdump::dump_hex_to_stdout(result);

			return result;
		}

		void init() {
			stream = std::ifstream(
				"demos/war_mp_solar_9_14_2024_17_53.demo", std::ios::binary | std::ifstream::in);
		}

		int peekNextMessageTime() {
			int time;

			m.lock();
			stream.read(reinterpret_cast<char*>(&time), sizeof(time));
			stream.seekg(-sizeof(time), std::ios_base::cur);
			m.unlock();

			return time;
		}

		void close() {
			stream.close();
		}
	};

	std::optional<DemoReader> reader;
	demonware::udp_server::endpoint_data lastEndpoint;

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
		response += "\\gametype\\war";
		response += "\\sv_motd\\Loading Demo";
		response += "\\xuid\\110000139E3FC15";
		response += "\\mapname\\mp_solar";
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

	int lastProcessedMessageTime = 0;
	int firstMessageTime = -1;
	int timeOffset = 0;

	int serverTime = 0;
	int prevFrame = 0;
	int msecsToProcess = 0;
	bool running = false;

	void theater_server::server_frame() {

		//console::info("Theater server doing frame! [%d] CURRENT STATUS: %d\n", serverTime, *game::connectionStatus);

		if (reader) {
			for (int i = 0; i < 1; i++) {

				int msgTime = reader->peekNextMessageTime();
				if (msgTime == 0xffffffff) {
					auto msg = reader->readNextMessage();
					continue;
				}

				if (firstMessageTime == -1) {
					firstMessageTime = msgTime;
					timeOffset = serverTime - firstMessageTime;
				}


				int realMessageTime = msgTime + timeOffset;

				if (realMessageTime > serverTime) {
					break;
				}

				if (realMessageTime <= serverTime) {
					console::info("Theater server sending message for time: %d\n", msgTime);
					this->send(lastEndpoint, reader->readNextMessage());
					lastProcessedMessageTime = realMessageTime;
				}
			}
		}
	}

	void theater_server::frame() {
		udp_server::frame();

		if (!running) {
			return;
		}


		auto time = *game::com_frameTime;

		console::info("THEATER_SERVER FRAME!! %d\n", time);
		msecsToProcess += time - prevFrame;

		const int rate = 50;

		while (msecsToProcess > rate) {
			msecsToProcess -= rate;
			serverTime += rate;
			this->server_frame();
		}

		prevFrame = time;
	}

	void theater_server::handle_connect(const endpoint_data& endpoint, const std::string& packet) {
		reader = DemoReader();
		reader->init();

		serverTime = 0;
		msecsToProcess = 0;
		lastProcessedMessageTime = 0;
		running = true;
		prevFrame = *game::com_frameTime;

		

		auto msg = reader->readNextMessage();
		this->send(endpoint, msg);

		lastEndpoint = endpoint;
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

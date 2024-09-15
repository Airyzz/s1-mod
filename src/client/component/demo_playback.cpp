#include <std_include.hpp>
#include "loader/component_loader.hpp"
#include "game/game.hpp"

#include "command.hpp"
#include "console.hpp"
#include <utils/io.hpp>
#include "game/demonware/servers/theater_server.hpp"
#include "demo_playback.h"
#include "scheduler.hpp"

namespace demo_playback
{
	demo_reader::demo_reader(std::string filepath)
	{
		time = 0;
		firstMessageTime = -1;
		offsetTime = 0;

		stream = std::ifstream(
			filepath, std::ios::binary | std::ifstream::in);

		int mapNameLength;
		stream.read(reinterpret_cast<char*>(&mapNameLength), sizeof(mapNameLength));

		map.resize(mapNameLength);
		stream.read(&map[0], mapNameLength);

		int modeLength;
		stream.read(reinterpret_cast<char*>(&modeLength), sizeof(modeLength));

		mode.resize(modeLength);
		stream.read(&mode[0], modeLength);

		console::info("Demo reader created: %s  %s\n", map.data(), mode.data());

		memset(&last_client_data, 0, sizeof(demo_data::demo_client_data_t));
	}

	std::optional<demo_data::demo_client_data_t> demo_reader::get_current_client_data()
	{
		return last_client_data;
	}

	std::optional<std::string> demo_reader::dequeue_server_message()
	{
		if (server_queue.empty()) {
			return std::nullopt;
		}

		auto msg = server_queue.front();
		server_queue.pop_front();
		
		return msg;
	}

	std::string demo_reader::get_map_name()
	{
		return map;
	}

	std::string demo_reader::get_mode()
	{
		return mode;
	}

	int demo_reader::peek_next_message_time()
	{
		int time;
		stream.read(reinterpret_cast<char*>(&time), sizeof(time));
		stream.seekg(-sizeof(time), std::ios_base::cur);
		return time;
	}

	void demo_reader::read_server_message()
	{
		int size;
		std::string data;
		stream.read(reinterpret_cast<char*>(&size), sizeof(size));

		data.resize(size);
		stream.read(&data[0], size);
		
		
		int splitSize;
		std::string splitData;
		stream.read(reinterpret_cast<char*>(&splitSize), sizeof(splitSize));

		splitData.resize(splitSize);

		stream.read(&splitData[0], splitSize);

		std::string result = data + splitData;

		server_queue.push_back(result);
	}

	void demo_reader::read_client_data()
	{
		int size;
		stream.read(reinterpret_cast<char*>(&size), sizeof(size));

		if (size != sizeof(demo_data::demo_client_data_t)) {
			console::warn("Invalid size of demo client data! skipping %d bytes", size);
			stream.seekg(size, std::ios_base::cur);
			return;
		}
		else {
			demo_data::demo_client_data_t data;
			stream.read((char*)&data, sizeof(data));
			last_client_data = data;
		}
	}

	void demo_reader::read_message()
	{
		console::info("Demo reader reading message\n");

		int time;
		stream.read(reinterpret_cast<char*>(&time), sizeof(time));

		int type;
		stream.read(reinterpret_cast<char*>(&type), sizeof(type));

		if (type == demo_data::DemoPacketType::SERVER_MESSAGE) {
			read_server_message();
		}
		else if (type == demo_data::DemoPacketType::CLIENT_DATA) {
			read_client_data();
		}
	}

	void demo_reader::read_frame(int ms)
	{
		time += ms;

		for (int i = 0; i < 10; i++) {
			int msg_time = peek_next_message_time();

			if (msg_time == 0xffffffff) {
				read_message();
				continue;
			}

			if (firstMessageTime == -1) {
				firstMessageTime = msg_time;
				offsetTime = time - msg_time;
			}


			int adjusted_msg_time = msg_time + offsetTime;
			console::info("[%d]ms msg_time: %d   offset: %d    time: %d  adjusted_time: %d\n", ms, msg_time, offsetTime, time, adjusted_msg_time);

			if (adjusted_msg_time > time) {
				break;
			}

			read_message();
		}
	}

	std::optional<demo_reader> current_reader;

	std::optional<demo_reader>* get_current_demo_reader()
	{
		return &current_reader;
	}

	int prevFrameTime = 0;

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			command::add("demo_play", [](const command::params& params)
			{
				if (params.size() == 2) {
					auto file = params.get(1);
					console::info("Playing demo: %s\n", file);
					auto filepath = "demos/" + std::string(file);

					if (utils::io::file_exists(filepath)) {
						current_reader = demo_reader(filepath);
						command::execute("connect demo");
					}
					else {
						console::warn("Demo file does not exist: %s\n", filepath);
					}
				}
			});

			scheduler::loop([] {
				if (current_reader && demonware::theater::instance->connected()) {
					int time = *game::com_frameTime;
					int delta = time - prevFrameTime;
					prevFrameTime = time;
					delta = game::Com_TimeScaleMsec(delta);

					current_reader->read_frame(delta);
				}
			});
		}
	};
}

REGISTER_COMPONENT(demo_playback::component)
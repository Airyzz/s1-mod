#include <std_include.hpp>
#include "loader/component_loader.hpp"
#include "game/game.hpp"

#include "command.hpp"
#include "console.hpp"
#include <utils/io.hpp>
#include "game/demonware/servers/theater_server.hpp"
#include "demo_playback.h"
#include "scheduler.hpp"
#include <utils/hook.hpp>
#include "scripting.hpp"

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

		memset(&frames, 0, sizeof(frames));
	}

	std::optional<demo_data::demo_client_data_t> demo_reader::get_client_data_for_time(int time)
	{
		console::info("attempting to read client data for frame: %d\n", time);

		auto frame = frames[time % 256];

		console::info("Found frame: %d\n", frame.predictedDataServerTime);
		console::info("----\n");

		return frame;
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
			
			frames[data.predictedDataServerTime % 256] = data;
		}
	}

	void demo_reader::read_message()
	{

		int time;
		stream.read(reinterpret_cast<char*>(&time), sizeof(time));

		console::info("Demo reader reading message: %d ", time);

		int type;
		stream.read(reinterpret_cast<char*>(&type), sizeof(type));

		if (type == demo_data::DemoPacketType::SERVER_MESSAGE) {
			console::info("SERVER_MESSAGE\n ");
			read_server_message();
		}
		else if (type == demo_data::DemoPacketType::CLIENT_DATA) {
			console::info("CLIENT_DATA\n ");
			read_client_data();
		}
	}

	void demo_reader::read_frame(int ms)
	{
		time += ms;

		int file_offset = stream.tellg();

		for (int i = 0; i < 20; i++) {
			int msg_time = peek_next_message_time();

			if (msg_time == 0xffffffff) {
				read_message();
				continue;
			}

			if (msg_time > 0 && firstMessageTime <= 0) {
				firstMessageTime = msg_time;
				time = msg_time;
			}

			if (msg_time > time) {
				break;
			}

			read_message();
		}
	}

	void demo_reader::close()
	{
		stream.close();
	}



	std::optional<demo_reader> current_reader;

	bool is_playing()
	{
		return current_reader.has_value();
	}

	std::optional<demo_reader>* get_current_demo_reader()
	{
		return &current_reader;
	}

	int prevFrameTime = 0;

	utils::hook::detour cg_draw_active_frame_hook;
	int cg_drawactiveframe_stub(int localClientNum, int serverTime, int demoType, void* cubemapShot, void* cubemapSize, void* renderScreen, void* idk) {
		int demo = demoType;

		if (is_playing()) {
			demo = 1;
		}

		return cg_draw_active_frame_hook.invoke<int>(localClientNum, serverTime, demo, cubemapShot, cubemapSize, renderScreen, idk);
	}

	utils::hook::detour cl_get_predicted_player_information_for_server_time_hook;
	int64_t cl_get_predicted_player_information_for_server_time_stub(game::mp::clientActive_t* cl, int serverTime, game::mp::playerstate* to) {
		auto result = cl_get_predicted_player_information_for_server_time_hook.invoke<int64_t>(cl, serverTime, to);

		console::info("Getting predicted info for server time %d   %llx   original result: %d\n", serverTime, to, result);

		auto reader = demo_playback::get_current_demo_reader();
		if (reader->has_value()) {
			auto val = (*reader)->get_client_data_for_time(serverTime);

			if (val) {
				console::info("Overwriting data  [%f, %f, %f]  <-- [%f, %f, %f]\n", to->origin[0], to->origin[1], to->origin[2], (*val).origin[0], (*val).origin[1], (*val).origin[2]);
				to->origin[0] = (*val).origin[0];
				to->origin[1] = (*val).origin[1];
				to->origin[2] = (*val).origin[2];


				to->viewAngles[0] = (*val).viewAngles[0];
				to->viewAngles[1] = (*val).viewAngles[1];
				to->viewAngles[2] = (*val).viewAngles[2];

				to->velocity[0] = (*val).velocity[0];
				to->velocity[1] = (*val).velocity[1];
				to->velocity[2] = (*val).velocity[2];

				to->bobCycle = (*val).bobCycle;
				to->movementDir = (*val).movementDir;




				return 1;
			}

		}

		return result;
	}



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

			scripting::on_shutdown([](int free_scripts)
			{
				console::info("==== DEMO PLAYBACK SHUTDOWN ====\n");
				//if (current_reader) {
				//	current_reader->close();
				//	current_reader = std::nullopt;
				//}
			});

			cg_draw_active_frame_hook.create(0x1401D4710, &cg_drawactiveframe_stub);
			cl_get_predicted_player_information_for_server_time_hook.create(0x140210FF0, &cl_get_predicted_player_information_for_server_time_stub);
		}
	};
}

REGISTER_COMPONENT(demo_playback::component)
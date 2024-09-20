#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"
#include "game/dvars.hpp"

#include "console.hpp"

#include "game/demonware/byte_buffer.hpp"

#include <utils/hook.hpp>
#include <utils/string.hpp>
#include <utils/hexdump.hpp>
#include <utils/io.hpp>

#include "command.hpp"
#include "scheduler.hpp"
#include "scripting.hpp"
#include "demo_data.hpp"
#include "demo_playback.h"

namespace demo
{
	std::string get_dvar_string(const std::string& dvar)
	{
		const auto* dvar_value = game::Dvar_FindVar(dvar.data());
		if (dvar_value && dvar_value->current.string)
		{
			return { dvar_value->current.string };
		}

		return {};
	}

	class DemoRecorder {

	private:
		std::ofstream stream;

	public: 
		void write_message(int time, game::msg_t* data) {
			demonware::byte_buffer buffer;
			buffer.set_use_data_types(false);

			int start = *(int*)data->data;
			if (start == 0xffffffff) {
				time = start;
			}

			buffer.write_int32(time);
			buffer.write_int32(demo_data::DemoPacketType::SERVER_MESSAGE);
			buffer.write_blob(data->data, data->cursize);
			buffer.write_blob(data->splitData, data->splitSize);
			auto d = buffer.get_buffer();
			stream.write(d.data(), static_cast<std::streamsize>(d.size()));
			stream.flush();
		}

		void write_client_data(int time, demo_data::demo_client_data_t* data) {
			demonware::byte_buffer buffer;
			buffer.set_use_data_types(false);

			buffer.write_int32(time);
			buffer.write_int32(demo_data::DemoPacketType::CLIENT_DATA);

			buffer.write_blob((char*)data, (int)sizeof(demo_data::demo_client_data_t));
			auto d = buffer.get_buffer();

			stream.write(d.data(), static_cast<std::streamsize>(d.size()));
			stream.flush();
		}

		void init() {
			utils::io::create_directory("demos");

			auto map = get_dvar_string("ui_mapname");
			auto type = get_dvar_string("g_gametype");
			int start_time = *game::mp::clientTime;

			auto now = std::chrono::system_clock::now();
			std::time_t time = std::chrono::system_clock::to_time_t(now);

			auto tm = localtime(&time);

			auto min = tm->tm_min;
			auto hour = tm->tm_hour;
			auto year = tm->tm_year + 1900;
			auto day = tm->tm_mday;
			auto month = tm->tm_mon + 1;

			auto current_recording = std::format("{}_{}_{}_{}_{}_{}_{}.demo", type, map, month, day, year, hour, min);

			std::string path = "demos/" + current_recording;

			stream = std::ofstream(
				path, std::ios::binary | std::ofstream::out);

			demonware::byte_buffer buffer;
			buffer.set_use_data_types(false);

			buffer.write_blob(map);
			buffer.write_blob(type);

			auto d = buffer.get_buffer();
			stream.write(d.data(), static_cast<std::streamsize>(d.size()));
			stream.flush();
		}

		void close() {
			stream.close();
		}
	};


	utils::hook::detour cl_parseservermessage_hook;
	utils::hook::detour cl_connectionlesspacket_hook;
	utils::hook::detour cl_save_predicted_player_information_for_server_time_hook;

	std::optional<DemoRecorder> recorder;


	void begin_recording() {
		console::info("----- Beginning Demo Recording! ------\n");
		console::info("----- Map Name: %s ------\n", get_dvar_string("mapname").data());
		console::info("----- Game Type: %s ------\n", get_dvar_string("g_gametype").data());
		recorder = DemoRecorder();
		recorder->init();
	}

	void cl_parseservermessage_stub(int localClientNum, game::msg_t* message) {
		cl_parseservermessage_hook.invoke<void>(localClientNum, message);


		if (demo_playback::is_playing()) {
			console::info("CL_ParseServerMessage called during demo playback");
			return;
		}

		if (message->overflowed) {
			return;
		}

		if (recorder) {
			recorder->write_message(*game::mp::clientTime, message);
		};
	}

	void* cl_connectionlesspacket_stub(int localClientNum, game::netsrc_t* from, game::msg_t* message, int64_t a4) {


		std::string data = std::string(message->data, message->cursize);

		auto result = cl_connectionlesspacket_hook.invoke<void*>(localClientNum, from, message, a4);

		if (demo_playback::is_playing()) {
			return result;
		}

		if (data.starts_with("\xff\xff\xff\xff\connectResponse")) {
			if (!game::VirtualLobby_Loaded()) {
				begin_recording();
			}
		}

		if (message->overflowed) {
			return result;
		}

		// we dont need to record this
		if (data.starts_with("\xff\xff\xff\xff\syncDataResponse")) {
			return result;
		}

		if (recorder) {
			recorder->write_message(*game::mp::clientTime, message);
		};

		return result;
	}

	int cl_save_predicted_player_information_for_server_time_stub(game::mp::clientActive_t* clientActive, int serverTime) {
		int result = cl_save_predicted_player_information_for_server_time_hook.invoke<int>(clientActive, serverTime);


		if (recorder) {
			console::info("Saving client active: %llx   %d  (%d)  [%d]\n", clientActive, serverTime, *game::mp::clientTime, clientActive->cgamePredictedServerTime);

			auto state = (game::mp::playerstate**)(0x01417A1860);

			demo_data::demo_client_data_t data;
			data.predictedDataServerTime = clientActive->cgamePredictedServerTime;
			data.origin[0] = clientActive->cgameOrigin[0];
			data.origin[1] = clientActive->cgameOrigin[1];
			data.origin[2] = clientActive->cgameOrigin[2];

			data.velocity[0] = clientActive->cgameVelocity[0];
			data.velocity[1] = clientActive->cgameVelocity[1];
			data.velocity[2] = clientActive->cgameVelocity[2];

			data.viewAngles[0] = clientActive->cgameViewAngles[0] + (*state)->deltaAngles[0];
			data.viewAngles[1] = clientActive->cgameViewAngles[1] + (*state)->deltaAngles[1];
			data.viewAngles[2] = clientActive->cgameViewAngles[2] + (*state)->deltaAngles[2];

			data.bobCycle = clientActive->cgameBobCycle;
			data.movementDir = clientActive->cgameMovementDir;

			console::info("Saving client active: %f %f %f\n", data.origin[0], data.origin[0], data.origin[0] );

			recorder->write_client_data(*game::mp::clientTime, &data);
		}

		return result;
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			cl_parseservermessage_hook.create(0x1402129B0, &cl_parseservermessage_stub);
			cl_connectionlesspacket_hook.create(0x01402096E0, &cl_connectionlesspacket_stub);
			
			cl_save_predicted_player_information_for_server_time_hook.create(0x0140213140, &cl_save_predicted_player_information_for_server_time_stub);
			

			scripting::on_shutdown([](int free_scripts)
			{
				console::info("==== DEMO RECORDING SHUTDOWN ====\n");
				if (recorder) {
					recorder->close();
					recorder = std::nullopt;
				}
			});
		}
	};
}

REGISTER_COMPONENT(demo::component)
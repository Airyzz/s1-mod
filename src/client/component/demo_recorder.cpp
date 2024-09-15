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

			auto map = get_dvar_string("mapname");
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
	utils::hook::detour cg_draw_active_frame_hook;

	std::optional<DemoRecorder> recorder;

	game::dvar_t demo_rendering;

	int cg_drawactiveframe_stub(int localClientNum, int serverTime, int demoType, void* cubemapShot, void* cubemapSize, void* renderScreen, void* idk) {
		auto var = game::Dvar_FindVar("demo_rendering");
		int demo = demoType;
		if (var) {
			demo = var->current.integer;
		}
		return cg_draw_active_frame_hook.invoke<int>(localClientNum, serverTime, demo, cubemapShot, cubemapSize, renderScreen, idk);
	}


	void begin_recording() {
		console::info("----- Beginning Demo Recording! ------\n");
		console::info("----- Map Name: %s ------\n", get_dvar_string("mapname").data());
		console::info("----- Game Type: %s ------\n", get_dvar_string("g_gametype").data());
		recorder = DemoRecorder();
		recorder->init();
	}

	void cl_parseservermessage_stub(int localClientNum, game::msg_t* message) {
		cl_parseservermessage_hook.invoke<void>(localClientNum, message);

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

	void* cg_predict_player_state_stub(int localClientNum) {
		auto result = utils::hook::invoke<void*>(0x1401B6B70, localClientNum);

		if (recorder) {
			demo_data::demo_client_data_t data;

			data.commandTime = game::mp::pmoves->playerState->commandTime;
			data.otherflags = game::mp::pmoves->playerState->otherflags;
			data.pm_flags = game::mp::pmoves->playerState->pm_flags;
			data.pm_time = game::mp::pmoves->playerState->pm_time;
			data.pm_type = game::mp::pmoves->playerState->pm_type;
			data.serverTime = game::mp::pmoves->playerState->serverTime;

			data.cmd.serverTime = game::mp::pmoves->cmd.serverTime;
			data.cmd.buttons = game::mp::pmoves->cmd.buttons;
			data.cmd.angles_0 = game::mp::pmoves->cmd.angles_0;
			data.cmd.angles_1 = game::mp::pmoves->cmd.angles_1;
			data.cmd.angles_2 = game::mp::pmoves->cmd.angles_2;

			data.prevCmd.serverTime =	game::mp::pmoves->oldcmd.serverTime;
			data.prevCmd.buttons =		game::mp::pmoves->oldcmd.buttons;
			data.prevCmd.angles_0 =		game::mp::pmoves->oldcmd.angles_0;
			data.prevCmd.angles_1 =		game::mp::pmoves->oldcmd.angles_1;
			data.prevCmd.angles_2 =		game::mp::pmoves->oldcmd.angles_2;



			for (int i = 0; i < 3; i++) {
				data.origin[i] = game::mp::pmoves->playerState->origin[i];
				data.DeltaAngles[i] = game::mp::pmoves->playerState->DeltaAngles[i];
				data.velocity[i] = game::mp::pmoves->playerState->velocity[i];
				data.viewAngles[i] = game::mp::pmoves->playerState->viewAngles[i];
			}

			recorder->write_client_data(*game::mp::clientTime, &data);
		}

		auto reader = demo_playback::get_current_demo_reader();
		if (reader->has_value()) {
			auto val = (*reader)->get_current_client_data();

			if (val) {
				console::info("Overwrote predicted data with latest demo data!");

				game::mp::pmoves->playerState->commandTime = val->commandTime;
				game::mp::pmoves->playerState->otherflags = val->otherflags;
				game::mp::pmoves->playerState->pm_flags = val->pm_flags;
				game::mp::pmoves->playerState->pm_time = val->pm_time;
				game::mp::pmoves->playerState->pm_type = val->pm_type;
				game::mp::pmoves->playerState->serverTime = val->serverTime;

				game::mp::pmoves->cmd.serverTime = val->cmd.serverTime;
				game::mp::pmoves->cmd.buttons = val->cmd.buttons;
				game::mp::pmoves->cmd.angles_0 = val->cmd.angles_0;
				game::mp::pmoves->cmd.angles_1 = val->cmd.angles_1;
				game::mp::pmoves->cmd.angles_2 = val->cmd.angles_2;

				game::mp::pmoves->oldcmd.serverTime = val->prevCmd.serverTime;
				game::mp::pmoves->oldcmd.buttons = val->prevCmd.buttons;
				game::mp::pmoves->oldcmd.angles_0 = val->cmd.angles_0;
				game::mp::pmoves->oldcmd.angles_1 = val->cmd.angles_1;
				game::mp::pmoves->oldcmd.angles_2 = val->cmd.angles_2;

				for (int i = 0; i < 3; i++) {
					game::mp::playerStates->origin[i] = val->origin[i];
					game::mp::playerStates->DeltaAngles[i] = val->DeltaAngles[i];
					game::mp::playerStates->velocity[i] = val->velocity[i];
					game::mp::playerStates->viewAngles[i] = val->viewAngles[i];
				}
			}
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
			cg_draw_active_frame_hook.create(0x1401D4710, &cg_drawactiveframe_stub);
			
			utils::hook::call(0x1401D4BD6, cg_predict_player_state_stub);

			game::Dvar_RegisterInt("demo_rendering", 0, 0, 10, game::DVAR_FLAG_NONE, "is rendering in demo mode");

			scripting::on_shutdown([](int free_scripts)
			{
				console::info("==== DEMO RECORDING SHUTDOWN ====\n");
				if (recorder) {
					recorder->close();
				}
			});
		}
	};
}

REGISTER_COMPONENT(demo::component)
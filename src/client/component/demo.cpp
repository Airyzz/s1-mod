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
		void writeMessage(int time, game::msg_t* data) {
			demonware::byte_buffer buffer;
			buffer.set_use_data_types(false);

			int start = *(int*)data->data;
			if (start == 0xffffffff) {
				time = start;
			}

			buffer.write_int32(time);
			buffer.write_blob(data->data, data->cursize);
			buffer.write_blob(data->splitData, data->splitSize);
			auto d = buffer.get_buffer();
			stream.write(d.data(), static_cast<std::streamsize>(d.size()));
			stream.flush();
		}

		void init() {
			utils::io::create_directory("demos");

			auto map = get_dvar_string("ui_mapname");
			auto type = get_dvar_string("g_gametype");

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
		}

		void close() {
			stream.close();
		}
	};


	std::string current_recording;


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
		console::info("----- Map Name: %s ------\n", get_dvar_string("ui_mapname").data());
		console::info("----- Game Type: %s ------\n", get_dvar_string("g_gametype").data());
		recorder = DemoRecorder();
		recorder->init();
	}

	void cl_parseservermessage_stub(int localClientNum, game::msg_t* message) {

		auto copy = *message;
		copy.data = reinterpret_cast<char*>(malloc(copy.cursize));
		memcpy(copy.data, message->data, message->cursize);

		std::string data = std::string(copy.data, copy.cursize);

		console::info("\tCL_ParseServerMessage (pre): [cursize: %d  - readcount: %d  -  overflowed: %d]\n", message->cursize, message->readcount, message->overflowed);
		console::info("tCL_ParseServerMessage: [cursize %d  -  serverTime: %d   gameTime: %d   clientTime: %d]  CURRENT STATUS: %d\n", copy.cursize, *game::mp::serverTime, *game::mp::gameTime, *game::mp::clientTime, *game::connectionStatus);
		//utils::hexdump::dump_hex_to_stdout(std::string(copy.data, copy.cursize));

		console::info("=== BEGIN PARSE SERVER MESSAGE INVOKE ===\n");
		cl_parseservermessage_hook.invoke<void>(localClientNum, message);
		console::info("=== END PARSE SERVER MESSAGE INVOKE ===\n");

		if (message->overflowed) {
			console::info("Message overflowed! CL_ParseServerMessage: [cursize: %d  - readcount: %d]\n", message->cursize, message->readcount);
			return;
		}

		if (recorder) {
			console::info("Recorded CL_ServerMessage for serverTime: %d  gameTime: %d   clientTime: %d\n", *game::mp::serverTime, *game::mp::gameTime, *game::mp::clientTime);
			recorder->writeMessage(*game::mp::serverTime, &copy);
		};

	}

	void* cl_connectionlesspacket_stub(int localClientNum, game::netsrc_t* from, game::msg_t* message, int64_t a4) {
		auto copy = *message;
		copy.data = reinterpret_cast<char*>(malloc(copy.cursize));
		memcpy(copy.data, message->data, message->cursize);

		std::string data = std::string(copy.data, copy.cursize);

		if (data.starts_with("\xff\xff\xff\xff\connectResponse")) {
			begin_recording();
		}

		console::info("\CL_ConnectionlessPacket (pre): [cursize: %d  - readcount: %d  -  overflowed: %d]\n", message->cursize, message->readcount, message->overflowed);
		console::info("CL_ConnectionlessPacket: [cursize %d  -  serverTime: %d   gameTime: %d   clientTime: %d]  CURRENT STATUS: %d\n", copy.cursize, *game::mp::serverTime, *game::mp::gameTime, *game::mp::clientTime, *game::connectionStatus);
		//utils::hexdump::dump_hex_to_stdout(std::string(copy.data, copy.cursize));

		console::info("=== BEGIN CONNECTIONLESS PACKET INVOKE ===\n");
		auto result = cl_connectionlesspacket_hook.invoke<void*>(localClientNum, from, message, a4);
		console::info("=== END CONNECTIONLESS PACKET INVOKE ===\n");
		if (message->overflowed) {
			console::info("Message overflowed! CL_ConnectionlessPacket: [cursize: %d  - readcount: %d]\n", message->cursize, message->readcount);
			return result;
		}

		if (recorder) {
			console::info("Recorded CL_ConnectionlessPacket for serverTime: %d  gameTime: %d   clientTime: %d\n", *game::mp::serverTime, *game::mp::gameTime, *game::mp::clientTime);
			recorder->writeMessage(*game::mp::serverTime, &copy);
		};

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
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

	utils::hook::detour cl_packetevent_hook;
	utils::hook::detour cl_parseservermessage_hook;
	utils::hook::detour cl_connectionlesspacket_hook;

	utils::hook::detour cl_parsemessage_hook;

	utils::hook::detour msg_read_delta_playerstate_hook;
	utils::hook::detour cg_processentity_hook;

	utils::hook::detour cl_parse_packet_unk1_hook;
	utils::hook::detour cl_parse_packet_unk2_hook;

	utils::hook::detour msg_read_delta_client_hook;

	utils::hook::detour msg_read_delta_struct_hook;

	utils::hook::detour cg_draw_active_frame_hook;

	utils::hook::detour msg_readlong_hook;
	utils::hook::detour msg_readbyte_hook;
	utils::hook::detour msg_discard_hook;

	utils::hook::detour memfile_readcompresseddata_hook;
	utils::hook::detour msg_readbitscompress_hook;

	std::optional<DemoRecorder> recorder;

	game::dvar_t demo_rendering;

	void cl_parse_packet_unk1_stub(void* cl, game::msg_t* msg, int time, unsigned int* oldFrame, unsigned int* newFrame) {
		char buf[0x5160];
		memcpy(buf, newFrame, 0x5160);

		cl_parse_packet_unk1_hook.invoke<void>(cl, msg, time, oldFrame, newFrame);

		memcpy(newFrame, buf, 0x5160);
	}

	void cl_parse_packet_unk2_stub(void* cl, game::msg_t* msg, int time, unsigned int* oldFrame, unsigned int* newFrame) {
		char buf[0x5160];
		memcpy(buf, newFrame, 0x5160);

		cl_parse_packet_unk2_hook.invoke<void>(cl, msg, time, oldFrame, newFrame);

		memcpy(newFrame, buf, 0x5160);

		//newFrame[5192] = time;
	}

	int64_t msg_readdeltaclient_stub(game::msg_t* msg, int time, void* fromState, void* toState, int number) {
		console::info("[%d] Reading delta client for number: [%d]!\n", time, number);
		return msg_read_delta_client_hook.invoke<int64_t>(msg, time, fromState, toState, number);
	}

	int64_t msg_read_delta_struct_stub(game::msg_t* msg, int time, void* from, void* to, int number, int numFields, int indexBits, void* stateFields, void* skippedFieldbits) {
		console::info("\tReading delta struct for number: [%d] num fields: (%d)!\n", time, number, numFields);
		return msg_read_delta_struct_hook.invoke<int64_t>(msg, time, from, to, number, numFields, indexBits, stateFields, skippedFieldbits);
	}

	int64_t msg_readlong_stub(game::msg_t* msg) {
		auto result = msg_readlong_hook.invoke<int64_t>(msg);
		console::info("read_long: %llx\n", result);

		return result;
	}

	uint8_t msg_readbyte_stub(game::msg_t* msg) {
		auto result = msg_readbyte_hook.invoke<uint8_t>(msg);
		console::info("read_byte: %x\n", result);
		return result;
	}

	int64_t memfile_readcompresseddata_stub(void* src, int num_bytes, void* dst, int dst_size_maybe) {


		console::info("MemFile_ReadCompressedData: %llx   %llx   %llx    %llx\n", src, num_bytes, dst, dst_size_maybe);

		auto result = memfile_readcompresseddata_hook.invoke<int64_t>(src, num_bytes, dst, dst_size_maybe);

		console::info("MemFile_ReadCompressedData returned: %llx\n", result);

		auto compressed_data = std::string(reinterpret_cast<char*>(src), num_bytes);
		auto decompressed_data = std::string(reinterpret_cast<char*>(dst), result);

		console::info("MemFile_ReadCompressedData compressed data: \n");
		utils::hexdump::dump_hex_to_stdout(compressed_data);

		console::info("THERE SHOULD HAVE BEEN DATA RIGHT ABOVE HERE!\n");
		console::info("MemFile_ReadCompressedData decompressed data:\n");
		utils::hexdump::dump_hex_to_stdout(decompressed_data);


		return result;
	}

	int64_t MSG_ReadBitsCompress_stub(void* from, void* to, int size, int64_t dst_size, int* out_size) {

		console::info("MSG_ReadBitsCompress: %llx   %llx   %x   %llx    %llx\n", from, to, size, dst_size, out_size);
		auto result = msg_readbitscompress_hook.invoke<int64_t>(from, to, size, dst_size, out_size);

		console::info("MSG_ReadBitsCompress returned: %llx   (%d bytes)\n", result, *out_size);

		auto compressed_data = std::string(reinterpret_cast<char*>(from), size);
		auto decompressed_data = std::string(reinterpret_cast<char*>(to), *out_size);

		//console::info("MSG_ReadBitsCompress compressed data:\n");
		//utils::hexdump::dump_hex_to_stdout(compressed_data);


		//console::info("MSG_ReadBitsCompress decompressed data:\n");
		//utils::hexdump::dump_hex_to_stdout(decompressed_data);

		return result;
	}

	void* msg_discard_stub(void* a1) {
		auto result = msg_discard_hook.invoke<void*>(a1);
		console::info("MSG_Discard(%llx) called -> %llx\n", a1, result);
		return result;
	}

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

	int cl_packetevent_stub(int localClientNum, game::netadr_s* from, game::msg_t* message, int time) {

		int result = cl_packetevent_hook.invoke<int>(localClientNum, from, message, time);

		console::info("\CL_PacketEvent  received msg of [cursize: %d ]\n", message->cursize);



		return result;
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

	void cl_parsemessage_stub(int localClientNum, void* idk, game::msg_t* message) {
		console::info("\t\tCL_ParseMessage (pre): [cursize: %d  - readcount: %d  -  overflowed: %d]\n", message->cursize, message->readcount, message->overflowed);
		cl_parsemessage_hook.invoke<void>(localClientNum, idk, message);
		console::info("\t\tCL_ParseMessage (post): [cursize: %d  - readcount: %d  -  overflowed: %d]\n", message->cursize, message->readcount, message->overflowed);
	}

	void msg_read_delta_playerstate_stub(int localClientNum, game::msg_t* msg, int time, game::playerState_s* from, game::playerState_s* to, bool predictedFieldsIgnoreXor) {
		msg_read_delta_playerstate_hook.invoke<void>(localClientNum, msg, time, from, to, predictedFieldsIgnoreXor);

		console::info("MSG_ReadDeltaPlayerState: %d, %llx, %d, %llx, %llx, %d\n", localClientNum, msg, time, from, to, predictedFieldsIgnoreXor);
	}

	uint8_t msg_readfirstbyte_stub(game::msg_t* msg) {
		auto result = game::MSG_ReadByte(msg);
		console::info("CL_ParseMessage: got first byte %d\n", result);

		if (result == 0) {
			console::info("CL_ParseMessage: FIRST BYTE WAS ZERO!! MESSAGE: %d\n", result);
			std::string data = std::string(msg->data, msg->cursize);

			console::info("CL_ParseMessage: [cursize %d  - readcount  %d]  CURRENT STATUS: %d    %s\n", msg->cursize, msg->readcount, *game::connectionStatus, utils::string::dump_hex(std::string(msg->data, msg->cursize)).data());
		}

		return result;
	};

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			cl_packetevent_hook.create(0x14020D720, &cl_packetevent_stub);
			
			//utils::hook::call(0x1402117E3, msg_readfirstbyte_stub);
			//memfile_readcompresseddata_hook.create(0x01404C5F80, &memfile_readcompresseddata_stub);
			//msg_readbitscompress_hook.create(0x01403D47E0, &MSG_ReadBitsCompress_stub);
			
			cl_parseservermessage_hook.create(0x1402129B0, &cl_parseservermessage_stub);
			cl_connectionlesspacket_hook.create(0x01402096E0, &cl_connectionlesspacket_stub);
			
			//cl_parsemessage_hook.create(0x1402117A0, &cl_parsemessage_stub);
			//msg_discard_hook.create(0x1403D4380, &msg_discard_stub);

			// msg_read_delta_playerstate_hook.create(0x1403D76F0, &msg_read_delta_playerstate_stub);
			//cl_parse_packet_unk1_hook.create(0x140211DE0, &cl_parse_packet_unk1_stub);
			//cl_parse_packet_unk2_hook.create(0x140211940, &cl_parse_packet_unk2_stub);
			//msg_read_delta_client_hook.create(0x1403D6030, &msg_readdeltaclient_stub);
			//msg_read_delta_struct_hook.create(0x1403D6C10, &msg_read_delta_struct_stub);
			
			cg_draw_active_frame_hook.create(0x1401D4710, &cg_drawactiveframe_stub);

			game::Dvar_RegisterInt("demo_rendering", 0, 0, 10, game::DVAR_FLAG_NONE, "is rendering in demo mode");

			command::add("dump_info", [&]()
			{
				console::info("cg_draw2d: %llx\n", game::Dvar_FindVar("cg_draw2d"));

				msg_readbyte_hook.create(0x1403D4890, &msg_readbyte_stub);
				msg_readlong_hook.create(0x1403D4B70, &msg_readlong_stub);
			});

			scripting::on_shutdown([](int free_scripts)
			{
				console::info("==== DEMO RECORDING SHUTDOWN ====\n");
				if (recorder) {
					recorder->close();
				}
				//game::Dvar_SetBool(demo_recording, false);
			});
		}
	};
}

REGISTER_COMPONENT(demo::component)
#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"
#include "game/dvars.hpp"

#include "console.hpp"

#include <utils/hook.hpp>
#include <utils/string.hpp>
#include <utils/hexdump.hpp>

#include "scheduler.hpp"
#include "scripting.hpp"
namespace demo
{

	struct client_msg {
		bool replayed;
		int len;
		char* data;
	};

	game::dvar_t* demo_recording;

	game::dvar_t* demo_playback;

	std::map<int, game::msg_t> storedSnapshots;

	std::map<int, std::list<client_msg>> storedClientSnapshots;

	utils::hook::detour net_send_loop_hook;
	utils::hook::detour sys_send_packet_hook;

	utils::hook::detour sys_get_packet_hook;
	utils::hook::detour net_get_loop_packet_hook;

	void sv_send_message_to_client_stub(game::msg_t* msg, game::mp::client_t* client) {

		int time = *game::mp::gameTime;

		
		if (demo_playback->current.enabled) {
			if (storedSnapshots.find(time) != storedSnapshots.end()) {
				auto snapshot = storedSnapshots[time];
				auto dump = utils::hexdump::dump_hex(std::string(snapshot.data, snapshot.cursize)).data();
				console::info("Demo Recorder: Playing back stored snapshot (%d) %s\n", time, dump);
				utils::hook::invoke<void>(0x140451040, snapshot, client);
				return;
			}
			else {
				console::info("Demo Recorder: Could not find snapshot for frame: %d\n", time);
			}

		} else if (demo_recording->current.enabled) {
			auto dump = utils::hexdump::dump_hex(std::string(msg->data, msg->cursize)).data();
			console::info("Demo Recorder: [%d] Sending snapshot to client %s\n", time, dump);
			game::msg_t copy = *msg;
			
			copy.data = (char *) alloca(msg->cursize);
			memcpy(copy.data, msg->data, msg->cursize);

			storedSnapshots[time] = copy;
		}

		utils::hook::invoke<void>(0x140451040, msg, client); // SV_SendMessageToClient
	}

	int64_t cl_netchan_transmit_sub(void* chan, char* data, int len)  {
		int time = *game::mp::gameTime;

		if (demo_playback->current.enabled) {
			int64_t result = 0;
			if (storedClientSnapshots.find(time) != storedClientSnapshots.end()) {
				auto snapshots = storedClientSnapshots[time];
				
				for (; !snapshots.empty(); snapshots.pop_front()) {
					client_msg msg = snapshots.front();
				//	console::info("CL_Netchan playing back [%s]\n", utils::string::dump_hex(std::string(msg.data, msg.len)).data());
					result = utils::hook::invoke<int64_t>(0x140210A70, chan, msg.data, msg.len);
				}

			}

			return result;
		} else if (demo_recording->current.enabled) {
			client_msg msg;
			msg.replayed = false;
			msg.len = len;
			msg.data = (char*) alloca(len + 1);

			memcpy(msg.data, data, len);

			//console::info("CL_Netchan [%d] Stored %d bytes, %s\n", time, len, utils::string::dump_hex(std::string(msg.data, msg.len)).data());
			if (storedClientSnapshots.find(time) != storedClientSnapshots.end()) {
				storedClientSnapshots[time].push_back(msg);
			}
			else {
				storedClientSnapshots[time] = {msg};
			}
		}

		return utils::hook::invoke<int64_t>(0x140210A70, chan, data, len);
	}

	int sys_get_packet_stub(game::netadr_s* from, game::msg_t* message) {
		int result = sys_get_packet_hook.invoke<int>(from, message);
		
		if (result != 0) {
			std::string data = std::string(message->data, message->cursize);
			std::string dump = utils::hexdump::dump_hex(data);
			console::info("Sys_GetPacket !![%d]!!: (%d, %d, %d) %s\n", message->targetLocalNetID, from->type, from->localNetID, from->addrHandleIndex,  dump.data());
		}
		else {
			//console::info("Sys_GetPacket Attempted: (%d, %d, %d)\n", from->type, from->localNetID, from->addrHandleIndex);
		}
		
		return result;
	}

	int net_get_loop_packet_stub(game::netsrc_t sock, game::netadr_s* from, game::msg_t* message) {

		int result = net_get_loop_packet_hook.invoke<int>(sock, from, message);

		if (result != 0) {
			/*
			std::string data = std::string(message->data, message->cursize);
			std::string dump = utils::hexdump::dump_hex(data);
			console::info("(Net_GetLoop) sock: %d, type: %d, id: %d\n", sock, from->type, from->localNetID);
			if (sock == game::NS_SERVER) {
				console::info("(Net_GetLoop) server <- client %s\n", dump.data());
			}
			else if (sock == game::NS_CLIENT1) {
				console::info("(Net_GetLoop) client <- server %s\n", dump.data());
			}
			*/
		}

		return result;
	}
	
	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{

			utils::hook::call(0x1404513CF, sv_send_message_to_client_stub); // SV_SendSnapshots
			utils::hook::call(0x140205D63, cl_netchan_transmit_sub); // CL_WritePacket

			sys_get_packet_hook.create(0x1404D8280, &sys_get_packet_stub);
			net_get_loop_packet_hook.create(0x1403DABC0, &net_get_loop_packet_stub);


			console::info("Demo component registered!");


			
			demo_recording = game::Dvar_RegisterBool("demo_recording", false, game::DVAR_FLAG_NONE, "True if a demo is currently being recorded");
			demo_playback = game::Dvar_RegisterBool("demo_playback", false, game::DVAR_FLAG_NONE, "True if we are currently playing back a demo");
			

			scripting::on_init([]
			{
				console::info("------- Game Initialization -------\n");
				console::info("----- DEMO RECORDING INITIALIZE ------\n");
				console::info("----- DEMO RECORDING INITIALIZE ------\n");
				console::info("----- DEMO RECORDING INITIALIZE ------\n");
				console::info("----- DEMO RECORDING INITIALIZE ------\n");
				//game::Dvar_SetBool(demo_recording, true);
			});

			
			scripting::on_shutdown([](int free_scripts)
			{
				console::info("==== DEMO RECORDING SHUTDOWN ====\n");
				//game::Dvar_SetBool(demo_recording, false);
			});


			scheduler::on_game_initialized([]
			{

				console::info("==================================\n");
				console::info("DEMO RECORDING STARTED!\n");
				console::info("DEMO RECORDING STARTED!\n");
				console::info("DEMO RECORDING STARTED!\n");
				console::info("DEMO RECORDING STARTED!\n");
				console::info("DEMO RECORDING STARTED!\n");
				console::info("==================================\n");


			}, scheduler::pipeline::main);


		}
	};
}

REGISTER_COMPONENT(demo::component)
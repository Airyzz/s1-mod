#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"
#include "game/dvars.hpp"

#include "console.hpp"

#include <utils/hook.hpp>
#include <utils/string.hpp>

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

	void sv_send_message_to_client_stub(game::msg_t* msg, game::mp::client_t* client) {

		int time = *game::mp::gameTime;
		
		if (demo_playback->current.enabled) {
			if (storedSnapshots.find(time) != storedSnapshots.end()) {
				auto snapshot = storedSnapshots[time];

				console::info("Demo Recorder: Playing back stored snapshot (%d) %s\n", time, utils::string::dump_hex(std::string(snapshot.data, snapshot.cursize)).data());
				utils::hook::invoke<void>(0x140451040, snapshot, client);
				return;
			}
			else {
				console::info("Demo Recorder: Could not find snapshot for frame: %d\n", time);
			}

		} else if (demo_recording->current.enabled) {
			console::info("Demo Recorder: [%d] Sending snapshot to client %s\n", time,  utils::string::dump_hex(std::string(msg->data, msg->cursize)).data());
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


	int sys_string_to_adr_stub(const char* s, game::netadr_s* a) {
		console::info("SYS_StringToAdr called: %s\n", s);
		if (strcmp(s, "demo.server") == 0) {
			memcpy(a->ip, "demo", 4);
			a->port = 1337;
			a->type = game::NA_DEMO_SERVER;

			console::info("Returning demo server ip\n");
			return 1;
		}

		return utils::hook::invoke<int>(0x1404B7110, s, a);
	}
	
	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{

			utils::hook::call(0x1404513CF, sv_send_message_to_client_stub); // SV_SendSnapshots
		//	utils::hook::call(0x140205D63, cl_netchan_transmit_sub); // CL_WritePacket

			utils::hook::call(0x1403DB0E6, sys_string_to_adr_stub); //Net_StringToIP4
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


		}
	};
}

REGISTER_COMPONENT(demo::component)
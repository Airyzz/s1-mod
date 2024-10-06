#include <std_include.hpp>
#include "loader/component_loader.hpp"
#include "game/game.hpp"

#include "command.hpp"
#include "console.hpp"
#include "filesystem.hpp"
#include "network.hpp"
#include "party.hpp"
#include "scheduler.hpp"
#include "server_list.hpp"

#include <utils/hook.hpp>
#include <utils/string.hpp>

namespace bots
{
	namespace
	{
		struct botoutfit_t {
			int u1;
			int top;
			int head;
			int pants;
			int gloves;
			int boots;
			int kneeguards;
			int loadout;
			int helmet;
			int eyewear;
			int exo;
		};

		constexpr std::size_t MAX_NAME_LENGTH = 16;

		bool can_add()
		{
			if (party::get_client_count() < *game::mp::svs_numclients)
			{
				return true;
			}

			return false;
		}

		void bot_team_join(const unsigned int entity_num)
		{
			scheduler::once([entity_num]()
			{
				game::Scr_AddInt(2);
				game::Scr_AddString("team_select");
				game::Scr_Notify(&game::mp::g_entities[entity_num], static_cast<std::uint16_t>(game::SL_GetString("luinotifyserver", 0)), 2);

				scheduler::once([entity_num]()
				{
					game::Scr_AddString(utils::string::va("class%d", std::rand() % 5));
					game::Scr_AddString("class_select");
					game::Scr_Notify(&game::mp::g_entities[entity_num], static_cast<std::uint16_t>(game::SL_GetString("luinotifyserver", 0)), 2);
				}, scheduler::pipeline::server, 2s);
			}, scheduler::pipeline::server, 2s);
		}

		void spawn_bot(const int entity_num)
		{
			game::SV_SpawnTestClient(&game::mp::g_entities[entity_num]);
			if (game::Com_GetCurrentCoDPlayMode() == game::CODPLAYMODE_CORE)
			{
				bot_team_join(entity_num);
			}
		}

		void add_bot()
		{
			if (!can_add())
			{
				return;
			}

			// SV_BotGetRandomName
			const auto* const bot_name = game::SV_BotGetRandomName();
			const auto* bot_ent = game::SV_AddBot(bot_name);
			if (bot_ent)
			{
				spawn_bot(bot_ent->s.number);
			}
			else if (can_add()) // workaround since first bot won't ever spawn
			{
				add_bot();
			}
		}

		utils::hook::detour get_bot_name_hook;
		volatile bool bot_names_received = false;
		std::vector<std::string> bot_names;

		std::vector<std::string> bot_outfits = {
			// default outfits
			/*"1|11|4|196609|2|3|8|4|17|7|458753|",
			"1|10|12|12|3|4|4|8|24|0|8|",
			"1|1|8|3|2|11|3|3|2|0|2|",
			"1|1|14|13|1|3|13|7|0|0|1|",
			"2|8|10|12|1|9|12|13|1|0|1|",
			"1|2|9|1|1|9|12|13|24|1|2|",
			"1|10|5|131085|3|7|3|17|0|0|3|",
			"2|11|11|13|12|11|15|4|0|0|458753|",
			"1|2|5|196609|14|10|4|5|3|0|458753|",
			"1|12|13|9|3|4|8|6|15|0|3|",
			"1|1|6|1|1|1|1|1|4|0|8|",
			"2|7|12|9|12|10|4|17|0|8|1|",
			"1|9|7|131085|2|3|65542|7|0|0|7|",
			"2|10|15|1|12|7|65542|4|17|0|8|",
			"1|1|17|1|1|1|0|1|0|0|1|",
			"2|7|9|3|1|9|12|4|0|0|1|",
			"1|7|3|9|1|10|65542|3|0|2|8|"*/
		};

		bool should_use_remote_bot_names()
		{
#ifdef ALLOW_CUSTOM_BOT_NAMES
			return !filesystem::exists("bots.txt");
#else
			return true;
#endif
		}

		void parse_bot_names_from_file()
		{
			std::string data;
			filesystem::read_file("bots.txt", &data);
			if (data.empty())
			{
				return;
			}

			auto name_list = utils::string::split(data, '\n');
			for (auto& entry : name_list)
			{
				// Take into account CR line endings
				entry = utils::string::replace(entry, "\r", "");

				if (entry.empty())
				{
					continue;
				}

				entry = entry.substr(0, MAX_NAME_LENGTH - 1);
				bot_names.emplace_back(entry);
			}
		}

		void parse_bot_outfits_from_file()
		{
			std::string data;
			filesystem::read_file("bot_outfits.txt", &data);
			if (data.empty())
			{
				return;
			}

			auto name_list = utils::string::split(data, '\n');
			for (auto& entry : name_list)
			{
				// Take into account CR line endings
				entry = utils::string::replace(entry, "\r", "");

				if (entry.starts_with("//")) {
					continue;
				}

				if (entry.empty())
				{
					continue;
				}


				console::info("Added bot outfit: %s\n", entry.data());
				bot_outfits.emplace_back(entry);
			}
		}

		const char* get_random_bot_name()
		{
			if (!bot_names_received && bot_names.empty())
			{
				// last attempt to use custom names if they can be found
				parse_bot_names_from_file();
			}

			if (bot_names.empty())
			{
				return get_bot_name_hook.invoke<const char*>();
			}

			const auto index = std::rand() % bot_names.size();
			const auto& name = bot_names.at(index);

			return utils::string::va("%.*s", static_cast<int>(name.size()), name.data());
		}

		void update_bot_names()
		{
			bot_names_received = false;

			game::netadr_s master{};
			if (server_list::get_master_server(master))
			{
				console::info("Getting bots...\n");
				network::send(master, "getbots");
			}
		}

		int bot_format_customization_string(botoutfit_t* outfit, char* dst, int size) {
			if (bot_outfits.empty()) {
				return sprintf(dst, "%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|",
					outfit->u1,
					outfit->top,
					outfit->head,
					outfit->pants,
					outfit->gloves,
					outfit->boots,
					outfit->kneeguards,
					outfit->loadout,
					outfit->helmet,
					outfit->eyewear,
					outfit->exo
				);
			}

			int random_value = std::rand();
			int i = random_value % bot_outfits.size();
			auto pick = bot_outfits.at(i);

			int character = (std::rand() % 15) + 1;
			auto result = utils::string::replace(pick, "$random_character", std::to_string(character));
			
			int len = result.length();
			memcpy(dst, result.data(), len);

			return len;
		}
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			if (game::environment::is_sp())
			{
				return;
			}

			std::srand(std::time(nullptr));

			get_bot_name_hook.create(game::SV_BotGetRandomName, get_random_bot_name);
			utils::hook::call(0x140439039, bot_format_customization_string);

			command::add("spawnBot", [](const command::params& params)
			{
				if (!game::SV_Loaded() || game::VirtualLobby_Loaded()) return;

				auto num_bots = 1;
				if (params.size() == 2)
				{
					num_bots = std::atoi(params.get(1));
				}

				num_bots = std::min(num_bots, *game::mp::svs_numclients);

				console::info("Spawning %i %s\n", num_bots, (num_bots == 1 ? "bot" : "bots"));

				for (auto i = 0; i < num_bots; ++i)
				{
					scheduler::once(add_bot, scheduler::pipeline::server, 100ms * i);
				}
			});

			command::add("print_fit", [](const command::params& params)
			{
				auto outfit = (botoutfit_t*)(0x147B73360);

				 console::info("\n\n\n%d|%d|$random_character|%d|%d|%d|%d|%d|%d|%d|%d|\n\n\n",
					outfit->u1,
					outfit->top,
					//outfit->head,
					outfit->pants,
					outfit->gloves,
					outfit->boots,
					outfit->kneeguards,
					outfit->loadout,
					outfit->helmet,
					outfit->eyewear,
					outfit->exo
				);
			});

			if (should_use_remote_bot_names())
			{
				scheduler::on_game_initialized([]() -> void
				{
					update_bot_names();
					scheduler::loop(update_bot_names, scheduler::main, 1h);
				}, scheduler::main);
			}
			else
			{
				parse_bot_names_from_file();
			}

			parse_bot_outfits_from_file();

			network::on("getbotsResponse", [](const game::netadr_s& target, const std::string_view& data)
			{
				game::netadr_s master{};
				if (server_list::get_master_server(master) && !bot_names_received && target == master)
				{
					const std::string received_data{ data };
					bot_names = utils::string::split(received_data, '\n');
					console::info("Got %zu names from the master server\n", bot_names.size());
					bot_names_received = true;
				}
			});
		}
	};
}

REGISTER_COMPONENT(bots::component)

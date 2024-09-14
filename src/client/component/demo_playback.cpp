#include <std_include.hpp>
#include "loader/component_loader.hpp"
#include "game/game.hpp"

#include "command.hpp"
#include "console.hpp"
#include <utils/io.hpp>
#include "game/demonware/servers/theater_server.hpp"

namespace demo_playback
{
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
						demonware::theater::instance->set_demo_file(filepath);
						command::execute("connect demo");
					}
					else {
						console::warn("Demo file does not exist: %s\n", filepath);
					}
					
				}
			});
		}
	};
}

REGISTER_COMPONENT(demo_playback::component)
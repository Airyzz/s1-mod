#pragma once

#include "udp_server.hpp"

namespace demonware
{
	namespace theater {
	class theater_server : public udp_server
	{
	public:
		theater_server(std::string name);
		void frame() override;
		using udp_server::udp_server;
		void set_demo_file(std::string filepath);
		void stop();

	private:

		void handle(const endpoint_data& endpoint, const std::string& packet) override;
		void handle_connectionless_packet(const endpoint_data& endpoint, const std::string& packet);
		void handle_get_info(const endpoint_data& endpoint, const std::string& packet);
		void handle_get_challenge(const endpoint_data& endpoint, const std::string& packet);
		void handle_connect(const endpoint_data& endpoint, const std::string& packet);
		void server_frame();
	};

	static theater_server* instance;
	}
}

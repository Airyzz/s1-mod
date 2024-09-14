#pragma once

#include "udp_server.hpp"

namespace demonware
{
	class theater_server : public udp_server
	{
	public:
		void frame() override;
		using udp_server::udp_server;


	private:

		void handle(const endpoint_data& endpoint, const std::string& packet) override;
		void handle_connectionless_packet(const endpoint_data& endpoint, const std::string& packet);
		void handle_get_info(const endpoint_data& endpoint, const std::string& packet);
		void handle_get_challenge(const endpoint_data& endpoint, const std::string& packet);
		void handle_connect(const endpoint_data& endpoint, const std::string& packet);
		void handle_stats(const endpoint_data& endpoint, const std::string& packet);
		void server_frame();
	};
}

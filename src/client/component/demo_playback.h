#pragma once
#include "demo_data.hpp"
#include <optional>
#include <string>

namespace demo_playback
{
	class demo_reader
	{
	private:
		int time;
		int firstMessageTime;
		int offsetTime;
		std::ifstream stream;
		std::string map;
		std::string mode;
		std::list<std::string> server_queue;
		demo_data::demo_client_data_t last_client_data;


		int peek_next_message_time();

		void read_server_message();
		void read_client_data();
		void read_message();

	public:
		demo_reader(std::string filepath);
		std::optional<demo_data::demo_client_data_t> get_current_client_data();
		std::optional<std::string> dequeue_server_message();
		std::string get_map_name();
		std::string get_mode();

		void read_frame(int ms);
	};

	std::optional<demo_reader>* get_current_demo_reader();
}
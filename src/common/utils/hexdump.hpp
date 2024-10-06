#pragma once
#include <string>

namespace utils::hexdump {
	std::string dump_hex(const std::string& data);

	void dump_hex_to_stdout(const std::string& data);
}
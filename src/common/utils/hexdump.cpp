#include "hexdump.hpp"

#include <iostream>
#include <vector>
#include <iomanip>
#include <numeric>
#include <sstream>

namespace utils::hexdump
{

    std::ostream& render_printable_chars(std::ostream& os, const char* buffer, size_t bufsize) {
        os << " | ";
        for (size_t i = 0; i < bufsize; ++i)
        {
            if (std::isprint(static_cast<unsigned char>(buffer[i])))
            {
                os << buffer[i];
            }
            else
            {
                os << ".";
            }
        }
        return os;
    }

    std::ostream& hex_dump(std::ostream& os, const uint8_t* buffer, size_t bufsize, bool showPrintableChars = true)
    {

        os << std::hex;
        bool printBlank = false;
        size_t i = 0;
        int width = 16;
        for (; i < bufsize; ++i)
        {
            if (i % width == 0)
            {
                if (i != 0 && showPrintableChars)
                {
                    render_printable_chars(os, reinterpret_cast<const char*>(&buffer[i] - width), width);
                }
                os << std::endl;
                printBlank = false;
            }
            if (printBlank)
            {
                os << ' ';
            }
            os << std::setw(2) << std::right << unsigned(buffer[i]);
            if (!printBlank)
            {
                printBlank = true;
            }
        }
        if (i % width != 0 && showPrintableChars)
        {
            for (size_t j = 0; j < width - (i % width); ++j)
            {
                os << "   ";
            }
            render_printable_chars(os, reinterpret_cast<const char*>(&buffer[i] - (i % width)), (i % width));
        }

        os << std::endl;

        return os;
    }


	std::string dump_hex(const std::string& data)
	{
        std::stringstream ss;
        hex_dump(ss, reinterpret_cast<const uint8_t*>(data.data()), data.length(), true);

        return ss.str();
	}
}

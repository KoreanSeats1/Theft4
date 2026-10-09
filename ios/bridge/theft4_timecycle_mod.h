#pragma once
#include <cmath>
#include <filesystem>
#include <fstream>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>

namespace theft4::mods {
// The supported base game's nine weather tables each contain eleven time slots.
// Reject truncated/extended tables before the guest parser consumes them.
inline bool ValidTimeCycle(std::istream& input) {
    size_t rows = 0;
    std::string line;
    while (std::getline(input, line)) {
        line.resize(line.find("//") == std::string::npos ? line.size() : line.find("//"));
        std::istringstream values(line);
        values.imbue(std::locale::classic());
        values >> std::ws;
        if (values.eof()) continue;
        size_t columns = 0;
        std::string token;
        while (values >> token) {
            std::istringstream number(token);
            number.imbue(std::locale::classic());
            double value;
            if (!(number >> value) || !std::isfinite(value) || ++columns > 134) return false;
            number >> std::ws;
            if (!number.eof()) return false;
        }
        if (!values.eof() || columns != 134 || ++rows > 99) return false;
    }
    return !input.bad() && rows == 99;
}

inline bool ValidTimeCycleFile(const std::filesystem::path& file) {
    std::error_code error;
    const auto size = std::filesystem::file_size(file, error);
    if (error || !size || size > 256 * 1024) return false;
    std::ifstream input(file);
    return input.is_open() && ValidTimeCycle(input);
}
} // namespace theft4::mods

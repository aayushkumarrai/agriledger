#include "photos.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace e4 {
namespace fs = std::filesystem;

std::optional<std::string> sniff_image_extension(const std::string& b) {
    if (b.size() >= 3 && static_cast<unsigned char>(b[0]) == 0xFF && static_cast<unsigned char>(b[1]) == 0xD8 &&
        static_cast<unsigned char>(b[2]) == 0xFF)
        return "jpg";
    static const unsigned char png[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    if (b.size() >= 8 && std::equal(png, png + 8, reinterpret_cast<const unsigned char*>(b.data()))) return "png";
    return std::nullopt;
}

bool valid_photo_name(const std::string& name) {
    const auto dot = name.find('.');
    if (dot != 64) return false;
    const std::string ext = name.substr(65);
    if (ext != "jpg" && ext != "png") return false;
    return std::all_of(name.begin(), name.begin() + 64, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

bool store_photo(const std::string& dir, const std::string& sha, const std::string& ext, const std::string& bytes) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) return false;
    const fs::path final_path = fs::path(dir) / (sha + "." + ext);
    if (fs::exists(final_path, ec)) return true;   // same content, same name
    const fs::path tmp = fs::path(dir) / ("." + sha + ".tmp");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out) return false;
    }
    fs::permissions(tmp, fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read, ec);
    fs::rename(tmp, final_path, ec);
    return !ec;
}

std::optional<std::string> read_photo(const std::string& dir, const std::string& name) {
    if (!valid_photo_name(name)) return std::nullopt;
    std::ifstream in(fs::path(dir) / name, std::ios::binary);
    if (!in) return std::nullopt;
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

}  // namespace e4

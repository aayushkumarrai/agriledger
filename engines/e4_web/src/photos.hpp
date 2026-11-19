// Lot photos: type checked by content, stored under a content-addressed name, served only to logged-in users.
#pragma once
#include <optional>
#include <string>

namespace e4 {

// "jpg" or "png" if the bytes start with a JPEG or PNG signature; nothing else is accepted, whatever the client says.
std::optional<std::string> sniff_image_extension(const std::string& bytes);

// photo_dir/<sha256>.<ext>; the client's file name is never used. Returns false if the file could not be written.
bool store_photo(const std::string& photo_dir, const std::string& sha256_hex, const std::string& ext, const std::string& bytes);

// <64 lowercase hex>.jpg or .png, nothing else (no slashes, no dots elsewhere): prevents path traversal.
bool valid_photo_name(const std::string& name);

std::optional<std::string> read_photo(const std::string& photo_dir, const std::string& name);

}  // namespace e4

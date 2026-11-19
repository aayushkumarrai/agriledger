#include <sodium.h>

#include <stdexcept>

#include "support.hpp"

namespace e4 {

PasswordHasher::PasswordHasher(unsigned long long opslimit, std::size_t memlimit)
    : ops_(opslimit ? opslimit : crypto_pwhash_OPSLIMIT_INTERACTIVE),
      mem_(memlimit ? memlimit : crypto_pwhash_MEMLIMIT_INTERACTIVE) {
    if (sodium_init() < 0) throw std::runtime_error("libsodium could not be initialised");
    dummy_ = hash("dummy-password-for-timing");
}

std::string PasswordHasher::hash(const std::string& password) const {
    char out[crypto_pwhash_STRBYTES];
    // crypto_pwhash_str uses argon2id; the string carries its own salt and parameters
    if (crypto_pwhash_str(out, password.c_str(), password.size(), ops_, mem_) != 0)
        throw std::runtime_error("password hashing ran out of memory");
    return out;
}

bool PasswordHasher::verify(const std::string& stored, const std::string& password) const {
    return crypto_pwhash_str_verify(stored.c_str(), password.c_str(), password.size()) == 0;
}

void PasswordHasher::burn_time(const std::string& password) const { (void)verify(dummy_, password); }

std::string new_session_token() {
    unsigned char raw[32];
    randombytes_buf(raw, sizeof raw);
    char hex[65];
    sodium_bin2hex(hex, sizeof hex, raw, sizeof raw);
    sodium_memzero(raw, sizeof raw);
    return hex;
}

}  // namespace e4

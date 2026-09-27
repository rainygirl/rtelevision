// AES-128 in CBC mode, decryption only.
//
// HLS encrypts segments with AES-128-CBC (#EXT-X-KEY:METHOD=AES-128). Players
// normally do this themselves; the relay needs it when it is feeding a player
// that cannot read HLS at all and therefore never sees the key.
//
// Small and portable on purpose: the core may not call a platform crypto
// library, and one cipher in one mode is all this needs.
#pragma once

#include <string>

namespace tv {

// `key` and `iv` are 16 bytes each; `data` is decrypted in place and its PKCS#7
// padding removed. False when the sizes are wrong or the padding is not valid,
// in which case `data` is left unusable.
bool aes128CbcDecrypt(const std::string& key, const std::string& iv, std::string& data);

}  // namespace tv

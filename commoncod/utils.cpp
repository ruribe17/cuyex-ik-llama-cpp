// commoncod/utils.cpp
// Implementación de utilidades para ik_llama_backend

#include "commoncod/utils.h"
#include <base64.hpp> // Librería externa
#include "llama.h"
#include <sstream>
#include <iomanip>
#include "common.h"


// ✅ CORREGIDO: Llama a la librería externa, no a sí mismo
std::vector<uint8_t> base64_decode(const std::string& input) {
    std::string decoded = base64::decode(input);
    return std::vector<uint8_t>(decoded.begin(), decoded.end());
}

std::string tokens_to_str(void* ctx, const std::vector<llama_token>& tokens) {
    std::string ret;
    for (const auto& token : tokens) {
        ret += common_token_to_piece((llama_context*)ctx, token);
    }
    return ret;
}

std::string tokens_to_output_formatted_string(void* ctx, llama_token token) {
    std::string out = token == -1 ? "" : common_token_to_piece((llama_context*)ctx, token);
    if (out.size() == 1 && (out[0] & 0x80) == 0x80) {
        std::stringstream ss;
        ss << std::hex << (out[0] & 0xff);
        std::string res(ss.str());
        out = "byte: \\x" + res;
    }
    return out;
}

size_t find_partial_stop_string(const std::string& stop, const std::string& text) {
    if (!text.empty() && !stop.empty()) {
        const char text_last_char = text.back();
        for (int64_t char_index = stop.size() - 1; char_index >= 0; char_index--) {
            if (stop[char_index] == text_last_char) {
                const std::string current_partial = stop.substr(0, char_index + 1);
                if (ends_with(text, current_partial)) {
                    return text.size() - char_index - 1;
                }
            }
        }
    }
    return std::string::npos;
}

bool ends_with(const std::string& str, const std::string& suffix) {
    return str.size() >= suffix.size() && 0 == str.compare(str.size() - suffix.size(), suffix.size(), suffix);
}

size_t common_part(const std::vector<llama_token>& a, const std::vector<llama_token>& b) {
    size_t i;
    for (i = 0; i < a.size() && i < b.size() && a[i] == b[i]; i++) {}
    return i;
}

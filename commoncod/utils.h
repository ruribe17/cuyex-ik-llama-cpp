// commoncod/utils.h
// Utilidades comunes para ik_llama_backend

#pragma once
#include "llama.h"
#include <string>
#include <vector>
#include <regex>
#include <filesystem>

// Decode base64 string
std::vector<uint8_t> base64_decode(const std::string& input);

// Convert tokens to string
std::string tokens_to_str(void* ctx, const std::vector<llama_token>& tokens);

// Format incomplete UTF-8 character
std::string tokens_to_output_formatted_string(void* ctx, llama_token token);

// Find partial stop string
size_t find_partial_stop_string(const std::string& stop, const std::string& text);

// Check if string ends with suffix
bool ends_with(const std::string& str, const std::string& suffix);

// Common part of two token vectors
size_t common_part(const std::vector<llama_token>& a, const std::vector<llama_token>& b);

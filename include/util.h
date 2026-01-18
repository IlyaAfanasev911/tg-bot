#pragma once

#include <cstddef>
#include <string>
#include <vector>

std::string getenv_or(const char* key, const std::string& def);
std::string random_token(std::size_t len = 32);

std::string trim(const std::string& s);
bool starts_with(const std::string& s, const std::string& prefix);
std::vector<std::string> split_ws(const std::string& s);
std::vector<std::string> split_by(const std::string& s, char delim);

#pragma once

#include <string>
#include <unordered_map>
#include <vector>

struct opts_main_t {
    std::string frontend;
    std::string frontend_path;
    int socket;
};

struct opts_msgr_t {
    std::string command;
    std::unordered_map<int, std::string> metadata;
};

inline const std::string laplace_socket_path = "/tmp/laplace.sock";
inline const std::string env_var_socket = "LAPLACE_FD";

void main_mode(const opts_main_t& opts);
void msgr_mode(const opts_msgr_t& opts);

struct metadata_t {
    std::string command;
    std::unordered_map<std::string, std::vector<std::string>> body;
};
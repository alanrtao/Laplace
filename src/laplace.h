#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <unordered_set>

struct opts_main_t {
    std::string frontend;
    std::string frontend_path;
    int socket;
};

struct opts_msgr_t {
    std::string command;
    std::unordered_map<int, std::string> metadata;
    std::unordered_set<std::string> ignores;

    opts_msgr_t(int argc, char** argv);
};

inline const std::string laplace_socket_path = "/tmp/laplace.sock";
inline const std::string env_var_socket = "LAPLACE_FD";

void main_mode(const opts_main_t& opts);
void msgr_mode(const opts_msgr_t& opts);

struct metadata_t {
    std::vector<std::string> commands;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> body;
};

struct client_req_t {
    std::string endpoint;
    std::string params;
};
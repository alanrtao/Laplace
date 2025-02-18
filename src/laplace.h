#pragma once

#include <string>

struct opts_main_t {
    std::string frontend;
    std::string frontend_path;
    int socket;
};

struct opts_msgr_t {
    std::string command;
};

inline const std::string laplace_socket_path = "/tmp/laplace.sock";
inline const std::string env_var_socket = "LAPLACE_FD";

void main_mode(const opts_main_t& opts);
void msgr_mode(const opts_msgr_t& opts);

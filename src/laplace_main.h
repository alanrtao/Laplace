#pragma once

#include <expected>

#include "laplace.h"

struct shell_t {
    pid_t pid;
};

std::expected<shell_t, std::string> launch_shell(const opts_main_t& shell) noexcept;
std::expected<void, std::string> manage_shell(const shell_t& shell) noexcept;

void web_socket_server();
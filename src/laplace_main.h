#pragma once

#include <expected>

#include "laplace.h"

struct shell_t {
    pid_t pid;
};

std::expected<void, std::string> manage_shell(const opts_main_t& opts) noexcept;

void web_socket_server();
void shell_listener(int sock_fd);
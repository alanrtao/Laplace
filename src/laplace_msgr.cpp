#include <sstream>

#include "unistd.h"
#include "sys/socket.h"
#include "sys/un.h"

#include <glaze/json.hpp>

#include "laplace_msgr.h"
#include "utils.h"

std::unordered_map<std::string, std::string> parse_environ() noexcept {

    std::unordered_map<std::string, std::string> env {};
    for (size_t i = 0; environ[i] != nullptr; ++i) {
        const std::string line(environ[i]);
        const auto split = line.find('=');
        env.emplace(
            // key
            line.substr(0, split),
            // value
            line.substr(split+1)
        );
    }

    return env;
}

std::expected<void, std::string> msg(
    const opts_msgr_t& opts,
    const std::unordered_map<std::string, std::string>& env) noexcept
{
    const auto json = glz::write_json(env);
    throwif(json);

    int sock = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (sock < 0) { throw("Open socket: " + ERRNO); }
    defer([sock]{ close(sock); });
    
    sockaddr_un name {};
    name.sun_family = AF_UNIX;
    strncpy(name.sun_path, laplace_socket_path.c_str(), laplace_socket_path.size() + 1);

    if (sendto(sock, json->c_str(), json->size() + 1, 0, (sockaddr*) &name, sizeof(struct sockaddr_un)) < 0) {
        throw("Send message: " + ERRNO);
    }

    return {};
}

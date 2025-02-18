#include <sstream>
#include <iostream>

#include <dirent.h>
#include "unistd.h"
#include "sys/socket.h"
#include "sys/un.h"

#include <glaze/json.hpp>

#include "laplace_msgr.h"
#include "utils.h"


std::expected<void, std::string> msg(
    const opts_msgr_t& opts) noexcept
{
    std::unordered_map<std::string, std::string> metadata {};
    metadata.reserve(opts.metadata.size());
    for (const auto& [fd, metadata_type] : opts.metadata) {
        metadata[metadata_type] = read_file_descriptor(fd);
    }

    const auto _json = glz::merge{ 
        std::unordered_map<std::string, std::string> {{"command", opts.command}},
        metadata
    };

    const auto json = glz::write_json(_json);
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

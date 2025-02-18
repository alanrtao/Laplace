#include <sstream>
#include <iostream>
#include <ranges>
#include <algorithm>

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
    metadata_t metadata {
        .command = opts.command
    };
    metadata.body.reserve(opts.metadata.size());

    for (const auto& [fd, metadata_type] : opts.metadata) {
        const auto raw = read_file_descriptor(fd);
        for (const auto& item : raw | std::ranges::views::split('\0')) {
            const auto item_str = std::string(item.begin(), item.end());
            
            for (const auto& ignore : opts.ignores) {
                if (item_str.rfind(ignore, 0) == 0) {
                    goto ignore_item;
                }
            }

            if (item_str.size() > 0) {
                metadata.body[metadata_type].push_back(item_str);
            }
            ignore_item: continue;
        }
    }

    const auto json = glz::write_json(metadata);
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

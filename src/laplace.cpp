#include <iostream>

#include "laplace.h"
#include "laplace_main.h"
#include "laplace_msgr.h"

void main_mode(const opts_main_t &opts)
{
    const auto result = launch_shell(opts).and_then(manage_shell);
    if (!result) {
        std::cerr << result.error() << std::endl;
    }
}

void msgr_mode(const opts_msgr_t &opts)
{
    const auto result = msg(opts);
    if (!result) {
        std::cerr << result.error() << std::endl;
    }
}

inline std::string __msgr_help = "Messenger usage: [-i <name>]... [<fd> <metadata type>]...\n";

opts_msgr_t::opts_msgr_t(int argc, char **argv)
{
    command = std::string(std::istreambuf_iterator<char>(std::cin), {});

    int i = 1;
    while (i < argc) {
        const auto ignore_flag = std::string(argv[i++]);
        if (ignore_flag != "-i") {
            --i;
            break;
        }
        if (i == argc) {
            throw std::runtime_error(__msgr_help);
        }
        ignores.emplace(std::string(argv[i++]));
    }

    metadata.reserve(argc / 2);

    while (i < argc) {
        try {            
            const auto k = std::stoi(argv[i++]);

            if (i == argc) {
                throw std::runtime_error(__msgr_help);
            }

            const auto v = std::string(argv[i++]);
            metadata[k] = v;
        } catch (...) {
            throw std::runtime_error(__msgr_help);
        }
    }
}

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
    const auto env = parse_environ();
    const auto result = msg(opts, env);
    if (!result) {
        std::cerr << result.error() << std::endl;
    }
}

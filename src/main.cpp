#include <iostream>
#include <algorithm>

#include <sys/poll.h>

#include "laplace.h"

bool stdin_populated();

int main(int argc, char *argv[])
{
    if (stdin_populated())
    {   
        msgr_mode(opts_msgr_t(argc, argv));
    }
    else
    {
        std::vector<std::string> argv_for_shell {};
        for (int i = 2; i < argc; ++i) {
            std::cout << argv[i] << std::endl;
            argv_for_shell.push_back(argv[i]);
        }

        const std::string shell(argv[1]);
        main_mode({
            .frontend=shell,
            .frontend_path="/bin/"+shell,
            .argv = argv_for_shell,
        });
    }
}

bool stdin_populated()
{
    struct pollfd fds;
    fds.fd = 0;
    fds.events = POLLIN;
    return poll(&fds, 1, 0) > 0;
}
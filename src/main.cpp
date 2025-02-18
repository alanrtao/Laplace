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
        const std::string shell(argv[1]);
        main_mode({
            .frontend=shell,
            .frontend_path="/bin/"+shell,
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
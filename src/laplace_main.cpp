#include <iostream>
#include <filesystem>
#include <thread>

#include "unistd.h"
#include "sys/socket.h"
#include "sys/un.h"
#include "sys/wait.h"

#include "git2.h"

#include "laplace_main.h"
#include "utils.h"

std::expected<shell_t, std::string> launch_shell(const opts_main_t& opts) noexcept {
    const std::string adapter = "/adapter/" + opts.frontend; // TODO: refactor
    if (!std::filesystem::exists(adapter)) {
        throw("Adapter not found: " + adapter);
    }

    shell_t shell {};
    if (shell.pid = fork(); shell.pid < 0) {
        throw("Fork failure: " + ERRNO);
    }

    unlink(laplace_socket_path.c_str());

    // on child process, switch to shell
    if (shell.pid == 0) {
        // TODO: look into zsh params
        auto cmd = const_cast<char*>(opts.frontend_path.c_str());
        char * const argv[] = { 
            cmd,
            const_cast<char*>("--rcfile"), const_cast<char*>(adapter.c_str()), 
            NULL
        };

        // std::string env_laplace_fd = "LAPLACE_FD=" + std::to_string(opts.socket);
        // char * const envp[] = {
        //     const_cast<char*>(env_laplace_fd.c_str()),
        //     NULL
        // };

        execve(opts.frontend_path.c_str(), argv, NULL); 

        throw("Execute shell failure: " + ERRNO);
    }

    return shell;
}


std::expected<void, std::string> manage_shell(const shell_t& shell) noexcept {
   
    defer([&shell]{ kill(shell.pid, SIGKILL); });

    auto t_wait = std::thread([&shell](){
        while(waitpid(shell.pid, NULL, 0) != shell.pid);
        raise(SIGINT);
    });

    /* Create socket from which to read. */
    int sock_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (sock_fd < 0) { throw("Open socket: " + ERRNO); }

    defer([sock_fd]{ close(sock_fd); });

    if (const int status = git_libgit2_init(); status < 0) {
        throw("Failed to initialize libgit2" + std::to_string(status));
    } 
    else { defer([]{ git_libgit2_shutdown(); }); }

    /* Create name. */
    sockaddr_un name {};
    name.sun_family = AF_UNIX;
    strncpy(name.sun_path, laplace_socket_path.c_str(), laplace_socket_path.size() + 1);

    /* Bind the UNIX domain address to the created socket */
    if (bind(sock_fd, (struct sockaddr *) &name, sizeof(sockaddr_un)) < 0) {
        throw("Bind socket: " + ERRNO);
    }

    while (true) {
        std::array<char, 1 * 1024 * 1024> buf {};

        int n = recvfrom(sock_fd, buf.data(), 1024, 0, NULL, NULL);
        if (n < 0) { std::cerr << "recvfrom" + ERRNO << "\n"; }
        else if (n > 0) {
            std::cout<<"Received: "<<std::string(buf.begin(), buf.begin() + n - 1);
        }


    }

    return {};
}

#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <git2.h>
#include "utils.h"

pid_t launch_shell(const char* shell);

int main(int argc, char* argv[]) {
    const pid_t shell = launch_shell("bash");
    if (git_libgit2_init() < 0) {
        std::cerr << "Failed to initialize libgit2" << std::endl;
        return 1;
    }
    defer([]{ git_libgit2_shutdown(); });
    while (waitpid(shell, NULL, 0) != shell);
    return 0;
}

pid_t launch_shell(const char* shell) {
    const pid_t proc = fork();
    if (proc == 0) {
        execlp("/bin/bash", "/bin/bash", "--rcfile", ("/adapter/"+std::string(shell)).c_str(), NULL);
        exit(0);
    }
    return proc;
}

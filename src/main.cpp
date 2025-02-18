#include <iostream>
#include <algorithm>

#include <sys/poll.h>

#include <cxxopts.hpp>

#include "laplace.h"

bool stdin_populated();

int main(int argc, char *argv[])
{
    cxxopts::Options opts("Laplace", 
        "Shell wrapper with change monitoring and logging.\n"
        "Usage:\n"
        "  - To run managed shell, start with empty STDIN and pass in options from the `main` category\n"
        "  - To message shell manager, star with non-empty STDIN (ex. `echo 123 | laplace <options...>`)\n"
        "    and pass in arguments as ordered in the `msgr` category\n"
        "See more at <GIT REPO>" // TODO: make git repo and set this
    );

    opts.add_option("", { "h,help", "Print usage instructions" });

    const std::initializer_list<cxxopts::Option> main_opts {
        {
            "s,shell", "Wrapped shell type: `bash`, `zsh`, defaults to `bash`", 
            cxxopts::value<std::string>()->default_value("bash")
        },
    };
    opts.add_options("main", main_opts);

    if (stdin_populated())
    {
        std::string input(std::istreambuf_iterator<char>(std::cin), {});

        std::unordered_map<int, std::string> metadata {};
        metadata.reserve(argc / 2);

        for (int i = 1; i < argc;) {
            try {            
                const auto k = std::stoi(argv[i++]);

                if (i == argc) {
                    std::cerr<<"Messenger arguments must alternate between fd (int) and metadata type!\n";
                    return 1;
                }

                const auto v = std::string(argv[i++]);
                metadata[k] = v;
            } catch (...) {
                std::cerr<<"Messenger arguments must alternate between fd (int) and metadata type!\n";
            }
        }

        msgr_mode({
            .command=input,
            .metadata=metadata
        });
    }
    else
    {
        const auto parsed = opts.parse(argc, argv);

        if (parsed.contains("help")) {
            std::cout << opts.help();
            return 0;
        }

        const auto shell = parsed["shell"].as<std::string>();
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
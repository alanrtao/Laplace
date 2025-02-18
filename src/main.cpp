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

    const std::initializer_list<cxxopts::Option> msgr_opts {
        { "c,command", "Bash command that was run", cxxopts::value<std::string>() }
    };
    opts.add_options("msgr", msgr_opts);

    const bool use_msgr_mode = argv[1];// stdin_populated(); // only engage in messaging mode if thre is data to be read in STDIN
    std::vector<std::string> msgr_opts_names (msgr_opts.size());
    if (use_msgr_mode) {
        std::transform(msgr_opts.begin(), msgr_opts.end(), msgr_opts_names.begin(), [](const cxxopts::Option& o) { return o.opts_.substr(2); });
        opts.parse_positional(msgr_opts_names);
    }

    const auto parsed = opts.parse(argc, argv);

    if (parsed.contains("help")) {
        std::cout << opts.help();
        return 0;
    }

    if (use_msgr_mode)
    {
        for (const auto& name : msgr_opts_names) {
            if (!parsed.contains(name)) {
                std::cerr << "Argument `" << name << "` required!\n";
                return 1;
            }
        }

        msgr_mode({
            .command=parsed["command"].as<std::string>(),
        });
    }
    else
    {
        const auto shell = parsed["shell"].as<std::string>();
        main_mode({
            .frontend=shell,
            .frontend_path="/bin/"+shell,
            // .socket=parsed["descriptor"].as<int>(),
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
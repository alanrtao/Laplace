#include <iostream>
#include <filesystem>
#include <thread>

#include "unistd.h"
#include "sys/socket.h"
#include "sys/un.h"
#include "sys/wait.h"

#include "git2.h"
#include <glaze/json.hpp>
#include <ixwebsocket/IXWebSocketServer.h>

#include "laplace_main.h"
#include "utils.h"

void on_shell_msg(const std::string_view msg);
void on_client_msg(std::shared_ptr<ix::ConnectionState> connectionState, ix::WebSocket & webSocket, const ix::WebSocketMessagePtr & msg);

inline shell_t shell {};

std::expected<shell_t, std::string> launch_shell(const opts_main_t& opts) noexcept {
    const std::string adapter = "/adapter/" + opts.frontend; // TODO: refactor
    if (!std::filesystem::exists(adapter)) {
        throw("Adapter not found: " + adapter);
    }

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

        execve(opts.frontend_path.c_str(), argv, NULL); 

        throw("Execute shell failure: " + ERRNO);
    }

    return shell;
}

ix::WebSocketServer server(8008, "0.0.0.0");
std::mutex server_lock;

git_repository* repo;
std::optional<git_oid> current_guid = std::nullopt;
std::mutex data_lock;

std::expected<void, std::string> manage_shell(const shell_t& shell) noexcept {
    git_libgit2_init();
    defer([&shell]{ 
        kill(shell.pid, SIGKILL);
        git_libgit2_shutdown();
    });

    auto t_wait = std::thread([&shell](){
        while(waitpid(shell.pid, NULL, 0) != shell.pid);
        std::cerr << "Shell terminated\n";
        server.stop();
        std::exit(0);
    });

    auto t_wss = std::thread(web_socket_server);

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

    std::array<char, 1 * 1024 * 1024> buf {};
    while (true) {
        int n = recvfrom(sock_fd, buf.data(), 1024 * 1024, 0, NULL, NULL);
        if (n > 0) {
            on_shell_msg(std::string_view(buf.begin(), buf.begin() + n - 1));
        } else if (n < 0) { 
            std::cerr << "recvfrom" + ERRNO << "\n";
        }
    }

    return {};
}

void web_socket_server()
{   
    ix::SocketTLSOptions tls_opts {};
    tls_opts.tls = false;
    tls_opts.disable_hostname_validation = true;

    {
        std::lock_guard<std::mutex> _(server_lock);

        server.setTLSOptions(tls_opts);
        server.setOnClientMessageCallback(on_client_msg);
        
        const auto [success, error] = server.listen();
        if (!success)
        {
            // Error handling
            std::cerr << error << std::endl;
            return;
        }

        server.start();
    }

    server.wait();
}


void on_shell_msg(const std::string_view msg) {
    std::lock_guard<std::mutex> lock(server_lock);
    glz::error_ctx json_err;
    
    metadata_t update;
    
    json_err = glz::read_json(update, msg);
    if (json_err) {
        std::cerr << json_err.custom_error_message << "\n";
        return;
    }

    // subcommand message does not contain state information
    if (update.body.size() == 0) {
        if (!current_guid.has_value()) {
            std::cerr << "must not send subcommand updates";
            return;
        }
        // just append to current metadata
        for (const auto& cmd : update.commands) {
            metadata_history[*current_guid].commands.push_back(cmd);
        }
    }
    // command message contains at least the skeleton of the state,
    // which will always contain some entries.
    else {
        // for first ever message, create a repository
        if (!current_guid.has_value()) {
            git_repository_init(&repo, "/__laplace", false);
        }
        
        // TODO: create git commit and use its id as guid

        git_commit* curr;
        git_commit_lookup(&curr, repo, );

        metadata_history[*current_oid] = update;
    }

    for (const auto& c : server.getClients()) {
        c->send(std::string(msg));
    }
}

// Client API
void on_client_msg_jump(const std::string& guid);

void on_client_msg(std::shared_ptr<ix::ConnectionState> connectionState, ix::WebSocket& ws, const ix::WebSocketMessagePtr& msg) {
    // The ConnectionState object contains information about the connection,
    // at this point only the client ip address and the port.
    // std::cout << "Remote ip: " << connectionState->getRemoteIp() << std::endl;

    if (msg->type == ix::WebSocketMessageType::Open)
    {
        // std::cout << "New connection" << std::endl;

        // // A connection state object is available, and has a default id
        // // You can subclass ConnectionState and pass an alternate factory
        // // to override it. It is useful if you want to store custom
        // // attributes per connection (authenticated bool flag, attributes, etc...)
        // std::cout << "id: " << connectionState->getId() << std::endl;

        // // The uri the client did connect to.
        // std::cout << "Uri: " << msg->openInfo.uri << std::endl;

        // std::cout << "Headers:" << std::endl;
        // for (auto it : msg->openInfo.headers)
        // {
        //     std::cout << "\t" << it.first << ": " << it.second << std::endl;
        // }
    }
    else if (msg->type == ix::WebSocketMessageType::Message)
    {
        const auto& uri = msg->openInfo.uri; // todo: distinguish URI path
        const std::lock_guard<std::mutex> lock(data_lock);

        client_req_t req;
        auto err = glz::read_json<client_req_t>(req, msg->str);
        if (err) {
            std::cerr << err.custom_error_message << "\n";
            return;
        }

        if (req.endpoint == "fetch") {
            auto json = glz::write_json(metadata_history);
            // auto req = glz::read_json<req_t>(msg->str);
            if (json.has_value()) {
                ws.send(*json);
            }
        }
        else if (req.endpoint == "jump") {
            on_client_msg_jump(req.params);
        }
        
    }
}

void on_client_msg_jump(const git_oid& oid) {
    std::lock_guard<std::mutex> lock(data_lock);

    git_commit* commit;
    int err;
    err = git_commit_lookup(&commit, repo, &oid);
    if (err) {
        std::cerr << "git_commit_lookup " << std::to_string(err) << std::endl;
        return;
    }

    err = git_reset(repo, (git_object*) commit, GIT_RESET_HARD, NULL);
    if (err) {
        std::cerr << "git_reset (hard) " << std::to_string(err) << std::endl;
        return;
    }

    

    // TODO: adapt this to libgit2
    // // write each metadata type into a single temp file
    // for (const auto& [k, v] : md.body) {
    //     std::string fname = "/tmp/__laplace_" + k;
    //     int fd = open(fname.c_str(), O_WRONLY);
    //     for (const auto& [item_k, item_v] : v) {                
    //         write(fd, item_v.c_str(), item_v.size() + 1);
    //     }
    //     close(fd);
    // }
    
    kill(shell.pid, SIGUSR1);
}
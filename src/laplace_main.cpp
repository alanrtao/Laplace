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

void append_msg_to_current_commit(const metadata_t& msg);
void serialize_metadata(const metadata_t& msg);
git_commit* create_new_commit();
void add_commit_to_tree(git_commit* commit);

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

git_repository* repo = NULL;
std::unordered_map<git_reference*, std::vector<git_commit*>> branches {};
std::optional<git_reference*> current_branch = std::nullopt;

std::mutex data_lock;

std::expected<void, std::string> manage_shell(const shell_t& shell) noexcept {
    git_libgit2_init();
    defer([&shell]{ 
        kill(shell.pid, SIGKILL);
        for (auto& [_, b] : branches) {
            for (auto& commit : b) {
                git_commit_free(commit);
            }
        }
        git_repository_free(repo);
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
        append_msg_to_current_commit();
    }
    // command message contains at least the skeleton of the state,
    // which will always contain some entries.
    else {
        // for first ever message, create a repository
        if (!repo) {
            git_repository_init(&repo, "/__laplace", false);
        }
        
        // TODO: create git commit and use its id as guid
        git_oid oid;
        git_signature* sig;
        git_signature_now(&sig, "_", "_@laplace.com");
        
        git_index *index;
        git_oid tree_id;
        git_tree *tree;
        git_repository_index(&index, repo);
        git_index_add_all(index, NULL, 0, NULL, NULL);
        git_index_write_tree(&tree_id, index);

        git_index_free(index);

        // https://libgit2.org/docs/examples/init/
        git_commit_create_v(&oid, repo, "HEAD", sig, sig, NULL, "", tree, 0);
        std::cout << "Create new checkpoint commit " << std::string((const char* ) oid.id, 20) << std::endl;

        git_signature_free(sig);
    }

    for (const auto& c : server.getClients()) {
        c->send(std::string(msg));
    }
}

void append_msg_to_current_commit(const metadata_t &msg)
{
    if (!repo || !current_branch.has_value()) {
        std::cerr << "must not send subcommand updates";
        return;
    }
    // append subcommand to commit message of guid
    auto& branch = branches[*current_branch];
    auto& curr = branch.back();
    std::string curr_commit_msg(git_commit_message(curr));
    for (const auto& cmd : msg.commands) {
        curr_commit_msg += cmd;
    }
    git_oid oid;
    git_commit_amend(&oid, curr, NULL, NULL, NULL, NULL, curr_commit_msg.c_str(), NULL);
    std::cout << "Amend subcommand to " << std::string((const char* ) oid.id, 20) << std::endl;
}

void serialize_metadata(const metadata_t & msg)
{
    for (const auto& [k, v] : msg.body) {
        std::string fname = "/__laplace_" + k;
        int fd = open(fname.c_str(), O_WRONLY);
        for (const auto& [item_k, item_v] : v) {                
            write(fd, item_v.c_str(), item_v.size() + 1);
        }
        close(fd);
    }
}

git_commit *create_new_commit()
{
    return nullptr;
}

git_tree* create_tree() {

}

void add_commit_to_tree(git_commit *commit)
{
}

// Client API
void on_client_msg_jump(const git_oid& guid);

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
            auto json = glz::write_json();
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
    int err;

    git_commit* commit;

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

    git_repository_set_head(repo, );

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
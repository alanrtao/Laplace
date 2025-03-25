#include <iostream>
#include <filesystem>
#include <thread>
#include <stacktrace>

#include "unistd.h"
#include "sys/socket.h"
#include "sys/un.h"
#include "sys/wait.h"
#include <sys/sendfile.h>

#include "git2.h"
#include <glaze/json.hpp>
#include <ixwebsocket/IXWebSocketServer.h>

#include "laplace_main.h"
#include "utils.h"

ix::WebSocketServer server(8008, "0.0.0.0");
std::mutex server_lock;

struct laplace_git_note {
    std::string branch;
    std::string comments;
};

git_repository* repo = NULL;
git_revwalk* walk = NULL;
std::vector<std::string> subcommands {};

std::mutex data_lock;
inline shell_t shell {};

std::string oid_str(const git_oid& oid) {
    return std::string(git_oid_tostr_s(&oid));
}

git_oid str_oid(const std::string& str) {
    git_oid ret;
    git_oid_fromstr(&ret, str.c_str());
    return ret;
}

std::string ref_str(git_reference* ref) {
    return git_reference_name(ref);
}

std::expected<git_commit*, int> get_head_commit();
std::expected<std::unordered_map<std::string, std::string>, int> get_all_branch_tips();
inline static const char* git_note_namespace = "refs/notes/laplace";
std::expected<laplace_git_note, int> get_commit_note(const git_oid* oid);
std::expected<void, int> set_commit_note(const git_oid* oid, const laplace_git_note& note);
std::expected<std::string, int> render_mermaid();

std::expected<void, int> on_shell_msg(const std::string_view msg);
std::expected<void, int> on_client_msg(std::shared_ptr<ix::ConnectionState> connectionState, ix::WebSocket & webSocket, const ix::WebSocketMessagePtr & msg);

std::expected<void, int> serialize_metadata(const metadata_t& msg);
std::expected<void, int> apply_serialized_metadata();
std::expected<git_commit*, int> create_new_commit(git_commit* parent, git_reference* branch_ref);

// simplified from https://github.com/libgit2/libgit2/blob/21a351b0ed207d0871cb23e09c027d1ee42eae98/examples/common.c#L23
void print_lg2_err(int error, const std::string& msg)
{
	const git_error *lg2err;
	if ((lg2err = git_error_last()) != NULL && lg2err->message != NULL) {
		std::cerr<<msg<<" - "<<lg2err->message<<std::endl;
        // std::cerr<<std::stacktrace::current()<< std::endl;
	}
}
#define lg2(e, msg) { if (auto __v = (e); __v) { print_lg2_err(__v, (msg)); return std::unexpected(__v); } }

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

std::expected<void, std::string> manage_shell(const shell_t& shell) noexcept {
    git_libgit2_init();
    git_libgit2_opts(GIT_OPT_SET_OWNER_VALIDATION, 0);
    defer([&shell]{
        kill(shell.pid, SIGKILL);
        git_revwalk_free(walk);
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

std::expected<void, int> init_repo(const char* repo_path) {
    lg2(git_repository_init(&repo, repo_path, 0), "create repository");
    create_new_commit(NULL, NULL).and_then([](git_commit* commit) -> std::expected<void, int> {
        git_reference* branch;
        lg2(git_branch_create(&branch, repo, "main", commit, 0), "git branch create");
        lg2(git_repository_set_head(repo, "refs/heads/main"), "git repository set head"); // refs/heads is automatically used
        git_reference_free(branch);
        return {};
    }); // head at new commit
}

std::expected<git_commit*, int> get_head_commit() {
    git_oid oid;
    git_commit *commit = NULL;

    lg2(git_reference_name_to_id(&oid, repo, "HEAD"), "lookup head reference");
    lg2(git_commit_lookup(&commit, repo, &oid), "lookup head commit");

    return commit;
}

std::expected<std::unordered_map<std::string, std::string>, int> get_all_branch_tips()
{
    std::unordered_map<std::string, std::string> branch_tips {};
    git_branch_iterator* i;
    lg2(git_branch_iterator_new(&i, repo, GIT_BRANCH_LOCAL), "branch iterator new");
    defer([i]{
        git_branch_iterator_free(i);
    });

    git_reference *curr_ref;
    git_branch_t curr_type;
    while(git_branch_next(&curr_ref, &curr_type, i) != GIT_ITEROVER) {
        defer([curr_ref]{ git_reference_free(curr_ref); });

        const git_oid* target = git_reference_target(curr_ref);
        if (!target) { continue; }
        branch_tips[ref_str(curr_ref)] = oid_str(*target);
    }

    return branch_tips;
}

std::expected<laplace_git_note, int> get_commit_note(const git_oid *commit)
{
   
    git_note *note_raw = NULL;
    lg2(git_note_read(&note_raw, repo, git_note_namespace, commit), "read note from commit");
    defer([note_raw]{git_note_free(note_raw);});

    laplace_git_note note;
    auto json_err = glz::read_json(note, git_note_message(note_raw));
    if (json_err) {
        std::cerr << json_err.custom_error_message << "\n";
        return std::unexpected(0);
    }

    return note;
}

std::expected<void, int> set_commit_note(const git_oid *oid, const laplace_git_note &note)
{
    auto json = glz::write_json(note);

    if (!json.has_value()) {
        std::cerr << json.error().custom_error_message << "\n";
        return std::unexpected(0);
    }

    git_signature* sig;
    lg2(git_signature_now(&sig, "_", "_@laplace.com"), "git_signature_now");
    defer([sig]{ git_signature_free(sig); });

    git_oid note_oid;
    lg2(git_note_create(&note_oid, repo, git_note_namespace, sig, sig, oid, json->c_str(), 1 /* force */), "create or update commit note");
}

std::expected<void, int> on_shell_msg(const std::string_view msg) {
    std::lock_guard<std::mutex> lock(server_lock);
    glz::error_ctx json_err;
    int err;

    metadata_t update;

    json_err = glz::read_json(update, msg);
    if (json_err) {
        std::cerr << json_err.custom_error_message << "\n";
        return std::unexpected(0);
    }

    // subcommand message does not contain state information
    if (update.body.size() == 0) {
        for (auto& cmd : update.commands) {
            subcommands.push_back(cmd);
        }
    }
    // command message contains at least the skeleton of the state,
    // which will always contain some entries.
    else {
        serialize_metadata(update);

        // case 1: no branches at all, starting from a blank repository
        if (repo != NULL) {

            const char* repo_path = "/__laplace";

            if (git_repository_open(&repo, repo_path) == 0) {
                std::cout << "Existing records found, scanning...\n";
                // TODO: determine if there actually is any scanning needed
            } else {
                std::cout << "No existing records, creating new repository...\n";
                init_repo(repo_path);
            }

            lg2(git_revwalk_new(&walk, repo), "create revision walker");
            lg2(git_revwalk_sorting(walk, GIT_SORT_TOPOLOGICAL), "git revwalk sorting");
        }
        // case 2: branches exist, but user jumped back to prior commit, causing a detached HEAD
        else if (git_repository_head_detached(repo)) {

            // git_repository_head(, repo);
            // git_commit* commit = create_new_commit(NULL);
            // git_reference* branch;
            // git_branch_create(&branch, repo, ("b" + std::to_string(branches.size())).c_str(), commit, 0);
            // branches[branch] = { commit };
        }
        // case 3: branch exists and is currently the HEAD
        else {
            git_reference* branch_head_ref;

            get_head_commit().and_then([](git_commit* parent) {
                defer([parent]{
                    git_commit_free(parent);
                });

                auto new_commit = create_new_commit(parent, branch_head_ref);
                if (!new_commit.has_value()) {
                    return std::unexpected(new_commit.error());
                }
            });
        }
    }

    for (const auto& c : server.getClients()) {
        c->send(std::string(msg));
    }

    return {};
}

std::expected<void, int> serialize_metadata(const metadata_t & msg)
{
    // - __laplace
    // |---- k1
    // |-------- v1
    // |-------- v2
    // |-------- v3
    // |---- k2
    // ...

    for (const auto& [k, vs] : msg.body) {
        std::string d_k = "/__laplace/" + k;
        mkdir(d_k.c_str(), 0777);

        for (const auto& [v, val] : vs) {
            std::string f_v = d_k + "/" + v;
            
            int fd_v = open(f_v.c_str(), O_CREAT|O_WRONLY, 0666);
            if (write(fd_v, val.c_str(), val.size()) < 0) {
                std::string err = "write to " + f_v;
                perror(err.c_str());
            }

            close(fd_v);
        }
    }

    return {};
}

std::expected<void, int> apply_serialized_metadata()
{
    // - __laplace
    // |---- k1
    // |-------- v1
    // |-------- v2
    // |-------- v3
    // |---- k2
    // ...

    // each of k will be written to one tempfile called /tmp/__laplace_{k}
    // containing v1, v2, ... separated by \0

    // write each metadata type into a single temp file
    for (auto& k_ent : std::filesystem::directory_iterator("/__laplace")) {
        if (!k_ent.is_directory()) { continue; }

        auto k = k_ent.path().filename().string();

        // skip hidden folders such as .git
        if (k[0] == '.') {
            continue;
        }

        std::string f_dst = "/tmp/__laplace_" + k;
        int fd_dst = open(f_dst.c_str(), O_WRONLY | O_APPEND);

        for (auto& v_ent : std::filesystem::directory_iterator(k_ent.path()) ) {
            auto v = v_ent.path();
            int fd_src = open(v.c_str(), O_RDONLY);
            struct stat stat_buf;
            fstat(fd_src,&stat_buf);

            sendfile(fd_dst, fd_src, NULL, stat_buf.st_size);

            close(fd_src);
            write(fd_dst, "\0", 1);
        }

        close(fd_dst);
    }

    kill(shell.pid, SIGUSR1);

    return {};
}

std::expected<git_commit*, int> create_new_commit(git_commit* parent, git_reference* branch_ref)
{
    int err;

    git_oid oid;
    git_signature* sig;
    lg2(git_signature_now(&sig, "_", "_@laplace.com"), "git_signature_now");

    git_index *index;
    git_oid tree_id;
    git_tree *tree;
    lg2(git_repository_index(&index, repo), "git_repository_index");
    lg2(git_index_add_all(index, NULL, 0, NULL, NULL), "git_index_add_all");
    lg2(git_index_write_tree(&tree_id, index), "git_index_write_tree");
    lg2(git_tree_lookup(&tree, repo, &tree_id), "look up actual tree obj from tree_id");
    git_index_free(index);

    std::string subcommands_str = "";
    for (const auto& subcommand : subcommands) {
        subcommands_str.append(subcommand);
        subcommands_str.append("\n");
    }

    // https://libgit2.org/docs/examples/init/
    lg2(git_commit_create_v(&oid, repo, "HEAD", sig, sig, NULL, subcommands_str.c_str(), tree, 1, parent), "git_commit_create");
    // std::cout << "Create new checkpoint commit " << oid_str(oid) << std::endl;

    git_tree_free(tree);
    git_signature_free(sig);

    git_commit* ret;
    lg2(git_commit_lookup(&ret, repo, &oid), "commit lookup");

    // if (branch_ref) {
    //     std::string reflog_msg = "move forward branch " + std::string(git_reference_name(branch_ref)) + " to commit " + oid_str(oid);
    //     git_reference* new_branch_ref;
    //     lg2(git_reference_set_target(&new_branch_ref, branch_ref, &oid, reflog_msg.c_str()), "git_reference_set_target");
    //     git_reference_free(new_branch_ref);
    // }

    return {ret};
}

// Client API
std::expected<void, int> on_client_msg_jump(const git_oid& guid);
std::expected<void, int> on_client_msg_label_branch(const std::string& branch);
std::expected<void, int> on_client_msg_label_commit(const git_oid& commit);

std::expected<void, int> on_client_msg(std::shared_ptr<ix::ConnectionState> connectionState, ix::WebSocket& ws, const ix::WebSocketMessagePtr& msg) {
    if (msg->type == ix::WebSocketMessageType::Open) {}
    else if (msg->type == ix::WebSocketMessageType::Message)
    {
        const auto& uri = msg->openInfo.uri; // todo: distinguish URI path
        const std::lock_guard<std::mutex> lock(data_lock);

        client_req_t req;
        auto err = glz::read_json<client_req_t>(req, msg->str);
        if (err) {
            std::cerr << err.custom_error_message << "\n";
            return std::unexpected((int)err.ec);
        }

        if (req.endpoint == "fetch") {
            ws.send("received client fetch");
            // auto json = glz::write_json();
            // if (json.has_value()) {
            //     ws.send(*json);
            // }
            
            render_mermaid().and_then([&ws](const std::string& rendering) -> std::expected<void, int> {
                ws.send(rendering);
                return {};
            }).or_else([&ws](const int& error) {
                ws.send("ERROR " + std::to_string(error));
            });
        }
        else if (req.endpoint == "jump") {
            on_client_msg_jump(str_oid(req.params));
        }
        else if (req.endpoint == "relable branch") {
            
        }
        else if (req.endpoint == "relable commit") {

        }
    }

    return {};
}

std::expected<void, int> on_client_msg_jump(const git_oid& oid) {
    std::lock_guard<std::mutex> lock(data_lock);
    git_commit* commit;
    lg2(git_commit_lookup(&commit, repo, &oid), "git commit lookup");
    lg2(git_reset(repo, (git_object*) commit, GIT_RESET_HARD, NULL), "git reset");
    lg2(git_repository_set_head_detached(repo, &oid), "jump back to " + oid_str(oid));
    return apply_serialized_metadata();
}

std::expected<void, int> on_client_msg_label_branch(const std::string &branch)
{
    return {};
}

std::expected<void, int> on_client_msg_label_commit(const git_oid &commit)
{
    return {};
}

std::expected<std::string, int> render_mermaid() {
    std::lock_guard<std::mutex> lock(data_lock);

    lg2(git_revwalk_push_glob(walk, "*"), "git revwalk reset");

    std::vector<git_oid> topo {};
    git_oid oid;
    while (git_revwalk_next(&oid, walk) == 0) {
        topo.push_back(oid);
    }

    return str;
}
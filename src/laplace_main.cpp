#include <iostream>
#include <filesystem>
#include <thread>
#include <atomic>
#include <stacktrace>
#include <set>

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

struct laplace_git_note
{
    std::string branch; // do not allow multiple branches coexisting on the same commit
    std::string comments;
};

git_repository *repo = NULL;
std::vector<std::string> subcommands{};

shell_t shell{};
std::atomic<bool> pending_restart = false;
std::atomic<bool> disregard_shell_msg = false;

std::string oid_str(const git_oid &oid)
{
    return std::string(git_oid_tostr_s(&oid));
}

git_oid str_oid(const std::string &str)
{
    git_oid ret;
    git_oid_fromstr(&ret, str.c_str());
    return ret;
}

std::string ref_str(git_reference *ref)
{
    return git_reference_name(ref);
}

std::expected<git_commit *, int> get_head_commit();
std::expected<std::unordered_map<std::string, std::string>, int> get_all_branch_tips();
inline static const char *git_note_namespace = "refs/notes/laplace";
std::expected<laplace_git_note, int> get_commit_note(const git_oid *oid);
std::expected<void, int> set_commit_note(const git_oid *oid, const laplace_git_note &note);

std::expected<void, int> on_shell_msg(const std::string_view msg);
std::expected<void, int> on_client_msg(std::shared_ptr<ix::ConnectionState> connectionState, ix::WebSocket &webSocket, const ix::WebSocketMessagePtr &msg);

std::expected<void, int> serialize_metadata(const metadata_t &msg);
std::expected<void, int> apply_serialized_metadata();
std::expected<git_commit *, int> create_new_commit(const std::vector<git_commit *> &parents);

struct laplace_state_resp
{

    using cmd_t = std::string;
    using dict_t = std::unordered_map<std::string, std::string>;

    std::vector<dict_t> graph_timeseries;
    std::string current;
    std::string error;
};

std::expected<laplace_state_resp, int> render_mermaid();
std::string fetch_state_json()
{
    auto res = render_mermaid();
    res = res.value_or(laplace_state_resp{
        .error = std::to_string(res.error())});

    auto wr = glz::write_json(*res);
    std::string msg;
    if (!wr)
    {
        msg = std::string(wr.error().custom_error_message);
    }
    else
    {
        msg = *wr;
    }
    return msg;
}
void broadcast_state()
{
    std::string state_json = fetch_state_json(); // TODO: only use partial state changes (+branch and or +commit), remove graph.clear() from frontend
    for (const auto &c : server.getClients())
    {
        c->send(state_json);
    }
}

// simplified from https://github.com/libgit2/libgit2/blob/21a351b0ed207d0871cb23e09c027d1ee42eae98/examples/common.c#L23
void print_lg2_err(int error, const std::string &msg)
{
    const git_error *lg2err;
    if ((lg2err = git_error_last()) != NULL && lg2err->message != NULL)
    {
        std::cerr << msg << " - " << lg2err->message << std::endl;
        // std::cerr<<std::stacktrace::current()<< std::endl;
    }
}
#define lg2(e, msg)                      \
    {                                    \
        if (auto __v = (e); __v)         \
        {                                \
            print_lg2_err(__v, (msg));   \
            return std::unexpected(__v); \
        }                                \
    }

struct opts_additional_t
{
    std::string tty_path;
};

std::expected<std::string, std::string> get_current_tty()
{
    // Check if stdin is a terminal
    if (isatty(STDIN_FILENO))
    {
        const char *tty_path = ttyname(STDIN_FILENO);
        if (tty_path)
        {
            return std::string(tty_path);
        }
    }
    // Fallback to stdout/stderr if stdin is not a terminal
    if (isatty(STDOUT_FILENO))
    {
        const char *tty_path = ttyname(STDOUT_FILENO);
        if (tty_path)
        {
            return std::string(tty_path);
        }
    }
    if (isatty(STDERR_FILENO))
    {
        const char *tty_path = ttyname(STDERR_FILENO);
        if (tty_path)
        {
            return std::string(tty_path);
        }
    }
    // If no terminal found, return an empty string or throw an error
    return std::unexpected<std::string>("NO TTY DETECTED");
}

void reopen_tty(const std::string &tty_path)
{
    // Reopen stdin/stdout/stderr to the current TTY
    int tty_fd = open(tty_path.c_str(), O_RDWR);
    if (tty_fd >= 0)
    {
        dup2(tty_fd, STDIN_FILENO);
        dup2(tty_fd, STDOUT_FILENO);
        dup2(tty_fd, STDERR_FILENO);
        close(tty_fd);
    }
}

char delim = '\0';
std::expected<void, std::string> serialize_existing_metadata()
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
    for (auto &k_ent : std::filesystem::directory_iterator("/__laplace"))
    {
        if (!k_ent.is_directory())
        {
            continue;
        }

        auto k = k_ent.path().filename().string();

        // skip hidden folders such as .git
        if (k[0] == '.')
        {
            continue;
        }

        std::string f_dst = "/tmp/__laplace_" + k;
        int fd_dst = open(f_dst.c_str(), O_CREAT | O_WRONLY | O_TRUNC);

        if (fd_dst < 0)
        {
            perror("open");
            return std::unexpected("Create temp file " + f_dst);
        }

        for (auto &v_ent : std::filesystem::directory_iterator(k_ent.path()))
        {
            auto v = v_ent.path();
            int fd_src = open(v.c_str(), O_RDONLY);
            struct stat stat_buf;
            fstat(fd_src, &stat_buf);
            sendfile(fd_dst, fd_src, NULL, stat_buf.st_size);
            close(fd_src);
            write(fd_dst, &delim, 1);

            // std::cerr << k_ent << "::" << v_ent << std::endl;
        }

        close(fd_dst);
    }

    return {};
}

std::expected<void, std::string> restart_shell(const opts_main_t &opts, const opts_additional_t &opts_)
{

    serialize_existing_metadata();

    // generate a disregard message, to be consumed on the websocket side
    // this allows the first prompt command (i.e. without any command attached to it) to be ignored,
    // fixing the creation of unintended branches & commits
    disregard_shell_msg = true;

    const std::string adapter = "/adapter/" + opts.frontend; // TODO: refactor
    if (!std::filesystem::exists(adapter))
    {
        throw("Adapter not found: " + adapter);
    }

    int pid;
    if (pid = fork(); pid < 0)
    {
        throw("Fork failure: " + ERRNO);
    }

    // on child process, switch to shell
    if (pid == 0)
    {
        // int tty_fd = open(opts_.tty_path.c_str(), O_RDWR);
        // if (tty_fd < 0) {
        //     perror("reopen tty");
        //     exit(1);
        // }

        // dup2(tty_fd, STDIN_FILENO);
        // dup2(tty_fd, STDOUT_FILENO);
        // dup2(tty_fd, STDERR_FILENO);

        // FIXME: set as session leader, this grants the child process control over the terminal across restarts
        // setsid();

        // force TTY to open
        // reopen_tty(opts_.tty_path);

        // TODO: look into zsh params
        auto cmd = const_cast<char *>(opts.frontend_path.c_str());
        char *const argv[] = {
            cmd,
            const_cast<char *>("--rcfile"), const_cast<char *>(adapter.c_str()),
            NULL};
        execve(opts.frontend_path.c_str(), argv, NULL);
        perror("execve");
        exit(1);
    }
    else
    {
        shell.pid = pid;
    }

    return {};
}

std::expected<void, std::string> manage_shell(const opts_main_t &opts) noexcept
{

    /* Create socket from which to read. */
    int sock_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (sock_fd < 0)
    {
        throw("Open socket: " + ERRNO);
    }

    defer([sock_fd]
          { close(sock_fd); });

    if (const int status = git_libgit2_init(); status < 0)
    {
        throw("Failed to initialize libgit2" + std::to_string(status));
    }
    else
    {
        defer([]
              { git_libgit2_shutdown(); });
    }

    /* Create name. */
    unlink(laplace_socket_path.c_str());
    sockaddr_un name{};
    name.sun_family = AF_UNIX;
    strncpy(name.sun_path, laplace_socket_path.c_str(), laplace_socket_path.size() + 1);

    /* Bind the UNIX domain address to the created socket */
    if (bind(sock_fd, (struct sockaddr *)&name, sizeof(sockaddr_un)) < 0)
    {
        throw("Bind socket: " + ERRNO);
    }

    /* Local socket listener thread */
    auto t_sl = std::thread([sock_fd]
                            { shell_listener(sock_fd); });

    // Git setup
    git_libgit2_init();
    git_libgit2_opts(GIT_OPT_SET_OWNER_VALIDATION, 0);
    defer([]
          {
        kill(shell.pid, SIGKILL);
        git_repository_free(repo);
        git_libgit2_shutdown();
        server.stop(); });
    // preventing cold-start hanging issue by force running render function at least once
    render_mermaid();

    /* Web socket server thread */
    auto t_wss = std::thread(web_socket_server);

    /* Shell monitoring and restart thread (i.e. main thread) */

    // get TTY path once to use across multiple restarts
    auto tty = get_current_tty();
    abortif(tty);
    // std::cout << *tty << std::endl;

    // restart loop
    while (true)
    {
        if (auto res = restart_shell(opts, {.tty_path = *tty}); !res)
        {
            std::cerr << res.error() << std::endl;
            std::cerr << "Restart shell failed, exiting..." << std::endl;
            break;
        }

        // wait in separate thread, hopefully lower priority
        auto t_wait = std::thread([]
                                  {
            // Use while to handle unintended wakups
            while(waitpid(shell.pid, NULL, 0) != shell.pid);
            std::cerr << "Shell terminated" << std::endl; });
        t_wait.join();

        if (pending_restart)
        {
            pending_restart = false;
            std::cerr << "Restarting shell to reflect environment changes..." << std::endl;
        }
        else
        {
            break;
        }
    }
    return {};
}

void shell_listener(int sock_fd)
{
    std::array<char, 1 * 1024 * 1024> buf{};
    while (true)
    {
        int n = recvfrom(sock_fd, buf.data(), 1024 * 1024, 0, NULL, NULL);
        if (n > 0)
        {
            on_shell_msg(std::string_view(buf.begin(), buf.begin() + n - 1));
        }
        else if (n < 0)
        {
            std::cerr << "recvfrom" + ERRNO << "\n";
        }
    }
}

void web_socket_server()
{
    ix::SocketTLSOptions tls_opts{};
    tls_opts.tls = false;
    tls_opts.disable_hostname_validation = true;
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
    server.wait();
}

std::expected<void, int> init_repo(const char *repo_path)
{
    git_repository_init_options opts = GIT_REPOSITORY_INIT_OPTIONS_INIT;
    opts.mode = GIT_REPOSITORY_INIT_SHARED_ALL;
    opts.initial_head = "main";
    opts.description = "Repository managed by Laplace, with versions corresponding to Bash commands";
    lg2(git_repository_init_ext(&repo, repo_path, &opts), "create repository");
    return create_new_commit({nullptr}).and_then([](git_commit *commit) -> std::expected<void, int>
                                                 {
        // git_reference* branch;
        // lg2(git_branch_create(&branch, repo, "main", commit, 0), "git branch create");
        // lg2(git_repository_set_head(repo, "refs/heads/main"), "git repository set head"); // refs/heads is automatically used
        
        set_commit_note(git_commit_id(commit), {
            .branch = "main"
        });
        
        // git_reference_free(branch);
        return {}; }); // head at new commit
}

std::expected<git_commit *, int> get_head_commit()
{
    git_oid oid;
    git_commit *commit = NULL;

    lg2(git_reference_name_to_id(&oid, repo, "HEAD"), "lookup head reference");
    lg2(git_commit_lookup(&commit, repo, &oid), "lookup head commit");

    return commit;
}

std::expected<std::unordered_map<std::string, std::string>, int> get_all_branch_tips()
{
    std::unordered_map<std::string, std::string> branch_tips{};
    git_branch_iterator *i;
    lg2(git_branch_iterator_new(&i, repo, GIT_BRANCH_LOCAL), "branch iterator new");
    defer([i]
          { git_branch_iterator_free(i); });

    git_reference *curr_ref;
    git_branch_t curr_type;
    while (git_branch_next(&curr_ref, &curr_type, i) != GIT_ITEROVER)
    {
        defer([curr_ref]
              { git_reference_free(curr_ref); });

        const git_oid *target = git_reference_target(curr_ref);
        if (!target)
        {
            continue;
        }
        branch_tips[ref_str(curr_ref)] = oid_str(*target);
    }

    return branch_tips;
}

std::expected<laplace_git_note, int> get_commit_note(const git_oid *commit)
{
    git_note *note_raw = NULL;
    if (git_note_read(&note_raw, repo, git_note_namespace, commit) != 0)
    {
        return std::unexpected(0);
    }
    defer([note_raw]
          { git_note_free(note_raw); });

    laplace_git_note note;
    auto json_err = glz::read_json(note, git_note_message(note_raw));
    if (json_err)
    {
        std::cerr << json_err.custom_error_message << "\n";
        return std::unexpected(0);
    }

    return note;
}

std::expected<void, int> set_commit_note(const git_oid *oid, const laplace_git_note &note)
{
    auto json = glz::write_json(note);

    if (!json)
    {
        std::cerr << json.error().custom_error_message << "\n";
        return std::unexpected(0);
    }

    git_signature *sig;
    lg2(git_signature_now(&sig, "_", "_@laplace.com"), "git_signature_now");
    defer([sig]
          { git_signature_free(sig); });

    git_oid note_oid;
    lg2(git_note_create(&note_oid, repo, git_note_namespace, sig, sig, oid, json->c_str(), 1 /* force */), "create or update commit note");

    return {};
}

std::expected<void, int> init_laplace()
{
    const char *repo_path = "/__laplace";
    if (git_repository_open(&repo, repo_path) == 0)
    {
        std::cout << "Existing records found, scanning...\n";
        // TODO: determine if there actually is any scanning needed
    }
    else
    {
        std::cout << "No existing records, creating new repository...\n";
        init_repo(repo_path);
    }

    return {};
}

std::expected<std::string, int> diff_commit_parent(git_commit *commit)
{
    git_commit *parent = NULL;

    // for all non-first commits, at least one parent exists and could be compared against
    if (git_commit_parentcount(commit))
    {
        lg2(git_commit_parent(&parent, commit, 0), "git_commit_parent");
        defer([parent]
              { git_commit_free(parent); }); // TODO: accommodate multi parent for merging

        git_tree *commit_tree;
        lg2(git_commit_tree(&commit_tree, commit), "git_commit_tree");
        defer([commit_tree]
              { git_tree_free(commit_tree); });

        git_tree *parent_tree;
        lg2(git_commit_tree(&parent_tree, parent), "git_commit_tree");
        defer([parent_tree]
              { git_tree_free(parent_tree); });

        git_diff *diff = NULL;
        lg2(git_diff_tree_to_tree(&diff, repo, parent_tree, commit_tree, NULL), "git_diff_tree_to_tree");
        defer([diff]
              { git_diff_free(diff); });

        // see below, static buf is required, so thread safety is also required
        static std::mutex _;
        std::lock_guard _lock(_);

        static std::string buf;
        buf = "";

        auto line_cb = [](                              // cannot use capture here, C++ lambda with capture is not convertible to a function pointer (due to it being a functor object)
                           const git_diff_delta *delta, /**< delta that contains this data */
                           const git_diff_hunk *hunk,   /**< hunk containing this data */
                           const git_diff_line *line,   /**< line data */
                           void *payload) -> int
        {
            buf += std::string(line->content, line->content_len);
            return 0;
        };

        lg2(git_diff_print(diff, GIT_DIFF_FORMAT_PATCH, line_cb, nullptr), "git_diff_print"); // TODO: line callback

        return buf;
    }
    // for the very first commit, there is no parent
    else
    {
        // TODO:
        return "NEED IMPL";
    }
}

std::expected<bool, int> metadata_has_changed()
{

    auto head = get_head_commit();
    abortif(head);

    git_tree *commit_tree;
    lg2(git_commit_tree(&commit_tree, *head), "git_commit_tree");
    defer([commit_tree]
          { git_tree_free(commit_tree); });

    git_diff_options opts;
    lg2(git_diff_options_init(&opts, GIT_DIFF_OPTIONS_VERSION), "git_diff_options_init");

    git_diff *diff;
    lg2(git_diff_tree_to_workdir_with_index(&diff, repo, commit_tree, &opts), "git_diff_tree_to_workdir_with_index");
    defer([diff]
          { git_diff_free(diff); });

    std::cout << "metadata diff count: " << git_diff_num_deltas(diff) << std::endl;
    if (git_diff_num_deltas(diff) > 0)
    {
        return true;
    }
    else
    {
        return false;
    }
}

std::expected<void, int> on_shell_msg(const std::string_view msg)
{

    // Consume the disregard flag generated after restarting the shell
    if (disregard_shell_msg)
    {
        disregard_shell_msg = false;
        return {};
    }

    glz::error_ctx json_err;
    int err;

    metadata_t update;

    json_err = glz::read_json(update, msg);
    if (json_err)
    {
        std::cerr << json_err.custom_error_message << "\n";
        return std::unexpected(0);
    }

    // subcommand message does not contain state information
    if (update.body.size() == 0)
    {
        for (auto &cmd : update.commands)
        {
            subcommands.push_back(cmd);
        }
    }
    // command message contains at least the skeleton of the state,
    // which will always contain some entries.
    else
    {
        serialize_metadata(update);

        // case 1: initial call, starting from a blank repository
        auto changes = metadata_has_changed();
        abortif(changes);

        if (*changes)
        {
            // case 2: branches exist, but user jumped back to prior commit, causing a detached HEAD, when new changes
            //         are created, create a new branch along with a new note historoy
            if (git_repository_head_detached(repo))
            {

                // git_repository_head(, repo);
                // git_commit* commit = create_new_commit(NULL);
                // git_reference* branch;
                // git_branch_create(&branch, repo, ("b" + std::to_string(branches.size())).c_str(), commit, 0);
                // branches[branch] = { commit };

                get_head_commit().and_then([](git_commit *parent)
                                           {
                    defer([parent]{
                        git_commit_free(parent);
                    });
                    
                    return create_new_commit({parent}).and_then([parent](git_commit* commit) -> std::expected<void, int> {
                        auto oid = git_commit_id(commit);
                        std::string branch_name = "branch_" + oid_str(*oid).substr(0, 6);
                        std::string branch_ref_name = "refs/heads/" + branch_name;
                        
                        git_reference* branch;
                        lg2(git_branch_create(&branch, repo, branch_name.c_str(), commit, 0), "git branch create");
                        lg2(git_repository_set_head(repo, branch_ref_name.c_str()), "git repository set head"); // refs/heads is automatically used
                        
                        auto parent_note = get_commit_note(git_commit_id(parent)).value_or(laplace_git_note{});
                        
                        return set_commit_note(git_commit_id(commit), {
                            .branch = branch_name
                        });
                    }); });
            }
            // case 3: branch exists and is currently the HEAD, keep reusing the parent's note history
            else
            {
                git_reference *branch_head_ref;

                get_head_commit().and_then([](git_commit *parent)
                                           {
                    defer([parent]{
                        git_commit_free(parent);
                    });
                    
                    return create_new_commit({parent}).and_then([parent](git_commit* commit) {
                        auto parent_note = get_commit_note(git_commit_id(parent)).value_or(laplace_git_note{});
                        
                        return set_commit_note(git_commit_id(commit), {
                            .branch = parent_note.branch
                        });
                    }); });
            }

            subcommands.clear();
            broadcast_state();
        }
    }

    return {};
}

std::expected<void, int> serialize_metadata(const metadata_t &msg)
{
    // - __laplace
    // |---- k1
    // |-------- v1
    // |-------- v2
    // |-------- v3
    // |---- k2
    // ...

    for (const auto &[k, vs] : msg.body)
    {
        std::string d_k = "/__laplace/" + k;
        mkdir(d_k.c_str(), 0777);

        for (const auto &[v, val] : vs)
        {
            std::string f_v = d_k + "/" + v;

            int fd_v = open(f_v.c_str(), O_CREAT | O_WRONLY, 0666);
            if (write(fd_v, val.c_str(), val.size()) < 0)
            {
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
    // Due to lazy handling of Bash signals, a forced restart is needed
    pending_restart = true;
    kill(shell.pid, SIGHUP);
    return {};
}

std::expected<git_commit *, int> create_new_commit(const std::vector<git_commit *> &parents)
{
    int err;

    git_oid oid;
    git_signature *sig;
    lg2(git_signature_now(&sig, "_", "_@laplace.com"), "git_signature_now");
    defer([sig]
          { git_signature_free(sig); });

    git_index *index;
    git_oid tree_id;
    git_tree *tree;
    lg2(git_repository_index(&index, repo), "git_repository_index");
    lg2(git_index_add_all(index, NULL, 0, NULL, NULL), "git_index_add_all");
    lg2(git_index_write(index), "git_index_write");
    defer([index]
          { git_index_free(index); });
    lg2(git_index_write_tree(&tree_id, index), "git_index_write_tree");
    lg2(git_tree_lookup(&tree, repo, &tree_id), "look up actual tree obj from tree_id");
    defer([tree]
          { git_tree_free(tree); });

    std::string subcommands_str = "";
    for (const auto &subcommand : subcommands)
    {
        subcommands_str.append(subcommand);
        subcommands_str.append("\n");
    }

    // https://libgit2.org/docs/examples/init/
    lg2(git_commit_create(
            &oid, repo, "HEAD", sig, sig, NULL,
            subcommands_str.c_str(), tree,
            parents.size(), (const git_commit **)parents.data()),
        "git_commit_create");

    git_commit *ret;
    lg2(git_commit_lookup(&ret, repo, &oid), "commit lookup");

    return {ret};
}

// Client API

std::expected<void, int> on_client_msg_jump(const git_oid &guid);
std::expected<void, int> on_client_msg_label_branch(const std::string &branch);
std::expected<void, int> on_client_msg_label_commit(const git_oid &commit);

std::expected<void, int> on_client_msg(std::shared_ptr<ix::ConnectionState> connectionState, ix::WebSocket &ws, const ix::WebSocketMessagePtr &msg)
{
    if (msg->type == ix::WebSocketMessageType::Open)
    {
    }
    else if (msg->type == ix::WebSocketMessageType::Message)
    {
        const auto &uri = msg->openInfo.uri; // todo: distinguish URI path

        client_req_t req;
        auto err = glz::read_json<client_req_t>(req, msg->str);
        if (err)
        {
            std::cerr << err.custom_error_message << "\n";
            return std::unexpected((int)err.ec);
        }

        if (req.endpoint == "fetch")
        {
            ws.send(fetch_state_json());
        }
        else if (req.endpoint == "jump")
        {
            on_client_msg_jump(str_oid(req.params));
        }
        else if (req.endpoint == "annotate branch")
        {
        }
        else if (req.endpoint == "annotate commit")
        {
        }
    }

    return {};
}

int checkout_notify_cb(
    git_checkout_notify_t why,
    const char *path,
    const git_diff_file *baseline,
    const git_diff_file *target,
    const git_diff_file *workdir,
    void *payload)
{
    (void)baseline;
    (void)target;
    (void)workdir;
    (void)payload;

    if (why & GIT_CHECKOUT_NOTIFY_CONFLICT)
    {
        std::cerr << "Conflict detected in file: " << path << std::endl;
    }

    return 0;
}

std::expected<void, int> on_client_msg_jump(const git_oid &oid)
{
    git_checkout_options opts;
    lg2(git_checkout_options_init(&opts, GIT_CHECKOUT_OPTIONS_VERSION), "git checkout options init");
    opts.checkout_strategy = GIT_CHECKOUT_SAFE;
    opts.notify_cb = &checkout_notify_cb;

    git_object *target_commit = NULL;

    lg2(git_object_lookup(&target_commit, repo, &oid, GIT_OBJECT_COMMIT), "git object lookup");
    defer([target_commit]
          { git_object_free(target_commit); });

    lg2(git_checkout_tree(repo, target_commit, &opts), "git checkout tree");

    // jump right onto a branch ref, then just use that branch ref
    auto jump_to_branch = get_all_branch_tips().and_then(
        [&oid](const std::unordered_map<std::string, std::string> tips) -> std::expected<void, int>
        {
            for (const auto &[branch, commit] : tips)
            {
                git_oid commit_oid = str_oid(commit);
                if (git_oid_cmp(&commit_oid, &oid) == 0)
                {
                    // std::cout << "jumping to branch head: " << branch << " -> " << oid_str(oid) << std::endl;
                    git_repository_set_head(repo, branch.c_str());
                    return {};
                }
            }
            return std::unexpected(0);
        });

    // jumped to a non-ref commit, then detach
    if (!jump_to_branch)
    {
        // std::cout << "jump detached: " << oid_str(oid) << std::endl;
        lg2(git_repository_set_head_detached(repo, &oid), "jump back to " + oid_str(oid));
    }

    broadcast_state();

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

std::expected<laplace_state_resp, int> render_mermaid()
{
    if (!repo)
    {
        abortif(init_laplace());
    }

    auto _t_start = std::chrono::high_resolution_clock::now();

    git_oid head_commit;
    git_reference_name_to_id(&head_commit, repo, "HEAD");

    git_revwalk *walk;
    lg2(git_revwalk_new(&walk, repo), "create revision walker");
    defer([walk]
          { git_revwalk_free(walk); });

    git_branch_iterator *b_itr;
    lg2(git_branch_iterator_new(&b_itr, repo, GIT_BRANCH_LOCAL), "branch iterator new");
    defer([b_itr]
          { git_branch_iterator_free(b_itr); });

    git_reference *curr_branch_ref;
    git_branch_t curr_branch_type;
    while (git_branch_next(&curr_branch_ref, &curr_branch_type, b_itr) != GIT_ITEROVER)
    {
        git_revwalk_push_ref(walk, git_reference_name(curr_branch_ref));
    }

    lg2(git_revwalk_hide_glob(walk, "notes"), "git revwalk hide notes)");
    lg2(git_revwalk_sorting(walk, GIT_SORT_TOPOLOGICAL | GIT_SORT_REVERSE), "git revwalk sorting");

    git_oid oid;

    std::unordered_map<std::string, int> branch_encounter_order{};

    laplace_state_resp res{
        .graph_timeseries = {},
        .current = oid_str(head_commit)};

    while (!git_revwalk_next(&oid, walk))
    {
        // https://mermaid.js.org/syntax/gitgraph.html
        laplace_git_note note = get_commit_note(&oid).value_or(laplace_git_note{});

        if (!branch_encounter_order.contains(note.branch))
        {
            branch_encounter_order.emplace(note.branch, branch_encounter_order.size());

            res.graph_timeseries.push_back({{"entity", "branch"}, {"name", note.branch}});
            // if (note.branch != "main") {
            //     // mermaid will glitch out if there is a "branch main", for some reason
            //     // mermaid.append("\tbranch " + note.branch + "\n");
            // }
        }

        std::string message = "";
        std::string diffs = "";

        std::string id_full = oid_str(oid);
        std::string id = id_full.substr(0, 6);

        if (git_commit *commit; git_commit_lookup(&commit, repo, &oid) == 0)
        {
            message = std::string(git_commit_message(commit));
            auto diffs_req = diff_commit_parent(commit);
            if (diffs_req)
            {
                diffs = *diffs_req;
            }
            else
            {
                std::cerr << diffs_req.error() << std::endl;
            }
        }

        // FIXME
        res.graph_timeseries.push_back({
            {"entity", "commit"},
            {"branch", note.branch},
            {"id", id},
            {"id_full", id_full},
            {"diffs", diffs},
            {"commit_message", message},
            {"comments", note.comments},
        });

        // std::cerr << id << std::endl;

        // mermaid.append("\tcheckout " + note.branch + "\n"); // force checkout for implementation convenience
        // mermaid.append("\tcommit");
        // mermaid.append(" id:\"" + id + "\"");
        // if (note.comments != "") {
        //     mermaid.append(" tag:\"" + note.comments + "\"");
        // }
        // if (git_oid_cmp(&oid, &head_commit) == 0) {
        //     mermaid.append(" type: HIGHLIGHT");
        // }
        // mermaid.append("\n");
    }

    auto _t_end = std::chrono::high_resolution_clock::now();
    auto _t = std::chrono::duration_cast<std::chrono::microseconds>(_t_end - _t_start);
    // std::cout << "render time: " << _t.count() << std::endl;
    return res;
}
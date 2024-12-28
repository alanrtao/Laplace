#include <iostream>
#include <git2.h>
#include "utils.h"

void test_func();

int main(int argc, char* argv[]) {
    std::cout<<"hello\n";
    
    if (git_libgit2_init() < 0) {
        std::cerr << "Failed to initialize libgit2" << std::endl;
        return 1;
    }
    defer([]{ git_libgit2_shutdown(); });
    defer([]{ std::cout<<"goodbye\n"; });

    test_func();
    return 0;
}

void test_func() {

    // Open the repository at the current directory
    git_repository* repo = nullptr;
    const char* repo_path = "."; // Adjust the path to your repository if needed

    if (git_repository_open(&repo, repo_path) != 0) {
        const git_error* e = git_error_last();
        std::cerr << "Failed to open repository: "
                  << (e && e->message ? e->message : "Unknown error") << std::endl;
        return;
    }

    // Get the HEAD reference
    git_reference* head_ref = nullptr;
    if (git_repository_head(&head_ref, repo) != 0) {
        const git_error* e = git_error_last();
        std::cerr << "Failed to get HEAD reference: "
                  << (e && e->message ? e->message : "Unknown error") << std::endl;
        git_repository_free(repo);
        return;
    }

    // Print the HEAD reference name
    const char* head_name = git_reference_name(head_ref);
    std::cout << "Current HEAD: " << head_name << std::endl;

    // Cleanup
    git_reference_free(head_ref);
    git_repository_free(repo);
}
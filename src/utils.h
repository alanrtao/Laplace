#pragma once

#include <cerrno>
#include <cstring>
#include <string>
#include <expected>
#include <functional>

// https://stackoverflow.com/questions/19666142/why-is-a-level-of-indirection-needed-for-this-concatenation-macro
#define UNIQUE_VARNAME(pre) UNIQUE_VARNAME_(pre, __COUNTER__)
#define UNIQUE_VARNAME_(pre, post) CONCAT(pre, post)
#define CONCAT(x,y) x##y
#define defer(function_body) const _defer UNIQUE_VARNAME(_defer_) (function_body)

struct _defer {
    std::function<void()> func {};
    _defer(const std::function<void()>& func): func{func} {}
    ~_defer() { func(); }
};

#define ERRNO std::string(std::strerror(errno))

#define throw(str) return std::unexpected((str) + "\n")
#define abortif(expected) { if (!expected) { return std::unexpected((expected).error()); } }
#define throwif(expected) { if (!expected) { throw(std::to_string((expected).error())); } }

inline std::string read_file_descriptor(const int fd) {
    std::string result {};
    char buffer[4096];  // Read in chunks
    ssize_t bytesRead;
    
    while ((bytesRead = read(fd, buffer, sizeof(buffer))) > 0) {
        result.append(buffer, bytesRead);
    }

    return result;
}
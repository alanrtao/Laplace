#include <functional>

// https://stackoverflow.com/questions/19666142/why-is-a-level-of-indirection-needed-for-this-concatenation-macro
#define UNIQUE_VARNAME(pre) UNIQUE_VARNAME_(pre, __COUNTER__)
#define UNIQUE_VARNAME_(pre, post) CONCAT(pre, post)
#define CONCAT(x,y) x##y
#define defer(function_body) const _defer UNIQUE_VARNAME(_defer_) = _defer(function_body)

struct _defer {
    std::function<void()> func {};
    _defer(const std::function<void()>& func): func{func} {}
    ~_defer() { func(); }
};

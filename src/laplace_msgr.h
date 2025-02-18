#pragma once

#include <vector>
#include <unordered_map>
#include <expected>
#include "laplace.h"

std::unordered_map<std::string, std::string> parse_environ() noexcept;

std::expected<void, std::string> msg(
    const opts_msgr_t& opts,
    const std::unordered_map<std::string, std::string>& env) noexcept;
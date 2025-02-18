#pragma once

#include <vector>
#include <unordered_map>
#include <expected>
#include "laplace.h"

std::expected<void, std::string> msg(
    const opts_msgr_t& opts) noexcept;

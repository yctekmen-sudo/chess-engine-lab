#pragma once

#include <string_view>

namespace chess_lab {
// Scaffolding metadata only; unrelated to the future evaluator ABI/schema version.
[[nodiscard]] std::string_view project_version() noexcept;
}

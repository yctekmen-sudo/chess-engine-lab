#include "chess_lab/build_info.hpp"

namespace chess_lab {
std::string_view project_version() noexcept {
    return CHESS_LAB_VERSION;
}
}

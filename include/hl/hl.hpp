#pragma once

#include "core/control_block.hpp"
#include "core/borrow.hpp"
#include "core/local.hpp"
#include "memory/arena_hoist.hpp"
#include "trace/telemetry.hpp"

namespace hl {
    using core::local;
    using core::borrow;
    using trace::get_stats;
}

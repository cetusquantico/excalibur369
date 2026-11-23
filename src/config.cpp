#include "config.h"

namespace ex369 {

const char* order_name(Order o) {
    switch (o) {
        case Order::Sequential: return "sequential";
        case Order::Reverse:    return "reverse";
        default:                return "random-no-repeat";
    }
}

bool parse_order(const std::string& s, Order& out) {
    if (s == "sequential" || s == "seq" || s == "forward") { out = Order::Sequential; return true; }
    if (s == "reverse" || s == "rev" || s == "backward")   { out = Order::Reverse; return true; }
    if (s == "random" || s == "random-no-repeat" || s == "rnr") { out = Order::Random; return true; }
    return false;
}

const char* direction_name(Direction d) {
    return d == Direction::Forward ? "forward(P+=G)" : "reverse(P-=G)";
}

bool puzzle_range(int n, U256& lo, U256& hi) {
    if (n < 1 || n > 160) return false;
    lo = U256(1).shl((unsigned)(n - 1));
    hi = U256(1).shl((unsigned)n);
    hi = hi - U256(1);
    return true;
}

} // namespace ex369

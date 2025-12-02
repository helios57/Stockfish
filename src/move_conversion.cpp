#include "move_conversion.h"

#include <algorithm>
#include <cctype>
#include <string>

#include "position.h"
#include "movegen.h"

namespace Stockfish {

namespace {
    std::string to_lower(std::string str) {
        std::transform(str.begin(), str.end(), str.begin(), [](auto c) { return std::tolower(c); });
        return str;
    }
}

std::string square_to_string(Square s) {
    return std::string{char('a' + file_of(s)), char('1' + rank_of(s))};
}

std::string move_to_string(Move m, bool chess960) {
    if (m == Move::none())
        return "(none)";

    if (m == Move::null())
        return "0000";

    Square from = m.from_sq();
    Square to   = m.to_sq();

    if (m.type_of() == CASTLING && !chess960)
        to = make_square(to > from ? FILE_G : FILE_C, rank_of(from));

    std::string move = square_to_string(from) + square_to_string(to);

    if (m.type_of() == PROMOTION)
        move += " pnbrqk"[m.promotion_type()];

    return move;
}

Move to_move(const Position& pos, std::string str) {
    str = to_lower(str);

    for (const auto& m : MoveList<LEGAL>(pos))
        if (str == move_to_string(m, pos.is_chess960()))
            return m;

    return Move::none();
}

} // namespace Stockfish

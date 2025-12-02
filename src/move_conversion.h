#ifndef MOVE_CONVERSION_H
#define MOVE_CONVERSION_H

#include <string>
#include "types.h"

namespace Stockfish {

class Position;

std::string square_to_string(Square s);
std::string move_to_string(Move m, bool chess960);
Move to_move(const Position& pos, std::string str);

} // namespace Stockfish

#endif // MOVE_CONVERSION_H

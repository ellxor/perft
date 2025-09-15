// FIXME: this file is a real mess
#pragma once
#include "bitboard.h"
#include "board.h"

/*
 *   Store a compressed move in 16 bits. The 'init' and 'dest' fields store the initial and
 *   destination Squares of the move, and the 'piece' field stores the piece that will occupy the
 *   Square at the end of the move (in case of promotion, the type of the promoted piece). There is a
 *   single bit left over so we use this as a flag to indicate castling. This is redundant information
 *   but it does yield a small performance improvement to the move generation. Making the move smaller
 *   improves performance as the move buffer is relatively large (for the reasons give below), so it
 *   helps to improve cache locality.
 */

// FIXEME: Investigative why using struct { uint16_t init: 6, ... } is so much slower...
//         This below is just an ugly hack for now.

typedef uint16_t Move;

#define M(init, dest, piece)	((init) | (dest) << 6 | (piece) << 13)
#define M_CASTLING_MASK		0x1000u

#define M_CASTLING(dest)  (E1 | (dest) << 6 | M_CASTLING_MASK | (King << 13))
#define M_INIT(mov)       (((mov)      ) & 0x3f)
#define M_DEST(mov)       (((mov) >>  6) & 0x3f)
#define M_PIECE(mov)      ( (mov) >> 13)


/*
 *   The generated moves are stored in a fixed-size buffer for performance, reallocations would slow
 *   us down a lot. It is usually a large overallocation as chess has a branching factor of around
 *   30-40, but some positions, although exceedingly rare, do require this many moves.
 *
 *   This position holds the record for the maximum number of possible legal moves at 218:
 *   FEN: 3Q4/1Q4Q1/4Q3/2Q4R/Q4Q2/3Q4/1Q4Rp/1K1BBNNk w - -
 *
 *   "Normal" pawn moves (single non-promotion or double moves) are stored in a bitboard to prevent
 *   them from being iterated over twice. As these make up a lot of moves in the position, this
 *   yields a significant performance gain.
 */

constexpr size_t MaximumLegalMoves = 218;


struct MoveBuffer {
        BitBoard pawn_pushes;
        size_t   size;
        Move     moves[MaximumLegalMoves];

        void push(Move move) {
                moves[size++] = move;
        }
};


MoveBuffer generate_moves(Board& board);
uint64_t count_moves(Board& board); // used to make leaf counting faster

Board make_move(Board board, Move move);
Board make_pawn_push(Board board, Square dest);

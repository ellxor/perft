#pragma once
#include "bitboard.h"

/*
 *   The chess board state is compressed into just 4 bitboards. Due to its unusual
 *   nature there are a few things to note:
 *
 *   - It is color agnostic, so the bitboards are flipped so that the side to
 *     move is always at the bottom. Pieces of side to move are stored in `our`.
 *
 *   - The `our` bitboard also stores the en-passant square. As the en-passant
 *     square must be empty, this can easily be found by masking with occupied
 *     squares, and when using `our` as a mask, this en-passant square will have
 *     no effect.
 *
 *   - The `x`,`y`,`z` bitboards store the information of all piece types, as the
 *     three bits from each square form an ID of 8 distinct values for the 6 unique
 *     piece types and empty square.
 *
 *   - There is an eighth piece type called a castle, which simply represents a
 *     rook that can be castled with. Upon moving, this type decays to a rook.
 */

typedef int PieceType;

// Piece types correspond to:     zyx
static const PieceType Empty  = 0b000;
static const PieceType Pawn   = 0b001;
static const PieceType Knight = 0b010;
static const PieceType Bishop = 0b011;
static const PieceType Rook   = 0b100;
static const PieceType Castle = 0b101;
static const PieceType Queen  = 0b110;
static const PieceType King   = 0b111;


struct Board {
        BitBoard x,y,z;
        BitBoard our;

        BitBoard occupied()   { return x | y | z; }
        BitBoard en_passant() { return our & ~occupied(); }

        BitBoard extract_by_piece(PieceType piece) {
                // First handle the special case of rooks as castles are still rooks for
                // move generation and evaluation purposes.
                if (piece == Rook) return z &~ y;

                // This piece of code looks terrible slow and in-efficient but as this
                // function is inlined and always called with a constexpr piecetype, it
                // will be folded by the compiler into a couple bitwise instructions.
                return ((piece & 0b001) ? x : ~x)
                     & ((piece & 0b010) ? y : ~y)
                     & ((piece & 0b100) ? z : ~z);
        }
};


// FIXME: this should be done better
static inline void set_square(struct Board* board, Square dest, PieceType piece) {
        BitBoard mask = OneBB << dest;

        if (piece & 0b001) board->x |= mask;
        if (piece & 0b010) board->y |= mask;
        if (piece & 0b100) board->z |= mask;

        board->our |= mask;
}


// For debugging purposes only...
#include <stdio.h>

static inline void dump_board(struct Board board) {
        printf("\n------------------------------------------\n\n");

        for (Square rank = 8; rank --> 0;) {
                for (Square file = A1; file <= H1; ++file) {
                        auto sq = rank*North + file;

                        auto mask = OneBB << sq;
                        PieceType piece = Empty;

                        if (board.x & mask) piece |= 0b001;
                        if (board.y & mask) piece |= 0b010;
                        if (board.z & mask) piece |= 0b100;

                        char c = ".pnbrrqk"[piece];
                        if (board.our & mask) c -= 0x20; // covert to uppercase

                        printf("%c%c", c, (file == H1) ? '\n' : ' ');
                }
        }

        printf("\n------------------------------------------\n\n");
}

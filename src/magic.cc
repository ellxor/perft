#include <assert.h>
#include "bitboard.h"
#include "magic.h"

typedef int DiagonalIndex;

constexpr size_t SlidingAttacksTableSize = 107648;

BitBoard KnightAttacks[64+1];
BitBoard KingAttacks[64];
BitBoard SlidingAttacks[SlidingAttacksTableSize];
BitBoard LineBetween[64][64];

Magic BishopMagics[64];
Magic RookMagics[64];


inline unsigned leading_zeros(BitBoard bb) {
        return __builtin_clzll(bb);
}


// Generate diagonal for bishop moves, the diagonals are from bottom-left to top-right, with the
// main diagonal (index 0) being A1 to H8. The index (n) specifies the digonal, with positive
// shifting the digonal toward A8, and negative toward H1.

BitBoard generate_diagonal(DiagonalIndex index) {
        BitBoard main_diag = 0x8040201008040201;
        return (index > 0) ? main_diag << (8 * index) : main_diag >> -(8 * index);
}


BitBoard generate_sliding_attacks(Square sq, BitBoard mask, BitBoard occ)
{
        occ &= mask; // only use the occupancy of Squares we need
        auto bit = OneBB << sq;

        auto lower = occ & (bit - 1);
        auto upper = occ - lower;

        lower = (OneBB << 63) >> leading_zeros(lower | 1);   // isolate msb of lower bits...
        return mask & (upper ^ (upper - lower)) &~ bit; // ... and extract range up to lsb of upper bits
}


//  Generate the line (diagonal or orthogonal) between two Squares, used for pinned piece masks and
//  blocking checks. The mask returned does include the bit for Square b, used to allow pieces to
//  capture a checking piece.

BitBoard generate_line_between(Square from, Square dest)
{
        auto from_bb = OneBB << from;
        auto dest_bb = OneBB << dest;

        auto diag = BishopMagics[from].attacks(dest_bb);
        auto orth = RookMagics[from].attacks(dest_bb);

        auto line = EmptyBB;

        if (diag & dest_bb)  line = diag & BishopMagics[dest].attacks(from_bb);
        if (orth & dest_bb)  line = orth & RookMagics[dest].attacks(from_bb);

        return line | dest_bb;
}


void init_bitboard_tables()
{
        int index = 0;

        for (Square sq = A1; sq <= H8; ++sq) {
                auto bit = OneBB << sq;

                KnightAttacks[sq] = north(north(east(bit))) | north(north(west(bit)))
                                  | south(south(east(bit))) | south(south(west(bit)))
                                  | east(east(north(bit)))  | east(east(south(bit)))
                                  | west(west(north(bit)))  | west(west(south(bit)));

                KingAttacks[sq] = north(bit) | east(bit) | south(bit) | west(bit)
                                | north(east(bit)) | north(west(bit)) | south(east(bit)) | south(west(bit));


                // bishop attacks
                {
                        Square file = sq & 7, rank = sq >> 3;

                        auto diag = generate_diagonal(rank - file);
                        auto anti = rotate(generate_diagonal(7 - rank - file));

                        // Clear outer bits of mask. These not needed for magic BitBoards as a
                        // sliding piece can always move to the edge of the board if the Square
                        // just before is unoccupied. We also clear the bit of the Square as this
                        // is always occupied by the moving piece itself so is irrelevant.

                        auto outer = FileABB | FileHBB | Rank1BB | Rank8BB | bit;
                        auto mask = (diag | anti) &~ outer;

                        BishopMagics[sq].mask = mask;
                        BishopMagics[sq].table = SlidingAttacks + index;
                        auto occ = EmptyBB;

                        do {
                                SlidingAttacks[index++] = generate_sliding_attacks(sq, diag, occ)
                                                        | generate_sliding_attacks(sq, anti, occ);
                                occ = (occ - mask) & mask; // iterate over all subset BitBoards of a BitBoard
                        }
                        while (occ);
                }

                // rook attacks
                {
                        auto file = file_of(sq);
                        auto rank = rank_of(sq);

                        // Rook moves are generated using the same techniques as bishop moves
                        // above, except more care must be taken with the board edges.

                        auto file_outer = Rank1BB | Rank8BB;
                        auto rank_outer = FileABB | FileHBB;

                        auto mask = ((file &~ file_outer) | (rank &~ rank_outer)) &~ bit;

                        RookMagics[sq].mask = mask;
                        RookMagics[sq].table = SlidingAttacks + index;

                        auto occ = EmptyBB;

                        do {
                                SlidingAttacks[index++] = generate_sliding_attacks(sq, file, occ)
                                                        | generate_sliding_attacks(sq, rank, occ);
                                occ = (occ - mask) & mask;
                        }
                        while (occ);
                }
        }

        assert(index == SlidingAttacksTableSize);


        // LineBetween must be generated after as sliding attacks as it relies on bishop and
        // rook moves already being initialised.

        for (Square from = A1; from <= H8; ++from) {
                for (Square dest = A1; dest <= H8; ++dest) {
                        LineBetween[from][dest] = generate_line_between(from, dest);
                }
        }
}

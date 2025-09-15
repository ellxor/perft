#pragma once
#include <x86intrin.h>
#include "bitboard.h"


struct Magic {
        BitBoard* table;
        BitBoard  mask;

        BitBoard attacks(BitBoard occupied) {
                return table[_pext_u64(occupied, mask)];
        }
};

extern BitBoard KnightAttacks[64];
extern BitBoard KingAttacks[64];
extern BitBoard LineBetween[64][64];

extern struct Magic BishopMagics[64];
extern struct Magic RookMagics[64];

void init_bitboard_tables();

#pragma once
#include <stdint.h>

typedef uint64_t BitBoard;
typedef int Square;

constexpr Square A1 = 0, B1 = 1, C1 = 2, D1 = 3;
constexpr Square E1 = 4, F1 = 5, G1 = 6, H1 = 7;
constexpr Square A8 = 56, H8 = 63;

constexpr Square North = +8;
constexpr Square South = -8;
constexpr Square East  = +1;
constexpr Square West  = -1;

constexpr BitBoard EmptyBB = 0;
constexpr BitBoard OneBB = 1;

constexpr BitBoard FileABB = 0x0101'0101'0101'0101;
constexpr BitBoard FileHBB = 0x8080'8080'8080'8080;
constexpr BitBoard Rank1BB = 0x0000'0000'0000'00ff;
constexpr BitBoard Rank3BB = 0x0000'0000'00ff'0000;
constexpr BitBoard Rank8BB = 0xff00'0000'0000'0000;


inline BitBoard file_of(Square sq) { return FileABB << (sq &  7); }
inline BitBoard rank_of(Square sq) { return Rank1BB << (sq & 56); }

inline BitBoard  north(BitBoard bb) { return bb << North; }
inline BitBoard  south(BitBoard bb) { return bb >> North; }
inline BitBoard   east(BitBoard bb) { return bb << East &~ FileABB; }
inline BitBoard   west(BitBoard bb) { return bb >> East &~ FileHBB; }
inline BitBoard rotate(BitBoard bb) { return __builtin_bswap64(bb); }


inline Square trailing_zeros(BitBoard bb) {
        return __builtin_ctzll(bb);
}

inline unsigned popcount(BitBoard bb) {
        return __builtin_popcountll(bb);
}


#define bits(mask) mask != 0; mask &= mask - 1

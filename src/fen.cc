// FIXME: this is so much of an abomination it is in its own separate source file with
// no header...

// FIXME: this file is in need of improvement... this should later be integrated into some
// actual parsing within a UCI interface. This was just a quick'n'dirty function to do some
// perft testing during development

#pragma once
#include "board.h"

// Parse Forsyth-Edwards Notation for a legal chess position.
//   (Reference: https://www.chessprogramming.org/Forsyth-Edwards_Notation)

#define ERROR()  do { *ok = false; return board; } while(0)


struct Board parse_fen(const char *fen_string, bool *white_to_move, bool *ok)
{
        PieceType piece_lookup[128];
        piece_lookup['p'] = Pawn;
        piece_lookup['n'] = Knight;
        piece_lookup['b'] = Bishop;
        piece_lookup['r'] = Rook;
        piece_lookup['q'] = Queen;
        piece_lookup['k'] = King;

        struct Board board = {};
        Square sq = 56, file = 0;

	/* Parse board */
	while (sq != 8 || file != 8)
	{
		const char c = *fen_string++;
		const char lower_mask = 0x20;

		if (file > 8) ERROR();

		/* end of rank */
		if (file == 8) {
			if (c != '/') ERROR();
                        sq += 2*South, file = 0;
			continue;
		}

		/* blank squares */
		if ('1' <= c && c <= '8') {
                        Square offset =	c - '0';
			sq += offset, file += offset;
		}

		else {
                        PieceType piece = piece_lookup[c | lower_mask];
                        if (piece == Empty) ERROR();

			/* Check if piece is black */
			set_square(&board, sq, piece);
                        if (c & lower_mask) board.our ^= OneBB << sq;

			sq += 1, file += 1;
		}
	}

	/* space separator */
	if (*fen_string++ != ' ') ERROR();

	/* parse side-to-move */
	switch (*fen_string++) {
		case 'w': *white_to_move = true; break;
		case 'b': *white_to_move = false; break;
		default : ERROR();
	}

	/* space separator */
	if (*fen_string++ != ' ') ERROR();

	if (*fen_string == '-')
		fen_string += 1;

	else while (*fen_string != ' ') {
                BitBoard castling_mask = 0;

		switch (*fen_string++) {
			case 'K': castling_mask |= OneBB << H1; break;
			case 'Q': castling_mask |= OneBB << A1; break;
			case 'k': castling_mask |= OneBB << H8; break;
			case 'q': castling_mask |= OneBB << A8; break;
			default : ERROR();
		}

		/* flip rooks to castles */
		board.x ^= castling_mask;
	}

	/* space separator */
	if (*fen_string++ != ' ') ERROR();

	/* parse en-passant */
        BitBoard en_passant_mask = 0;

	if (*fen_string != '-') {
                Square file = *fen_string++ - 'a';
                Square rank = *fen_string++ - '1';

		if (file >= 8 || rank >= 8) ERROR();

                Square en_passant = (rank << 3) + file;
		en_passant_mask = OneBB << en_passant;
	}

	/* There is more FEN info after this point such as movenumber and 50 move clock but we don't
	 * care about this information yet. TODO: handle this information
	 */

	/* Rotate bitboards if black is the side to move */
	if (*white_to_move)
                board.our |= en_passant_mask;

	else {
                BitBoard black = board.occupied() &~ board.our;

                board.x = rotate(board.x);
                board.y = rotate(board.y);
                board.z = rotate(board.z);
                board.our = rotate(black | en_passant_mask);
	}

	*ok = true;
	return board;
}

#undef ERROR

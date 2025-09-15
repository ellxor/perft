// FIXME: this file is an absolute disaster...

#include "bitboard.h"
#include "board.h"
#include "magic.h"
#include "movegen.h"


// Some useful info to pass around to move generation
struct MoveGenerationInfo {
        BitBoard attacked;
        BitBoard targets;
        BitBoard en_passant;
        BitBoard hpinned;
        BitBoard vpinned;
        Square   king;
};


// Generate pawn moves from a move mask, from a given direction. This allows us to
// generate in more predictable loops.

void generate_partial_pawn_moves(MoveBuffer& buffer, BitBoard moves, Square direction, bool promotion)
{
        for (; bits(moves)) {
                auto dest = trailing_zeros(moves);
                auto init = dest - direction;

                if (promotion) {
                        buffer.push(M(init, dest, Knight));
                        buffer.push(M(init, dest, Bishop));
                        buffer.push(M(init, dest, Rook));
                        buffer.push(M(init, dest, Queen));
                }

                else {
                        buffer.push(M(init, dest, Pawn));
                }
        }
}


// Generate pawn moves according to a targets mask (where pawns must end their move, for example
// in case of check this may be restricted), and a pinned mask which indicates pawns that are
// pinned to our king.

void generate_pawn_moves(MoveBuffer& buffer, Board& board, MoveGenerationInfo& info)
{
        auto pawns   = board.extract_by_piece(Pawn) & board.our;
        auto occ     = board.occupied();
        auto enemy   = occ &~ board.our;
        auto targets = info.targets;

        // Check for pinned en-passant. Note that this is a special type of pinned piece as two
        // pieces dissappear in the checking direction. This introduces a slow branch into our pawn
        // move generation, but it is a necessary evil for full legality, however rare.

        auto candidates = pawns & south(east(info.en_passant) | west(info.en_passant));

        // We optimise this branch by only checking if the king is actually on the 5th rank
        if ((info.king & 56) == 32 && popcount(candidates) == 1) {
                BitBoard pinners = (board.extract_by_piece(Rook) | board.extract_by_piece(Queen)) &~ board.our;
                BitBoard clear = candidates | south(info.en_passant);

                // If the pawn is "double" pinned, then en-passant is no longer possible
                if (RookMagics[info.king].attacks( (occ | info.en_passant) &~ clear) & pinners)
                        info.en_passant = 0;
        }

        // enable en-passant if the pawn being captured was giving check
        targets |= info.en_passant & north(info.targets);
        enemy   |= info.en_passant;

        auto pinned = info.hpinned | info.vpinned;
        auto normal_pawns = pawns &~ pinned;
        auto pinned_pawns = pawns & pinned;

        auto file = file_of(info.king); // only pinned pawns on same file as king can move forward
        auto forward = normal_pawns | (pinned_pawns & file);

        auto single_move = north(forward) &~ occ;
        auto double_move = north(single_move & Rank3BB) &~ occ;

        auto east_capture = north(east(normal_pawns)) & enemy;
        auto west_capture = north(west(normal_pawns)) & enemy;

        auto pinned_east_capture = north(east(pawns & info.vpinned)) & enemy & info.vpinned;
        auto pinned_west_capture = north(west(pawns & info.vpinned)) & enemy & info.vpinned;

        single_move  = single_move & targets;
        double_move  = double_move & targets;
        east_capture = (east_capture | pinned_east_capture) & targets;
        west_capture = (west_capture | pinned_west_capture) & targets;

        buffer.pawn_pushes = (single_move &~ Rank8BB) | double_move;

        // promotions, note: double moves cannot promote
        generate_partial_pawn_moves(buffer, single_move  & Rank8BB, North,   true);
        generate_partial_pawn_moves(buffer, east_capture & Rank8BB, North+East, true);
        generate_partial_pawn_moves(buffer, west_capture & Rank8BB, North+West, true);

        generate_partial_pawn_moves(buffer, east_capture &~ Rank8BB, North+East, false);
        generate_partial_pawn_moves(buffer, west_capture &~ Rank8BB, North+West, false);
}


BitBoard generic_attacks(PieceType piece, Square sq, BitBoard occ)
{
        switch (piece) {
                case Knight: return KnightAttacks[sq];
                case Bishop: return BishopMagics[sq].attacks(occ);
                case Rook:   return RookMagics[sq].attacks(occ);
                case Queen:  return BishopMagics[sq].attacks(occ)
                                | RookMagics[sq].attacks(occ);
                default: __builtin_unreachable();
        }
}


void generate_piece_moves(MoveBuffer& buffer, Board& board, MoveGenerationInfo& info, PieceType piece, bool pinned)
{
        auto _pinned = info.hpinned | info.vpinned;
        if (pinned) _pinned = (piece == Bishop) ? info.vpinned : info.hpinned;

        auto occ    = board.occupied();
        auto pieces = board.extract_by_piece(piece);
        auto queens = board.extract_by_piece(Queen);
        if (pinned) pieces |= queens;

        pieces &= board.our & (pinned ? _pinned : ~_pinned);

        for (; bits(pieces)) {
                auto init = trailing_zeros(pieces);
                auto attacks = generic_attacks(piece, init, occ) & info.targets;
                auto p = piece;

                // If the piece is pinned, then the moves must remain aligned to the king.
                if (pinned) {
                        attacks &= _pinned;
                        if (queens & pieces & -pieces) p = Queen;
                }

                for (; bits(attacks)) {
                        Square dest = trailing_zeros(attacks);
                        buffer.push(M(init, dest, p));
                }
        }
}


// Generate king moves. We use a specialised function rather than the one above as we always have
// exactly one king so the outer loop can be optimised away. Here we also clear the attacked mask
// to prevent our king from walking into check.

void generate_king_moves(MoveBuffer& buffer, Board& board, MoveGenerationInfo& info)
{
        BitBoard occ = board.occupied();
        BitBoard attacks = KingAttacks[info.king] &~ (info.attacked | (board.our & occ));

        for (; bits(attacks)) {
                auto dest = trailing_zeros(attacks);
                buffer.push(M(info.king, dest, King));
        }

        auto castling = board.extract_by_piece(Castle)
                & RookMagics[info.king].attacks(occ);

// Special BitBoards to check castling against. The OCC BitBoards must not be occupied, and
// the ATT BitBoards must not be attacked, as castling out-of, through or into check is not
// allowed.

#define QATT  (1 << C1 | 1 << D1 | 1 << E1)
#define KATT  (1 << E1 | 1 << F1 | 1 << G1)

        if (castling & (1 << A1) && !(info.attacked & QATT))  buffer.push(M_CASTLING(C1));
        if (castling & (1 << H1) && !(info.attacked & KATT))  buffer.push(M_CASTLING(G1));
}


// Generate attacked mask to prevent illegal king walks in parallel. We can also generate
// pawn and Knight checks at the same time for efficiency.

BitBoard enemy_attacked(Board& board, BitBoard* checks)
{
        auto pawns   = board.extract_by_piece(Pawn)   &~ board.our;
        auto knights = board.extract_by_piece(Knight) &~ board.our;
        auto bishops = board.extract_by_piece(Bishop) &~ board.our;
        auto rooks   = board.extract_by_piece(Rook )  &~ board.our;
        auto queens  = board.extract_by_piece(Queen)  &~ board.our;
        auto king    = board.extract_by_piece(King )  &~ board.our;

        // Merge queens with other sliding pieces to reduce number of loops
        bishops |= queens;
        rooks   |= queens;

        auto attacked = EmptyBB;
        auto our_king = board.extract_by_piece(King) & board.our;
        auto occ = board.occupied() &~ our_king; // allows sliders to x-ray through our king

        // Simple non-sliding moves
        attacked |= south(east(pawns) | west(pawns));
        attacked |= KingAttacks[trailing_zeros(king)];

        *checks |= pawns & north(east(our_king) | west(our_king));
        *checks |= knights & KnightAttacks[trailing_zeros(our_king)];

        for (; bits(knights))  attacked |= KnightAttacks[trailing_zeros(knights)];
        for (; bits(bishops))  attacked |= BishopMagics[trailing_zeros(bishops)].attacks(occ);
        for (; bits(rooks))    attacked |= RookMagics[trailing_zeros(rooks)].attacks(occ);

        return attacked;
}


// Generate a mask that contains all pinned pieces for the side to move. Note, this may include other
// random Squares. We also generate sliding (Bishop, rook and queen) checks here for efficiency.

void generate_pinned(Board& board, MoveGenerationInfo* info, BitBoard* checks)
{
        auto occ     = board.occupied();
        auto bishops = board.extract_by_piece(Bishop) &~ board.our;
        auto rooks   = board.extract_by_piece(Rook)   &~ board.our;
        auto queens  = board.extract_by_piece(Queen)  &~ board.our;
        auto white = board.our & occ;

        bishops |= queens;
        rooks   |= queens;

        auto bishop_ray = BishopMagics[info->king].attacks(occ);
        auto rook_ray = RookMagics[info->king].attacks(occ);

        *checks |= bishop_ray & bishops;
        *checks |= rook_ray & rooks;

        auto nocc = occ & ~((bishop_ray | rook_ray) & white);

        bishops &= BishopMagics[info->king].attacks(nocc);
        rooks   &= RookMagics[info->king].attacks(nocc);

        for (; bits(bishops)) info->vpinned |= LineBetween[info->king][trailing_zeros(bishops)];
        for (; bits(rooks))   info->hpinned |= LineBetween[info->king][trailing_zeros(rooks)];
}


// Generate all legal moves for a given position. It is assumed that Board itself is a legal
// position, otherwise UB may occur (assumptions that we have a king may no longer be true).

MoveBuffer generate_moves(Board& board)
{
        MoveBuffer buffer; // unitialised for performance
        MoveGenerationInfo info = {};

        buffer.size = 0;
        buffer.pawn_pushes = 0; // This must be zeroed in case of an early exit and pawn moves aren't generated

        info.king = trailing_zeros(board.extract_by_piece(King) & board.our);
        BitBoard checks = 0;

        info.en_passant = board.our &~ board.occupied();
        info.targets = ~(board.occupied() & board.our); // cannot capture own pieces
        info.attacked = enemy_attacked(board, &checks);
        generate_pinned(board, &info, &checks);

        // If we are in check from more than one piece, then we can only move king otherwise
        // we must block the check, or capture the checking piece

        if (popcount(checks) == 2) goto king_moves;
        if (checks) info.targets &= LineBetween[info.king][trailing_zeros(checks)];

        // Generate moves of pinned pieces, note: pinned Knights can never move
        if ((info.hpinned | info.vpinned) & board.our) {
                generate_piece_moves(buffer, board, info, Rook,   true);
                generate_piece_moves(buffer, board, info, Bishop, true);
        }

        // Generate regular moves for non-pinned pieces
        generate_pawn_moves (buffer, board, info);
        generate_piece_moves(buffer, board, info, Knight, false);
        generate_piece_moves(buffer, board, info, Bishop, false);
        generate_piece_moves(buffer, board, info, Rook,   false);
        generate_piece_moves(buffer, board, info, Queen,  false);

king_moves:
        generate_king_moves(buffer, board, info);
        return buffer;
}


// Make a legal move on the board state and update it. Note: like generate_moves, this function
// also assumes that both board and move are legal.


Board make_move(Board board, Move move)
{
        Square init = M_INIT(move);
        Square dest = M_DEST(move);
        PieceType piece = M_PIECE(move);

        auto clear = OneBB << init | OneBB << dest;

        auto occ = board.occupied();
        auto en_passant = board.our &~ occ;

        // Remove captured en-passant pawn & castling rook
        if (piece == Pawn)	clear |= south(en_passant & clear);
        if (move & M_CASTLING_MASK) clear |= (dest < init) ? (1 << A1) : (1 << H1);

        // Clear necessary bits and set piece on dest Square
        board.x     &= ~clear;
        board.y     &= ~clear;
        board.z     &= ~clear;

        set_square(&board, dest, piece);
        if (move & M_CASTLING_MASK) set_square(&board, (dest + init) >> 1, Rook);
        if (piece == King)	board.x ^= board.extract_by_piece(Castle) & Rank1BB; // remove castling rights

        // Flip white BitBoard to black and update en-passant Square
        BitBoard black = board.occupied() &~ board.our;

        // Rotate BitBoards to be from black's perspective
        board.x   = rotate(board.x);
        board.y   = rotate(board.y);
        board.z   = rotate(board.z);
        board.our = rotate(black);

        return board;
}


Board make_pawn_push(Board board, Square dest)
{
        auto occ = board.occupied();
        auto black = occ &~ board.our;

        auto mask = OneBB << dest;
        auto down = south(mask);

        // case of double pawn move
        if (down &~ occ)  black |= down, down = south(down);

        mask |= down;
        board.x ^= mask;

        board.x   = rotate(board.x);
        board.y   = rotate(board.y);
        board.z   = rotate(board.z);
        board.our = rotate(black);

        return board;
}


/*
 *   For faster perft, at leaf nodes we only have to count the number of legal moves, and don't
 *   need to contruct the actual moves. This code is mostly a copy of above, but optimised for
 *   only counting.
 */


uint64_t count_pawn_moves(Board& board, MoveGenerationInfo& info)
{
        auto pawns   = board.extract_by_piece(Pawn) & board.our;
        auto occ     = board.occupied();
        auto enemy   = occ &~ board.our;
        auto targets = info.targets;

        // Check for pinned en-passant. Note that this is a special type of pinned piece as two
        // pieces dissappear in the checking direction. This introduces a slow branch into our pawn
        // move generation, but it is a necessary evil for full legality, however rare.

        auto candidates = pawns & south(east(info.en_passant) | west(info.en_passant));

        // We optimise this branch by only checking if the king is actually on the 5th rank
        if ((info.king & 56) == 32 && popcount(candidates) == 1) {
                BitBoard pinners = (board.extract_by_piece(Rook) | board.extract_by_piece(Queen)) &~ board.our;
                BitBoard clear = candidates | south(info.en_passant);

                // If the pawn is "double" pinned, then en-passant is no longer possible
                if (RookMagics[info.king].attacks( (occ | info.en_passant) &~ clear) & pinners)
                        info.en_passant = 0;
        }

        // enable en-passant if the pawn being captured was giving check
        targets |= info.en_passant & north(info.targets);
        enemy   |= info.en_passant;

        auto pinned = info.hpinned | info.vpinned;
        auto normal_pawns = pawns &~ pinned;
        auto pinned_pawns = pawns & pinned;

        auto file = file_of(info.king); // only pinned pawns on same file as king can move forward
        auto forward = normal_pawns | (pinned_pawns & file);

        auto single_move = north(forward) &~ occ;
        auto double_move = north(single_move & Rank3BB) &~ occ;

        auto east_capture = north(east(normal_pawns)) & enemy;
        auto west_capture = north(west(normal_pawns)) & enemy;

        auto pinned_east_capture = north(east(pawns & info.vpinned)) & enemy & info.vpinned;
        auto pinned_west_capture = north(west(pawns & info.vpinned)) & enemy & info.vpinned;

        single_move  = single_move & targets;
        double_move  = double_move & targets;
        east_capture = (east_capture | pinned_east_capture) & targets;
        west_capture = (west_capture | pinned_west_capture) & targets;

        uint64_t count = popcount( (single_move &~ Rank8BB) | double_move );

        // promotions, note: double moves cannot promote
        count += 4 * popcount(single_move  & Rank8BB);
        count += 4 * popcount(east_capture & Rank8BB);
        count += 4 * popcount(west_capture & Rank8BB);

        count += popcount(east_capture &~ Rank8BB);
        count += popcount(west_capture &~ Rank8BB);

        return count;
}


uint64_t count_piece_moves(Board& board, MoveGenerationInfo& info, PieceType piece, bool pinned)
{
        auto _pinned = info.hpinned | info.vpinned;
        if (pinned) _pinned = (piece == Bishop) ? info.vpinned : info.hpinned;

        auto occ    = board.occupied();
        auto pieces = board.extract_by_piece(piece);
        auto queens = board.extract_by_piece(Queen);
        if (pinned) pieces |= queens;

        pieces &= board.our & (pinned ? _pinned : ~_pinned);
        uint64_t count = 0;

        for (; bits(pieces)) {
                auto init = trailing_zeros(pieces);
                auto attacks = generic_attacks(piece, init, occ) & info.targets;

                // If the piece is pinned, then the moves must remain aligned to the king.
                if (pinned) attacks &= _pinned;
                count += popcount(attacks);
        }

        return count;
}


uint64_t count_king_moves(Board& board, MoveGenerationInfo& info)
{
        BitBoard occ = board.occupied();
        BitBoard attacks = KingAttacks[info.king] &~ (info.attacked | (board.our & occ));

        uint64_t count = popcount(attacks);

        auto castling = board.extract_by_piece(Castle)
                & RookMagics[info.king].attacks(occ);

// Special BitBoards to check castling against. The OCC BitBoards must not be occupied, and
// the ATT BitBoards must not be attacked, as castling out-of, through or into check is not
// allowed.

#define QATT  (1 << C1 | 1 << D1 | 1 << E1)
#define KATT  (1 << E1 | 1 << F1 | 1 << G1)

        if (castling & (1 << A1) && !(info.attacked & QATT))  ++count;
        if (castling & (1 << H1) && !(info.attacked & KATT))  ++count;

        return count;
}


uint64_t count_moves(Board& board)
{
        MoveGenerationInfo info = {};

        info.king = trailing_zeros(board.extract_by_piece(King) & board.our);
        BitBoard checks = 0;

        info.en_passant = board.our &~ board.occupied();
        info.targets = ~(board.occupied() & board.our); // cannot capture own pieces
        info.attacked = enemy_attacked(board, &checks);
        generate_pinned(board, &info, &checks);

        // If we are in check from more than one piece, then we can only move king otherwise
        // we must block the check, or capture the checking piece
        uint64_t count = count_king_moves(board, info);

        if (popcount(checks) == 2) return count;
        if (checks) info.targets &= LineBetween[info.king][trailing_zeros(checks)];

        // Generate moves of pinned pieces, note: pinned Knights can never move
        if ((info.hpinned | info.vpinned) & board.our) {
                count += count_piece_moves(board, info, Rook,   true);
                count += count_piece_moves(board, info, Bishop, true);
        }

        // Generate regular moves for non-pinned pieces
        count += count_pawn_moves (board, info);
        count += count_piece_moves(board, info, Knight, false);
        count += count_piece_moves(board, info, Bishop, false);
        count += count_piece_moves(board, info, Rook,   false);
        count += count_piece_moves(board, info, Queen,  false);

        return count;
}

// FIXME: this file is in need of more comments

#include "bitboard.h"
#include "board.h"
#include "magic.h"
#include "movegen.h"


// Some useful info to pass around to move generation
struct MoveGenerationInfo {
        BitBoard attacked;
        BitBoard targets;
        BitBoard pinned_diagonally;
        BitBoard pinned_orthogonally;
        Square   king;
};


// Generate pawn moves from a move mask, from a given direction. This allows us to
// generate in more predictable loops.

void generate_partial_pawn_moves(MoveBuffer& buffer, BitBoard moves, Square direction, bool promotion)
{
        while (moves) {
                auto dest = trailing_zeros_and_pop(moves);
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

void generate_pawn_moves(MoveBuffer& buffer, Board const& board, MoveGenerationInfo const& info)
{
        auto pawns   = board.extract_by_piece(Pawn) & board.our;
        auto occ     = board.occupied();
        auto enemy   = occ &~ board.our;
        auto targets = info.targets;

        // Check for pinned en-passant. Note that this is a special type of pinned piece as two
        // pieces dissappear in the checking direction. This introduces a slow branch into our pawn
        // move generation, but it is a necessary evil for full legality, however rare.

        auto en_passant = board.en_passant();
        auto candidates = pawns & south(east(en_passant) | west(en_passant));

        // We optimise this branch by only checking if the king is actually on the 5th rank
        if (info.king / 8 == 4 && popcount(candidates) == 1) {
                auto pinners = (board.extract_by_piece(Rook) | board.extract_by_piece(Queen)) &~ board.our;
                auto clear = candidates | south(en_passant);

                // If the pawn is "double" pinned, then en-passant is no longer possible
                if (RookMagics[info.king].attacks(occ &~ clear) & pinners)
                        en_passant = 0;
        }

        // enable en-passant if the pawn being captured was giving check
        targets |= en_passant & north(info.targets);
        enemy   |= en_passant;

        auto pinned = info.pinned_orthogonally | info.pinned_diagonally;
        auto normal_pawns = pawns &~ pinned;
        auto pinned_pawns = pawns & pinned;

        auto file = file_of(info.king); // only pinned pawns on same file as king can move forward
        auto forward = normal_pawns | (pinned_pawns & file);

        auto single_move = north(forward) &~ occ;
        auto double_move = north(single_move & Rank3BB) &~ occ;

        auto east_capture = north(east(normal_pawns)) & enemy;
        auto west_capture = north(west(normal_pawns)) & enemy;

        auto pinned_east_capture = north(east(pawns & info.pinned_diagonally)) & enemy & info.pinned_diagonally;
        auto pinned_west_capture = north(west(pawns & info.pinned_diagonally)) & enemy & info.pinned_diagonally;

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


void generate_piece_moves(MoveBuffer& buffer, Board const& board, MoveGenerationInfo const& info, PieceType piece)
{
        auto pinned = info.pinned_diagonally | info.pinned_orthogonally;
        auto pieces = board.extract_by_piece(piece) & board.our &~ pinned;

        while (pieces) {
                auto init = trailing_zeros_and_pop(pieces);
                auto attacks = generic_attacks(piece, init, board.occupied()) & info.targets;

                while (attacks) {
                        Square dest = trailing_zeros_and_pop(attacks);
                        buffer.push(M(init, dest, piece));
                }
        }
}


void generate_pinned_piece_moves(MoveBuffer& buffer, Board const& board, MoveGenerationInfo const& info, PieceType moves_like)
{
        auto pinned = (moves_like == Bishop) ? info.pinned_diagonally : info.pinned_orthogonally;

        auto pieces = board.extract_by_piece(moves_like);
        auto queens = board.extract_by_piece(Queen);

        pieces |= queens;
        pieces &= board.our & pinned;

        while (pieces) {
                auto init = trailing_zeros_and_pop(pieces);
                auto attacks = generic_attacks(moves_like, init, board.occupied()) & info.targets & pinned;
                auto actual_piece = (queens & (OneBB << init)) ? Queen : moves_like;

                while (attacks) {
                        Square dest = trailing_zeros_and_pop(attacks);
                        buffer.push(M(init, dest, actual_piece));
                }
        }
}


// Generate king moves. We use a specialised function rather than the one above as we always have
// exactly one king so the outer loop can be optimised away. Here we also clear the attacked mask
// to prevent our king from walking into check.

void generate_king_moves(MoveBuffer& buffer, Board const& board, MoveGenerationInfo const& info)
{
        auto attacks = KingAttacks[info.king] & info.targets;
        attacks &= ~info.attacked;

        while (attacks) {
                auto dest = trailing_zeros_and_pop(attacks);
                buffer.push(M(info.king, dest, King));
        }

        if (info.king != E1) return;

        auto castling = board.extract_by_piece(Castle)
                & RookMagics[info.king].attacks(board.occupied());

        // Special BitBoards to check castling against. The OCC BitBoards must not be occupied, and
        // the ATT BitBoards must not be attacked, as castling out-of, through or into check is not
        // allowed.

        constexpr BitBoard QATT = (1 << C1 | 1 << D1 | 1 << E1);
        constexpr BitBoard KATT = (1 << E1 | 1 << F1 | 1 << G1);

        if (castling & (1 << A1) && !(info.attacked & QATT))  buffer.push(M_CASTLING(C1));
        if (castling & (1 << H1) && !(info.attacked & KATT))  buffer.push(M_CASTLING(G1));
}


BitBoard generate_movegen_info(Board const& board, MoveGenerationInfo& info)
{
        // We cannot capture our own pieces!
        info.targets = ~(board.occupied() & board.our);

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
        auto checks = EmptyBB;

        auto our_king = board.extract_by_piece(King) & board.our;
        info.king = trailing_zeros(our_king);

        auto occ = board.occupied() &~ our_king; // allows sliders to x-ray through our king
        auto blockers = occ & board.our;

        auto king_diagonals = BishopMagics[info.king].attacks(occ);
        auto king_orthogonals = RookMagics[info.king].attacks(occ);
        auto remove_blockers = occ &~ ((king_diagonals | king_orthogonals) & blockers);

        // Simple non-sliding moves
        attacked |= south(east(pawns) | west(pawns));
        attacked |= KingAttacks[trailing_zeros(king)];

        checks |= pawns & north(east(our_king) | west(our_king));
        checks |= knights & KnightAttacks[trailing_zeros(our_king)];
        checks |= bishops & king_diagonals;
        checks |= rooks & king_orthogonals;

        auto bishop_pins = bishops & BishopMagics[info.king].attacks(remove_blockers);
        auto rook_pins = rooks & RookMagics[info.king].attacks(remove_blockers);

        // Unroll knight loop to two iterations for performance.
        // As we force pext, we have bmi2 so tzcnt is guaranteed to give 64 on an argument input of zero.
        while (knights) {
                attacked |= KnightAttacks[trailing_zeros(knights)];
                knights &= knights - 1;

                attacked |= KnightAttacks[trailing_zeros(knights)];
                knights &= knights - 1;
        }

        while (bishops)  attacked |= BishopMagics[trailing_zeros_and_pop(bishops)].attacks(occ);
        while (rooks)    attacked |= RookMagics[trailing_zeros_and_pop(rooks)].attacks(occ);

        info.attacked = attacked;
        info.pinned_diagonally = 0;
        info.pinned_orthogonally = 0;

        while (bishop_pins) info.pinned_diagonally |= LineBetween[info.king][trailing_zeros_and_pop(bishop_pins)];
        while (rook_pins)   info.pinned_orthogonally |= LineBetween[info.king][trailing_zeros_and_pop(rook_pins)];

        return checks;
}


// Generate all legal moves for a given position. It is assumed that board itself is a legal
// position, otherwise UB may occur (assumptions that we have a king may no longer be true).

MoveBuffer generate_moves(Board const& board)
{
        MoveBuffer buffer; // unitialised for performance
        MoveGenerationInfo info;

        buffer.size = 0;
        buffer.pawn_pushes = 0; // This must be zeroed in case of an early exit and pawn moves aren't generated

        auto checks = generate_movegen_info(board, info);
        generate_king_moves(buffer, board, info);

        // If we are in check from more than one piece, then we can only move king otherwise
        // we must block the check, or capture the checking piece

        if (popcount(checks) > 1) return buffer;
        if (checks) info.targets &= LineBetween[info.king][trailing_zeros(checks)];

        // Generate moves of pinned pieces, note: pinned Knights can never move
        if ((info.pinned_orthogonally | info.pinned_diagonally) & board.our) {
                generate_pinned_piece_moves(buffer, board, info, Bishop);
                generate_pinned_piece_moves(buffer, board, info, Rook);
        }

        // Generate regular moves for non-pinned pieces
        generate_pawn_moves (buffer, board, info);
        generate_piece_moves(buffer, board, info, Knight);
        generate_piece_moves(buffer, board, info, Bishop);
        generate_piece_moves(buffer, board, info, Rook);
        generate_piece_moves(buffer, board, info, Queen);
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

        auto mask = OneBB << dest;

        board.our |= mask;
        if (piece & 0b001) board.x |= mask;
        if (piece & 0b010) board.y |= mask;
        if (piece & 0b100) board.z |= mask;

        if (move & M_CASTLING_MASK) {
                auto castling_mask = OneBB << ((dest + init) / 2);
                board.our |= castling_mask;

                static_assert(Rook == 0b100, "required bit pattern");
                board.z |= castling_mask;
        }

        if (piece == King) {
                static_assert(Rook   == 0b100, "required bit pattern");
                static_assert(Castle == 0b101, "required bit pattern");

                // When the king moves for the first time, all castling is no longer allowed,
                // so we toggle our Castles to Rooks by flipping the `x` bit.
                board.x -= board.extract_by_piece(Castle) & Rank1BB;
        }

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


uint64_t count_pawn_moves(Board const& board, MoveGenerationInfo const& info)
{
        auto pawns   = board.extract_by_piece(Pawn) & board.our;
        auto occ     = board.occupied();
        auto enemy   = occ &~ board.our;
        auto targets = info.targets;

        // Check for pinned en-passant. Note that this is a special type of pinned piece as two
        // pieces dissappear in the checking direction. This introduces a slow branch into our pawn
        // move generation, but it is a necessary evil for full legality, however rare.

        auto en_passant = board.en_passant();
        auto candidates = pawns & south(east(en_passant) | west(en_passant));

        // We optimise this branch by only checking if the king is actually on the 5th rank
        if (info.king / 8 == 4 && popcount(candidates) == 1) {
                auto pinners = (board.extract_by_piece(Rook) | board.extract_by_piece(Queen)) &~ board.our;
                auto clear = candidates | south(en_passant);

                // If the pawn is "double" pinned, then en-passant is no longer possible
                if (RookMagics[info.king].attacks(occ &~ clear) & pinners)
                        en_passant = 0;
        }

        // enable en-passant if the pawn being captured was giving check
        targets |= en_passant & north(info.targets);
        enemy   |= en_passant;

        auto pinned = info.pinned_orthogonally | info.pinned_diagonally;
        auto normal_pawns = pawns &~ pinned;
        auto pinned_pawns = pawns & pinned;

        auto file = file_of(info.king); // only pinned pawns on same file as king can move forward
        auto forward = normal_pawns | (pinned_pawns & file);

        auto single_move = north(forward) &~ occ;
        auto double_move = north(single_move & Rank3BB) &~ occ;

        auto east_capture = north(east(normal_pawns)) & enemy;
        auto west_capture = north(west(normal_pawns)) & enemy;

        auto pinned_east_capture = north(east(pawns & info.pinned_diagonally)) & enemy & info.pinned_diagonally;
        auto pinned_west_capture = north(west(pawns & info.pinned_diagonally)) & enemy & info.pinned_diagonally;

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


uint64_t count_piece_moves(Board const& board, MoveGenerationInfo const& info, PieceType piece)
{
        auto pinned = info.pinned_diagonally | info.pinned_orthogonally;
        auto pieces = board.extract_by_piece(piece) & board.our &~ pinned;

        uint64_t count = 0;

        while (pieces) {
                auto init = trailing_zeros_and_pop(pieces);
                auto attacks = generic_attacks(piece, init, board.occupied()) & info.targets;
                count += popcount(attacks);
        }

        return count;
}


uint64_t count_pinned_piece_moves(Board const& board, MoveGenerationInfo const& info, PieceType moves_like)
{
        auto pinned = (moves_like == Bishop) ? info.pinned_diagonally : info.pinned_orthogonally;

        auto pieces = board.extract_by_piece(moves_like);
        auto queens = board.extract_by_piece(Queen);

        pieces |= queens;
        pieces &= board.our & pinned;

        uint64_t count = 0;

        while (pieces) {
                auto init = trailing_zeros_and_pop(pieces);
                auto attacks = generic_attacks(moves_like, init, board.occupied()) & info.targets;

                attacks &= pinned;
                count += popcount(attacks);
        }

        return count;
}


uint64_t count_king_moves(Board const& board, MoveGenerationInfo const& info)
{
        auto attacks = KingAttacks[info.king] & info.targets;
        attacks &= ~info.attacked;

        uint64_t count = popcount(attacks);
        if (info.king != E1) return count;

        auto castling = board.extract_by_piece(Castle)
                & RookMagics[info.king].attacks(board.occupied());

        // Special BitBoards to check castling against. The OCC BitBoards must not be occupied, and
        // the ATT BitBoards must not be attacked, as castling out-of, through or into check is not
        // allowed.

        constexpr BitBoard QATT = (1 << C1 | 1 << D1 | 1 << E1);
        constexpr BitBoard KATT = (1 << E1 | 1 << F1 | 1 << G1);

        if (castling & (1 << A1) && !(info.attacked & QATT))  ++count;
        if (castling & (1 << H1) && !(info.attacked & KATT))  ++count;

        return count;
}


uint64_t count_moves(Board const& board)
{
        MoveGenerationInfo info;
        auto checks = generate_movegen_info(board, info);

        // If we are in check from more than one piece, then we can only move king otherwise
        // we must block the check, or capture the checking piece
        uint64_t count = count_king_moves(board, info);

        if (popcount(checks) > 1) return count;
        if (checks) info.targets &= LineBetween[info.king][trailing_zeros(checks)];

        // Generate moves of pinned pieces, note: pinned Knights can never move
        if ((info.pinned_orthogonally | info.pinned_diagonally) & board.our) {
                count += count_pinned_piece_moves(board, info, Bishop);
                count += count_pinned_piece_moves(board, info, Rook);
        }

        // Generate regular moves for non-pinned pieces
        count += count_pawn_moves (board, info);
        count += count_piece_moves(board, info, Knight);
        count += count_piece_moves(board, info, Bishop);
        count += count_piece_moves(board, info, Rook);
        count += count_piece_moves(board, info, Queen);

        return count;
}

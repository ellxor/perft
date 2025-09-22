#include "bitboard.h"
#include "board.h"
#include "magic.h"
#include "movegen.h"

/*
 *   Information that is passed around to move generation functions.
 *   It stores (in order of definition):
 *
 *   - all squares attacked by enemy pieces to prevent illegal king walks
 *   - all squares piece *must* move to, this is to block checks, or capture checking
 *     pieces, etc...
 *   - all squares that are pinned diagonally (see brackets below)
 *   - all squares that are pinned orthogonally (not necessarily our or even occupied)
 *   - square that our king is on
 */

struct MoveGenerationInfo {
        BitBoard attacked;
        BitBoard targets;
        BitBoard pinned_diagonally;
        BitBoard pinned_orthogonally;
        Square   king;
};


// Generate pawn moves from a move mask, from a given direction. This allows us to
// generate in more predictable loops.

void partially_generate_pawn_moves(MoveBuffer& buffer, BitBoard moves, Square direction, bool promotion)
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


void generate_pawn_moves(MoveBuffer& buffer, Board const& board, MoveGenerationInfo const& info)
{
        auto pawns   = board.extract_by_piece(Pawn) & board.our;
        auto occ     = board.occupied();
        auto enemy   = occ &~ board.our;
        auto targets = info.targets;

        auto en_passant = board.en_passant();
        auto candidates = pawns & south(east(en_passant) | west(en_passant));

        // Check for pinned en-passant. Note that this is a special type of pinned piece as two
        // pieces dissappear in the checking direction. This introduces a slow branch into our pawn
        // move generation, but it is a necessary evil for full legality, however rare. We optimise this
        // branch by only checking if the king is actually on the 5th rank.

        if (info.king / 8 == 4 && popcount(candidates) == 1) {
                auto pinners = (board.extract_by_piece(Rook) | board.extract_by_piece(Queen)) &~ board.our;
                auto clear = candidates | south(en_passant);

                // If the pawn is "double" pinned, then en-passant is no longer possible
                if (RookMagics[info.king].attacks(occ &~ clear) & pinners)
                        en_passant = 0;
        }

        // Enable en-passant if the pawn being captured was giving check.
        targets |= en_passant & north(info.targets);
        enemy   |= en_passant;

        auto pinned = info.pinned_diagonally | info.pinned_orthogonally;
        auto unpinned_pawns = pawns &~ pinned;

        // The only pinned pawns that can move foward are on same file as our king.
        auto file = file_of(info.king);
        auto forward = unpinned_pawns | (pawns & info.pinned_orthogonally & file);

        auto single_move = north(forward) &~ occ;
        auto double_move = north(single_move & Rank3BB) &~ occ;

        auto east_capture = north(east(unpinned_pawns)) & enemy;
        auto west_capture = north(west(unpinned_pawns)) & enemy;

        // Again as with foward, we constrain pinned pawns capturing to staying diagonal to the king.
        // This is a sufficient condition for legality.
        auto pinned_east_capture = north(east(pawns & info.pinned_diagonally)) & enemy & info.pinned_diagonally;
        auto pinned_west_capture = north(west(pawns & info.pinned_diagonally)) & enemy & info.pinned_diagonally;

        single_move  = single_move & targets;
        double_move  = double_move & targets;
        east_capture = (east_capture | pinned_east_capture) & targets;
        west_capture = (west_capture | pinned_west_capture) & targets;

        buffer.pawn_pushes = (single_move &~ Rank8BB) | double_move;

        // Handle promotions, note that double pawn moves cannot promote.
        partially_generate_pawn_moves(buffer, single_move  & Rank8BB, North,   true);
        partially_generate_pawn_moves(buffer, east_capture & Rank8BB, North+East, true);
        partially_generate_pawn_moves(buffer, west_capture & Rank8BB, North+West, true);

        partially_generate_pawn_moves(buffer, east_capture &~ Rank8BB, North+East, false);
        partially_generate_pawn_moves(buffer, west_capture &~ Rank8BB, North+West, false);
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
                        auto dest = trailing_zeros_and_pop(attacks);
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

                // As with pinned pawns, as long as the moves stay on a square that is pinned, then it is
                // enough to satisfy legality. Note that for this to hold orthogonal and diagonal pins are
                // separated.

                auto attacks = generic_attacks(moves_like, init, board.occupied()) & info.targets & pinned;
                auto actual_piece = (queens & (OneBB << init)) ? Queen : moves_like;

                while (attacks) {
                        auto dest = trailing_zeros_and_pop(attacks);
                        buffer.push(M(init, dest, actual_piece));
                }
        }
}


void generate_king_moves(MoveBuffer& buffer, Board const& board, MoveGenerationInfo const& info)
{
        auto attacks = KingAttacks[info.king] & info.targets;
        attacks &= ~info.attacked;

        while (attacks) {
                auto dest = trailing_zeros_and_pop(attacks);
                buffer.push(M(info.king, dest, King));
        }

        // If our king is not on E1, it must have moved, so castling of any kind is no longer possible.
        // So we safely can optimise with an early return.
        if (info.king != E1) return;

        // Get a mask of rooks we can castle with, and that there are no occupied squares between our
        // king and that rook.
        auto castling = board.extract_by_piece(Castle)
                      & RookMagics[info.king].attacks(board.occupied());

        // We also then check that none of the squares between the king and the rook, including the
        // king's square itself, are attacked. Note that castling our of check is illegal.
        constexpr auto QueensideInbetween = (OneBB << C1 | OneBB << D1 | OneBB << E1);
        constexpr auto KingsideInbetween = (OneBB << E1 | OneBB << F1 | OneBB << G1);

        if (castling & (OneBB << A1) && !(QueensideInbetween & info.attacked)) buffer.push(M_CASTLING(C1));
        if (castling & (OneBB << H1) && !(KingsideInbetween & info.attacked))  buffer.push(M_CASTLING(G1));
}


BitBoard generate_movegen_info(Board const& board, MoveGenerationInfo& info)
{
        // We cannot capture our own pieces!
        info.targets = ~(board.occupied() & board.our);

        // Get all enemy pieces
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

        // For generating the enemy attacks, we allow sliding pieces (bishops,rook,queens)
        // to move through our king. This prevents the king stepping back illegally along
        // an attacked ray and accidentally block the check from where it previously was.
        auto occ = board.occupied() &~ our_king;
        auto blockers = occ & board.our;

        auto king_diagonals = BishopMagics[info.king].attacks(occ);
        auto king_orthogonals = RookMagics[info.king].attacks(occ);

        // Generate all pieces that are putting our king in check.
        checks |= pawns & north(east(our_king) | west(our_king));
        checks |= knights & KnightAttacks[trailing_zeros(our_king)];
        checks |= bishops & king_diagonals;
        checks |= rooks & king_orthogonals;

        // Strip away the first line of our pieces that could potentially be blocking a check (i.e. pinned).
        auto remove_blockers = occ &~ ((king_diagonals | king_orthogonals) & blockers);

        // Generate attacks of simple non-sliding moves.
        attacked |= south(east(pawns) | west(pawns));
        attacked |= KingAttacks[trailing_zeros(king)];

        // Unroll knight loop to two iterations for performance.
        // Note if (knights == 0) then trailing zeros gives 64, and KnightAttacks[64] is the empty bitboard.
        while (knights) {
                attacked |= KnightAttacks[trailing_zeros_and_pop(knights)];
                attacked |= KnightAttacks[trailing_zeros_and_pop(knights)];
        }

        // Get all bishops, rooks and queens that are x-raying our king. Note we calculate this before
        // finding the attacked squares of these piece types below as that would destroy these bitboards.
        auto bishop_pins = bishops & BishopMagics[info.king].attacks(remove_blockers);
        auto rook_pins = rooks & RookMagics[info.king].attacks(remove_blockers);

        while (bishops) attacked |= BishopMagics[trailing_zeros_and_pop(bishops)].attacks(occ);
        while (rooks)   attacked |= RookMagics[trailing_zeros_and_pop(rooks)].attacks(occ);

        info.attacked = attacked;

        // Generate pinned vectors by looping over all x-rays and generating the line between those pieces and
        // our king. Note that this may include empty squares as well as enemy pieces, but we don't care as we
        // only use these as masks when generating moves for our own pieces.
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

        // Initialise buffer. Note that pawn_pushes must be zeroed in case of an early exit,
        // caused by double check, and pawn moves aren't generated.
        buffer.size = 0;
        buffer.pawn_pushes = 0;

        auto checks = generate_movegen_info(board, info);
        generate_king_moves(buffer, board, info);

        // If we are in check from more than one piece, then we can only move king otherwise
        // we must block the check, or capture the checking piece
        if (popcount(checks) > 1) return buffer;
        if (checks) info.targets &= LineBetween[info.king][trailing_zeros(checks)];

        generate_pawn_moves(buffer, board, info);

        // Generate regular moves for non-pinned pieces
        generate_piece_moves(buffer, board, info, Knight);
        generate_piece_moves(buffer, board, info, Bishop);
        generate_piece_moves(buffer, board, info, Rook);
        generate_piece_moves(buffer, board, info, Queen);

        // Generate moves of pinned pieces, note: pinned knights can never move
        if ((info.pinned_orthogonally | info.pinned_diagonally) & board.our) {
                generate_pinned_piece_moves(buffer, board, info, Bishop);
                generate_pinned_piece_moves(buffer, board, info, Rook);
        }

        return buffer;
}


// Make a legal move on the board state and update it. Note: like generate_moves, this function
// also assumes that both board and move are legal.

Board make_move(Board board, Move move)
{
        Square init = M_INIT(move);
        Square dest = M_DEST(move);
        PieceType piece = M_PIECE(move);

        // We make sure to clear the destination square in case of a capture.
        auto clear = OneBB << init | OneBB << dest;

        // Remove captured en-passant pawn in piece moving is a pawn - only way it can
        // get to the en-passant square is by capturing it.
        if (piece == Pawn) {
                clear |= south(board.en_passant() & clear);
        }

        // After checking en-passant it is safe to find where enemy pieces will be after
        // the move has been made.
        auto enemy = board.occupied() &~ (board.our | clear);

        // When the king moves for the first time, all castling is no longer allowed.
        if (piece == King) {
                static_assert(Rook   == 0b100, "required bit pattern");
                static_assert(Castle == 0b101, "required bit pattern");

                // Toggle our Castles to Rooks by flipping the `x` bit.
                board.x -= board.extract_by_piece(Castle) & Rank1BB;
        }

        // Move rook that is being castled with in the case of castling.
        if (move & M_CASTLING_MASK) {
                static_assert(Rook == 0b100, "required bit pattern");

                // Remove rook by adding it to the clear mask.
                clear |= (dest < init) ? OneBB << A1 : OneBB << H1;

                // And set it on the middle of the init and dest squares.
                auto castling_mask = OneBB << ((dest + init) / 2);
                board.z   |= castling_mask;
        }

        // Clear necessary bits
        board.x &= ~clear;
        board.y &= ~clear;
        board.z &= ~clear;

        // Move piece to the destination square
        if (piece & 0b001) board.x |= OneBB << dest;
        if (piece & 0b010) board.y |= OneBB << dest;
        if (piece & 0b100) board.z |= OneBB << dest;

        // Rotate BitBoards to be from black's perspective
        board.x   = rotate(board.x);
        board.y   = rotate(board.y);
        board.z   = rotate(board.z);
        board.our = rotate(enemy);

        return board;
}

// Specialised function for making a simple pawn push (non-promotion). Like with the
// movebuffer, the tend to make up a lot of the legal moves in a position, so it's
// good for performance to have a fast branch for such moves.

Board make_pawn_push(Board board, Square dest)
{
        auto occupied = board.occupied();
        auto enemy = occupied &~ board.our; // pawn push can never capture.

        auto dest_bitboard = OneBB << dest;
        auto init_bitboard = south(OneBB << dest);

        // Check if case of double pawn move.
        if (init_bitboard &~ occupied) {
                enemy |= init_bitboard; // set the en-passant flag
                init_bitboard = south(init_bitboard);
        }

        static_assert(Pawn == 0b001, "required bit pattern");
        board.x ^= (dest_bitboard | init_bitboard); // toggle pawns

        board.x   = rotate(board.x);
        board.y   = rotate(board.y);
        board.z   = rotate(board.z);
        board.our = rotate(enemy);

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

        auto pinned = info.pinned_diagonally | info.pinned_orthogonally;
        auto unpinned_pawns = pawns &~ pinned;

        auto file = file_of(info.king); // only pinned pawns on same file as king can move forward
        auto forward = unpinned_pawns | (pawns & info.pinned_orthogonally & file);

        auto single_move = north(forward) &~ occ;
        auto double_move = north(single_move & Rank3BB) &~ occ;

        auto east_capture = north(east(unpinned_pawns)) & enemy;
        auto west_capture = north(west(unpinned_pawns)) & enemy;

        auto pinned_east_capture = north(east(pawns & info.pinned_diagonally)) & enemy & info.pinned_diagonally;
        auto pinned_west_capture = north(west(pawns & info.pinned_diagonally)) & enemy & info.pinned_diagonally;

        single_move  = single_move & targets;
        double_move  = double_move & targets;
        east_capture = (east_capture | pinned_east_capture) & targets;
        west_capture = (west_capture | pinned_west_capture) & targets;

        return popcount( (single_move &~ Rank8BB) | double_move )
             + popcount(east_capture &~ Rank8BB)
             + popcount(west_capture &~ Rank8BB)
             // Promotions count for 4 moves; knight, bishop, rook, queen.
             + popcount(single_move  & Rank8BB) * 4
             + popcount(east_capture & Rank8BB) * 4
             + popcount(west_capture & Rank8BB) * 4;
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
        uint64_t count = count_king_moves(board, info);

        if (popcount(checks) > 1) return count;
        if (checks) info.targets &= LineBetween[info.king][trailing_zeros(checks)];

        if ((info.pinned_orthogonally | info.pinned_diagonally) & board.our) {
                count += count_pinned_piece_moves(board, info, Bishop);
                count += count_pinned_piece_moves(board, info, Rook);
        }

        count += count_pawn_moves (board, info);
        count += count_piece_moves(board, info, Knight);
        count += count_piece_moves(board, info, Bishop);
        count += count_piece_moves(board, info, Rook);
        count += count_piece_moves(board, info, Queen);

        return count;
}

#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <threads.h>
#include <unistd.h>

#include "board.h"
#include "magic.h"
#include "movegen.h"

// Embed FEN parsing code
#include "fen.cc"


//  Unit-testing structure containing an FEN, and the (maximum) depth, as well as a list of expected
//  perft results at a given depth

typedef unsigned Depth;
typedef uint64_t Nodes;


struct PerftThreadInfo {
        Board board;
        Depth depth;
        Nodes result;
};


Nodes perft(Board& pos, unsigned depth)
{
        if (depth == 1) return count_moves(pos);
        auto buffer = generate_moves(pos);

        Nodes total = 0;

        for (size_t i = 0; i < buffer.size; i += 1) {
                auto child = make_move(pos, buffer.moves[i]);
                total += perft(child, depth - 1);
        }

        for (; bits(buffer.pawn_pushes)) {
                auto child = make_pawn_push(pos, trailing_zeros(buffer.pawn_pushes));
                total += perft(child, depth - 1);
        }

        return total;
}


int start_perft_thread(void* opaque_thread_info)
{
        assert(opaque_thread_info);

        auto* thread_info = (PerftThreadInfo*) opaque_thread_info;
        thread_info->result = perft(thread_info->board, thread_info->depth);

        return 0;
}


Nodes threaded_perft(Board& board, Depth depth)
{
        thrd_t threads[MaximumLegalMoves];
        PerftThreadInfo thread_info[MaximumLegalMoves];

        auto buffer = generate_moves(board);
        size_t thread_count = 0;

        for (size_t i = 0; i < buffer.size; ++i) {
                auto child = make_move(board, buffer.moves[i]);
                thread_info[thread_count] = { .board = child, .depth = depth - 1, .result = 0 };

                thrd_create(
                        &threads[thread_count],
                        start_perft_thread,
                        &thread_info[thread_count]
                );

                ++thread_count;
        }

        for (; bits(buffer.pawn_pushes)) {
                auto child = make_pawn_push(board, trailing_zeros(buffer.pawn_pushes));
                thread_info[thread_count] = { .board = child, .depth = depth - 1, .result = 0 };

                thrd_create(
                        &threads[thread_count],
                        start_perft_thread,
                        &thread_info[thread_count]
                );

                ++thread_count;
        }

        Nodes total = 0;

        for (size_t i = 0; i < thread_count; ++i) {
                thrd_join(threads[i], nullptr);
                total += thread_info[i].result;
        }

        return total;
}


double get_time_from_os() {
        timespec timestamp;
        clock_gettime(CLOCK_MONOTONIC, &timestamp);

        return timestamp.tv_sec + 1.0e-9 * timestamp.tv_nsec;
}


int main()
{
        auto cpu_core_count = sysconf(_SC_NPROCESSORS_ONLN);
        printf("Running multi-threaded Kiwipete perft on %ld cores.\n", cpu_core_count);

        init_bitboard_tables();

        bool white_to_move, ok;
        auto board = parse_fen(
                "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -",
                &white_to_move, &ok);

        assert(ok && white_to_move);

        Depth depth = 6;

#ifdef PROFILE
        --depth;
#endif

        auto t1 = get_time_from_os();
        auto nodes = threaded_perft(board, depth);
        auto t2 = get_time_from_os();

        auto nps = nodes / (t2 - t1);
        printf("Depth: %u, Nodes: %ld  (%.3f Gnps)\n", depth, nodes, nps / 1.0e9);
}

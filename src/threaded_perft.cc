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


typedef unsigned Depth;
typedef uint64_t Nodes;


struct PerftThreadInfo {
        Board* board_buffer;
        size_t buffer_size;

        Depth  depth;
        Nodes  result;
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
        assert(opaque_thread_info != nullptr);

        auto& thread_info = *(PerftThreadInfo*) opaque_thread_info;

        for (size_t i = 0; i < thread_info.buffer_size; ++i) {
                thread_info.result += perft(thread_info.board_buffer[i], thread_info.depth);
        }

        return 0;
}


void populate_position_pool(Board& board, Depth depth, Board position_pool[], size_t& position_pool_size)
{
        if (depth == 0) {
                position_pool[position_pool_size++] = board;
                return;
        }

        auto buffer = generate_moves(board);

        for (size_t i = 0; i < buffer.size; ++i) {
                auto child = make_move(board, buffer.moves[i]);
                populate_position_pool(child, depth - 1, position_pool, position_pool_size);
        }

        for (; bits(buffer.pawn_pushes)) {
                auto child = make_pawn_push(board, trailing_zeros(buffer.pawn_pushes));
                populate_position_pool(child, depth - 1, position_pool, position_pool_size);
        }
}


Nodes threaded_perft(Board& board, Depth depth, unsigned number_of_threads)
{
        constexpr size_t MAX_THREAD_COUNT = 64;
        constexpr Depth POPULATION_DEPTH = 2;

        assert(depth >= 2);
        assert(number_of_threads > 0);
        assert(number_of_threads <= MAX_THREAD_COUNT);

        static Board position_pool[10'000];
        size_t position_pool_size = 0;

        populate_position_pool(board, POPULATION_DEPTH, position_pool, position_pool_size);

        thrd_t threads[MAX_THREAD_COUNT];
        PerftThreadInfo thread_info[MAX_THREAD_COUNT];

        auto positions_per_thread = position_pool_size / number_of_threads;

        for (unsigned i = 0; i < number_of_threads; ++i) {
                auto begin = i * positions_per_thread;
                auto end = begin + positions_per_thread;

                if (i == number_of_threads - 1) {
                        end = position_pool_size;
                }

                thread_info[i] = {
                        .board_buffer = position_pool + begin,
                        .buffer_size  = end - begin,
                        .depth = depth - POPULATION_DEPTH,
                        .result = 0,
                };

                thrd_create(&threads[i], start_perft_thread, &thread_info[i]);
        }

        Nodes total = 0;

        for (unsigned i = 0; i < number_of_threads; ++i) {
                thrd_join(threads[i], nullptr);
                total += thread_info[i].result;
        }

        return total;
}


double get_time_from_os() {
        timespec timestamp;
        clock_gettime(CLOCK_REALTIME, &timestamp);

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

        Depth depth = 7;

#ifdef PROFILE
        depth = 5;
#endif

        // For some reason 32 threads works better than the cpu_core_count?

        auto t1 = get_time_from_os();
        auto nodes = threaded_perft(board, depth, 32);
        auto t2 = get_time_from_os();

        auto nps = nodes / (t2 - t1);
        printf("Depth: %u, Nodes: %ld  (%.3f Gnps)\n", depth, nodes, nps / 1.0e9);
}

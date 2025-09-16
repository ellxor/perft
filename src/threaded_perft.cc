#include <assert.h>
#include <stdatomic.h>
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

#define atomic _Atomic


typedef unsigned Depth;
typedef uint64_t Nodes;
typedef double Seconds;


struct PerftThreadInfo {
        Board*          board_buffer;
        size_t          buffer_size;
        atomic(size_t)  buffer_done;

        Depth           depth;
        atomic(Nodes)   result;
};


double get_time_from_os() {
        timespec timestamp;
        clock_gettime(CLOCK_REALTIME, &timestamp);

        return timestamp.tv_sec + 1.0e-9 * timestamp.tv_nsec;
}


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

        auto t1 = get_time_from_os();

        while (true) {
                size_t index = atomic_fetch_add(&thread_info.buffer_done, 1);
                if (index >= thread_info.buffer_size) break;

                auto& position = thread_info.board_buffer[index];

                Nodes nodes = perft(position, thread_info.depth);
                atomic_fetch_add(&thread_info.result, nodes);
        }

        auto t2 = get_time_from_os();
        auto seconds = t2 - t1;

        printf("Thread finished in %.3f seconds (%.3f Gnps).\n", t2 - t1, thread_info.result / seconds / 1.0e9);
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

        PerftThreadInfo info = {
                .board_buffer = position_pool,
                .buffer_size = position_pool_size,
                .depth = depth - POPULATION_DEPTH,
        };

        atomic_init(&info.buffer_done, 0);
        atomic_init(&info.result, 0);

        for (unsigned i = 0; i < number_of_threads; ++i) {
                thrd_create(&threads[i], start_perft_thread, &info);
        }

        for (unsigned i = 0; i < number_of_threads; ++i) {
                thrd_join(threads[i], nullptr);
        }

        return info.result;
}



int main()
{
        auto cpu_core_count = sysconf(_SC_NPROCESSORS_ONLN);
        printf("Running multi-threaded Kiwipete perft on %ld threads.\n", cpu_core_count);

        init_bitboard_tables();

        bool white_to_move, ok;
        auto board = parse_fen(
                "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -",
                &white_to_move, &ok);

        assert(ok && white_to_move);

        Depth depth = 7;

#ifdef PROFILE
        depth -= 2;
#endif

        auto t1 = get_time_from_os();
        auto nodes = threaded_perft(board, depth, cpu_core_count);
        auto t2 = get_time_from_os();

        auto nps = nodes / (t2 - t1);
        printf("Depth: %u, Nodes: %lu  (%.3f Gnps)\n", depth, nodes, nps / 1.0e9);
}

#include <atomic>
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <threads.h>
#include <unistd.h>

#include "board.h"
#include "magic.h"
#include "movegen.h"
#include "fen.cc" // Embed FEN parsing code

#define atomic(T) std::atomic<T>


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


//  Unit-testing structure containing an FEN, and the (maximum) depth, as well as a list of expected
//  perft results at a given depth

struct PerftTest {
        char const* name;
        char const* FEN;
        Depth       depth;
        Nodes       expected[7];
};



// Unit-test results we obtained from (https://www.chessprogramming.org/Perft_Results)
const PerftTest PerftTests[] =
{
        { .name = "startpos",
          .FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
          .depth = 6,
          .expected = { 20, 400, 8902, 197281, 4865609, 119060324 },
        },

        { .name = "kiwipete",
          .FEN = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -",
          .depth = 5,
          .expected = { 48, 2039, 97862, 4085603, 193690690 },
        },

        { .name = "tricky en-passant",
          .FEN = "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - -",
          .depth = 7,
          .expected = { 14, 191, 2812, 43238, 674624, 11030083, 178633661 },
        },

        { .name = "tricky castling",
          .FEN = "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq -",
          .depth = 6,
          .expected = { 6, 264, 9467, 422333, 15833292, 706045033 },
        },

        { .name = "tricky castling rotated",
          .FEN = "r2q1rk1/pP1p2pp/Q4n2/bbp1p3/Np6/1B3NBn/pPPP1PPP/R3K2R b KQ -",
          .depth = 6,
          .expected = { 6, 264, 9467, 422333, 15833292, 706045033 },
        },

        { .name = "talkchess",
          .FEN = "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ -",
          .depth = 5,
          .expected = { 44, 1486, 62379, 2103487, 89941194 },
        },

        { .name = "normal middlegame",
          .FEN = "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - -",
          .depth = 5,
          .expected = { 46, 2079, 89890, 3894594, 164075551 },
        },
};

constexpr size_t NumberOfPerftTests = sizeof(PerftTests) / sizeof(PerftTests[0]);


double get_time_from_os() {
        timespec timestamp;
        clock_gettime(CLOCK_REALTIME, &timestamp);

        return timestamp.tv_sec + 1.0e-9 * timestamp.tv_nsec;
}


// Recursively compute perft result, requires depth >= 1!
Nodes perft(Board const& pos, Depth depth)
{
        if (depth == 1) return count_moves(pos);
        auto buffer = generate_moves(pos);

        Nodes total = 0;

        for (size_t i = 0; i < buffer.size; i += 1) {
                auto child = make_move(pos, buffer.moves[i]);
                total += perft(child, depth - 1);
        }

        while (buffer.pawn_pushes) {
                auto child = make_pawn_push(pos, trailing_zeros_and_pop(buffer.pawn_pushes));
                total += perft(child, depth - 1);
        }

        return total;
}


// Multi-threaded perft implementation. First a shallow depth 2 perft is done to create a position
// pool, which is then consumed by $(number of cpu cores) threads.


int start_perft_thread(void* opaque_thread_info)
{
        assert(opaque_thread_info != nullptr);
        auto& thread_info = *(PerftThreadInfo*) opaque_thread_info;

        while (true) {
                size_t index = atomic_fetch_add(&thread_info.buffer_done, 1);
                if (index >= thread_info.buffer_size) break;

                auto& position = thread_info.board_buffer[index];

                Nodes nodes = perft(position, thread_info.depth);
                atomic_fetch_add(&thread_info.result, nodes);
        }

        return 0;
}


void populate_position_pool(Board const& board, Depth depth, Board position_pool[], size_t& position_pool_size)
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

        while (buffer.pawn_pushes) {
                auto child = make_pawn_push(board, trailing_zeros_and_pop(buffer.pawn_pushes));
                populate_position_pool(child, depth - 1, position_pool, position_pool_size);
        }
}


Nodes threaded_perft(Board const& board, Depth depth, size_t number_of_threads)
{
        constexpr size_t MAX_THREAD_COUNT = 256;
        constexpr Depth POPULATION_DEPTH = 2;

        assert(depth > POPULATION_DEPTH);
        assert(number_of_threads > 0);
        assert(number_of_threads <= MAX_THREAD_COUNT);

        Board position_pool[1 << 14];
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

        for (size_t i = 0; i < number_of_threads; ++i) {
                thrd_create(&threads[i], start_perft_thread, &info);
        }

        for (size_t i = 0; i < number_of_threads; ++i) {
                thrd_join(threads[i], nullptr);
        }

        return info.result;
}


void bench()
{
        auto cpu_core_count = sysconf(_SC_NPROCESSORS_ONLN);

        Seconds total_time = 0.0;
        Nodes total_nodes = 0;

        printf("name                      depth       nodes    \n");
        printf("===============================================\n");

        for (size_t index = 0; index < NumberOfPerftTests; index += 1)
        {
                auto test = PerftTests[index];

                bool white_to_move, ok;
                auto board = parse_fen(test.FEN, &white_to_move, &ok);
                assert(ok && "FEN parsing failed!");

                auto t1 = get_time_from_os();
                auto nodes = threaded_perft(board, test.depth, cpu_core_count);
                auto t2 = get_time_from_os();

                auto seconds = t2 - t1;
                printf("%-25s %-5u       %9zu\t\t(%6.3f Gnps)\n", test.name, test.depth, nodes, nodes / seconds / 1.0e9);

                total_nodes += nodes;
                total_time += seconds;

                auto expected = test.expected[test.depth - 1];
                assert(nodes == expected && "TEST FAILED!");
        }

        printf("\nAverage nodes per second: %6.3f Gnps\n", total_nodes / total_time / 1.0e9);
}


int main(int argc, char* argv[])
{
        init_bitboard_tables();

        if (argc == 2 && strcmp(argv[1], "--bench") == 0) {
                bench();
                return 0;
        }

        if (argc != 3) {
                fprintf(stderr,
                        "Usage: %s <FEN> <depth>\n\n"
                        " - FEN: position for perft test.\n"
                        " - depth: non-negative depth of perft test.\n",
                        argv[0]);
                return 1;
        }

        bool white_to_move, ok;
        auto board = parse_fen(argv[1], &white_to_move, &ok);

        if (!ok) {
                fprintf(stderr, "error: invalid fen.\n");
                return 1;
        }

        // FIXME: check position is actually legal, not just parses correctly.

        char* end_of_depth_string;
        auto depth = strtol(argv[2], &end_of_depth_string, 10);

        if (depth < 0 || *end_of_depth_string) {
                fprintf(stderr, "error: invalid depth.\n");
                return 1;
        }

        Nodes nodes;
        auto t1 = get_time_from_os();

        if (depth < 3) {
                if (!depth) nodes = 1; // definition of perft 1
                else        nodes = perft(board, depth);
        }

        else {
                auto cpu_core_count = sysconf(_SC_NPROCESSORS_ONLN);
                printf("Running multi-threaded perft on %ld threads.\n\n", cpu_core_count);

                nodes = threaded_perft(board, depth, cpu_core_count);
        }

        auto t2 = get_time_from_os();

        auto seconds = t2 - t1;
        auto nodes_per_second = nodes / seconds;

        printf("Result:            %lu\n", nodes);
        printf("Time taken:        %.3f seconds.\n", t2 - t1);

        if (nodes_per_second < 1.0e9) printf("Nodes per second:  %.0f million.\n", nodes_per_second / 1.0e6);
        else                          printf("Nodes per second:  %.3f billion.\n", nodes_per_second / 1.0e9);
}

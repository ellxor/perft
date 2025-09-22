### Perft

| Note this only runs on x86 CPUs with the `pext` instruction (BMI2)

This is a [perft](https://www.chessprogramming.org/Perft) program using a custom move generator and a unique board state
using only 4 bitboards. It currently achieves an average of 1.40 billion nodes per second across
[a set of 6 positions](https://www.chessprogramming.org/Perft_Results) in single-threaded mode, and reaches up to 10.8
billion nodes per second on Kiwipete on my 6 core (12 thread) 3.9 GHz machine.

**Results:**
```
startpos         ( 5.800 Gnps)
kiwipete         (10.817 Gnps)
position 3       ( 4.860 Gnps)
position 4       (10.014 Gnps)
position 5       ( 9.363 Gnps)
position 6       ( 9.452 Gnps)

Average nodes per second: 8.851 Gnps
```

**Build Instructions**

- Only a C++ compiler is needed to build (`clang` seems to be slightly faster than `gcc`)
- Compile the `src/unity_build.cc` file for a fast [unity build](https://en.wikipedia.org/wiki/Unity_build).
- Add some performance flags, e.g. `-O3 -flto -fno-exceptions -fno-rtti -march=native -Wl,-O1`.

**PGO Build**
For some extra performance, do a PGO (profile-guided-optimisation) build.
- First compile adding `-fprofile-generate`.
- Then run the binary `perft --bench`.
- If using clang, run `llvm-profdata merge *.profraw -o default.profdata`, skip this step for GCC.
- Then compile again adding `-fprofile-use`.

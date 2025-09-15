### Perft

This is a [perft](https://www.chessprogramming.org/Perft) program using a custom move generator and a unique board state using only 4 bitboards. It currently achieves an average of 1.38 Gnps across [a set of 6 positions](https://www.chessprogramming.org/Perft_Results).

**Results:**
```
startpos                        888 Mnps
kiwipete                       1710 Mnps
position 3                      720 Mnps
position 4                     1588 Mnps
position 4 (rotated)           1579 Mnps
position 5                     1514 Mnps
position 6                     1450 Mnps
```

**Build Instructions**

- Only a C++ compiler is needed to build (`clang` seems to be most optimal)
- Compile the `src/unity_build.cc` file for a fast [unity build](https://en.wikipedia.org/wiki/Unity_build).
- Add some performance flags, e.g. `-O3 -flto -fno-exceptions -fno-rtti -march=native -Wl,-O1`.
- For some extra performance, do a PGO (profile-guided-optimisation) build using the `-fprofile-generate`/`-fprofile-use` flags and the command `llvm-profdata merge *.profraw -o default.profdata`.

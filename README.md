
<div align="center">
This repository includes <a href="https://incom-0.github.io/BoxPacker_showcase/">interactive example</a> of BoxPacker_2D algo. <br>
It shows an algorithm/solver for a particular kind of <a href="https://en.wikipedia.org/wiki/Packing_problems">packing problems</a>, speficially <a href="https://en.wikipedia.org/wiki/Polyomino">polyomino</a> packing.<br><br>
Note: The polyomino packing problem is NP-hard and this algorithm does not stricly speaking 'solve' it.<br>
What it does do is it uses a heuristic process to compute 'some pretty good solution' extremely fast and its runtime complexity scales linearly with the problem size.<br>
The solver itself is part of the <a href="github.com/InCom-0/incstd">incstd</a> library.<br><br><br>

This was inspired by <a href="https://adventofcode.com/2025/day/12">Advent of Code 2025 Day 12</a> Advent of Code 2025 Day 12.<br>
Note: It does not directly provide answers to the puzzle itself.<br>
Note: The animation below shows slowed down recording of how the solver progresses.<br>
Note: Typical runtime on modern hardware for a problem like the one below is measured in miliseconds.<br>

<img width="600" alt="Result animation example" src="https://raw.githubusercontent.com/InCom-0/BoxPacker_showcase/bca9126c81f990d537094ca2370b627f5c5762c1/images/SolverAnim.avif" />
</div>




## Building ##

Requires C++26, requires CMake.

As of August 2026 builds with GCC 16.1+, Clang 22+, Emscripten 6.0.1+.<br>
As of August 2026 does NOT build with MSVC (MSVC STL lacks pack indexing feature of C++26)

<i>Note: libc++ does not yet fully implement parallel algorithms and the solver is measurably slower as a result when using libc++ (ie. when compiling with Emscripten, Apple Clang, and in some other setups).</i>


## License
This code is free to use under the terms of the [MIT license](https://github.com/InCom-0/BoxPacker_showcase/blob/main/LICENSE.txt).

## Acknowledgement
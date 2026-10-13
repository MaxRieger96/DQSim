# DQSim: Does the Task Assignment Slow Down Your Distributed Database System?

This repository contains the code to reproduce all results of our paper about DQSim. [[Authors Version](https://db.in.tum.de/~rieger/papers/dqsim.pdf)] [[ACM Version](https://dl.acm.org/doi/pdf/10.1145/3837106)]

## Dependencies

### Linux

Compilation dependencies:

```bash
sudo apt install build-essential cmake ninja-build g++-15 libboost-dev libboost-context-dev libglpk-dev
```

[uv](https://github.com/astral-sh/uv) for Python:

```
sudo apt install curl
curl -LsSf https://astral.sh/uv/install.sh | sh
```

### MacOS

```bash
brew install cmake ninja gcc@15 glpk uv
```

## Compile

To test whether compilation works, run the following from the root of this repository:

```bash
cmake -B build_test \
  -DCMAKE_CXX_COMPILER=g++-15 \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug

cmake --build build_test --parallel
```

To compile for development use:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -G Ninja -DCMAKE_CXX_COMPILER=$(command -v "${CXX:-g++-15}" || command -v g++ || command -v c++)
cmake --build build --parallel
```

Using the `-DCMAKE_CXX_COMPILER` flag enables using gcc instead of clang on macos when the nix flake is active

## Reproduce Paper Figures

The following will only work if compilation succeeds (see above).

First, setup the Python environment:

```bash
uv sync
```

Then run the automated script:

```
uv run python main.py
```

The figures will be placed in `figure_output`.

### Docker

If you want to use docker, build the Ubuntu image from the repository root:

```bash
docker build -t dqsim .
```

Then run the figure-generation script with the repository mounted into the container:

```bash
docker run --rm -v "$(pwd)":/app dqsim
```

## Build Tools

Run `clang-format`:

```bash
find src \( -name "*.cpp" -o -name "*.c" -o -name "*.h" -o -name "*.hpp" \) \
  -not -path "./build/*" | xargs clang-format -i
```

Install `clang-format` pre-commit hook:

```bash
git config core.hooksPath .githooks
```

Run `clang-tidy`:

```bash
./run-clang-tidy.sh
```

## Citation

If you use the contents of this repository, please cite our paper

```
@article{10.1145/3837106,
  author = {Rieger, Maximilian and Neumann, Thomas},
  title = {DQSim: Does the Task Assignment Slow Down Your Distributed Database System?},
  year = {2026},
  issue_date = {September 2026},
  publisher = {Association for Computing Machinery},
  address = {New York, NY, USA},
  volume = {4},
  number = {4},
  url = {https://doi.org/10.1145/3837106},
  doi = {10.1145/3837106},
  journal = {Proc. ACM Manag. Data},
  month = sep,
  articleno = {268},
  numpages = {27},
}
```

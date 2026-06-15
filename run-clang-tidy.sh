#!/bin/bash
# tidy.sh - run from project root
ISYSTEM_FLAGS=$(clang++ -v -x c++ /dev/null 2>&1 \
  | awk '/#include <\.\.\.> search starts here:/{found=1; next} /End of search list/{found=0} found{print "-isystem "$1}' \
  | tr '\n' ' ')

run-clang-tidy -p build -fix \
  -header-filter="${PROJECT_ROOT}/src/.*" \
  $(echo $ISYSTEM_FLAGS | sed 's/-isystem /-extra-arg=-isystem -extra-arg=/g')
# run-clang-tidy -p build \
#   -header-filter="^${PROJECT_ROOT}/src/.*" \
#   $(echo $ISYSTEM_FLAGS | sed 's/-isystem /-extra-arg=-isystem -extra-arg=/g')

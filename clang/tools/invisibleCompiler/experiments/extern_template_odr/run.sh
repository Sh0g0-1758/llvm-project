#!/bin/sh
# builds a.cc, b.cc and main.cc together at -O2 for each combination and runs the program
CXX=${CXX:-/usr/lib/llvm-22/bin/clang++}
cd "$(dirname "$0")"
for inline in "" inline; do
  for factor in 2 3; do
    $CXX -std=c++23 -O2 -DTWICE_INLINE=$inline -c a.cc -o a.o &&
    $CXX -std=c++23 -O2 -DTWICE_INLINE=$inline -DB_FACTOR=$factor -c b.cc -o b.o &&
    $CXX -std=c++23 -O2 -c main.cc -o main.o &&
    $CXX a.o b.o main.o -o prog && echo "template ${inline:-not inline}, b.cc returns x * $factor: links, $(./prog)"
  done
done
rm -f a.o b.o main.o prog

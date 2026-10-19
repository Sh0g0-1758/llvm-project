// the other TU: twice<int> specialized with its own body, which a.cc never sees declared
#include "twice.h"
template <> int twice<int>(int x) { return x * B_FACTOR; }

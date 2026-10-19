// the TU that only sees the template, and says twice<int> is instantiated elsewhere
#include "twice.h"
extern template int twice<int>(int);
int use(int a) { return twice(a); }

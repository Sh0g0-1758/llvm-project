/* C: hoist-calls puts temporaries after labels and in blocks of their own in a function with
   labels, as C uses goto for cleanup; short-circuit-to-ir's result is an int 1 or 0; constant
   folds keep their type, `(char)66`. */
/* CHECK: int __short_circuit_result_0 = 0 */
#include <stdio.h>
static int counter;
static int g(void) { return ++counter; }
static int f(int v) { return v; }
static int cleanup(int fail) {
  int r = 0;
  if (fail) goto out;
  r = f(g());
out:
  r += f(g());
  return r;
}
int main(void) {
  int x = 0, n = 0;
  char c = (char)('A' + 1);
L:
  x = f(g());
  n++;
  if (n < 2) goto L;
  int both = n > 1 && f(x) > 1;
  printf("%d %d %c %d %d\n", x, n, c, both, cleanup(1) + cleanup(0));
  return 0;
}

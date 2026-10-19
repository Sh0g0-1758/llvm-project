// A template argument of enum type whose value is no enumerator, here a combination of flags,
// is printed as a cast to the enum: `VM<int, 5>` doesn't convert 5 to Mode.
// The copy of run() is printed rather than left as written:
// CHECK: run__mono[0-9a-f]+\(VM<int, \(Mode\)5> \*__this\)\{
enum Mode { Search = 1, Backward = 4 };
template <class T, Mode m> struct VM {
  int run() { return m; }
};
int main() {
  VM<int, Mode(Search | Backward)> vm;
  return vm.run() - 5;
}

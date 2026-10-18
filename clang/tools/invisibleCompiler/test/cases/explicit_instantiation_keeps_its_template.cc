// An explicit instantiation definition, `template void Data::reserve<true>(int);` as in
// kakoune's String, needs the definition of its template, which monomorphization keeps, also
// when a function body instantiates it first.
struct Data {
  int cap = 0;
  template <bool copy = true> void reserve(int n);
  void grow() { reserve(cap + 1); }
};
template <bool copy> void Data::reserve(int n) { cap = copy ? n : -n; }
template void Data::reserve<true>(int);
template void Data::reserve<false>(int);
template <class T> T twice(T x) { return x + x; }
int four() { return twice(2); }
template int twice<int>(int);
int main() {
  Data d;
  d.grow();
  return d.cap - 1 + four() - 4;
}

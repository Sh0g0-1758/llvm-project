// What a constant expression calls stays where it is written, also in a body: kakoune's flag
// enums take `operator|` from a template constrained by a concept calling a constexpr overload
// that only argument dependent lookup finds, as it is declared after the concept, and use it in
// template arguments and static_asserts, whose calls hoist-calls leaves in them.
// CHECK: VM<int, \(kak::Mode\)3> |VM<int, Mode::Forward \| Mode::Search>
namespace kak {
template <class T> struct Type {};
constexpr bool with_bit_ops(...) { return false; }
template <class F> concept WithBitOps = with_bit_ops(Type<F>{});
template <WithBitOps F> constexpr F operator|(F a, F b) { return F(int(a) | int(b)); }
enum class Mode { Forward = 1, Search = 2 };
constexpr bool with_bit_ops(Type<Mode>) { return true; }
constexpr bool has_direction(Mode m) { return int(m) & 1; }
template <class It, Mode m> struct VM {
  int run() const {
    static_assert(has_direction(m));
    return int(m);
  }
};
int search() {
  VM<int, Mode::Forward | Mode::Search> vm;
  return vm.run();
}
}  // namespace kak
int main() { return kak::search() - 3; }

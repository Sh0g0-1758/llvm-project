// A specialization with a deduced return type can't be called before its definition, which
// decl-before-def writes at the end: what stays where it is written, here a constexpr function
// and a member's default initializer, keeps the templates it calls as written, as kakoune's
// `__begin<DisplayLine>`.
template <class T> auto first(T *items) { return items[0]; }
template <class T> auto last(T *items) { return items[1]; }
struct Line { int width; };
constexpr int width_of(Line *lines) { return last(lines).width; }
struct Buffer {
  Line lines[2] = {{3}, {5}};
  int width = first(lines).width;
};
int main() {
  Buffer b;
  return b.width + width_of(b.lines) - 8;
}

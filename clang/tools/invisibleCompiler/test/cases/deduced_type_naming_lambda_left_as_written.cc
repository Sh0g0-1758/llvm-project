// A function whose return type is deduced to name what can't be named elsewhere, here the
// closure type of a lambda in a template, as kakoune's views, stays where it is written: its
// declaration at the end of the TU would have to spell the type.
template <class F> struct View { F f; int get() const { return f(); } };
template <class... T> auto view(T... t) { return View{[t...] { return (t + ... + 1); }}; }
auto three() { return view(1, 1); }
int main() { return three().get() - 3; }

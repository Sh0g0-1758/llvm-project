// hoist-calls declares the temporary of a value whose type can't be named with `auto`: here
// a template specialized for the closure type of a lambda materialize-lambda leaves, as it
// captures a pack, like kakoune's `BufferManager::instance() | transform(...)`.
// CHECK: auto __func_hoist_ret__[0-9]+ = 
template <class F> struct View { F f; int get() const { return f(); } };
template <class... T> auto view(T... t) { return View{[t...] { return (t + ... + 1); }}; }
int unit() { return 1; }
struct Desc { int (*value)(); };
static const Desc descs[] = {{[]() { return view(unit()).get(); }}};
int main() { return descs[0].value() - 2; }

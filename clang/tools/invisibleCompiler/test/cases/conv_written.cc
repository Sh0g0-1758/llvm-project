struct P { explicit operator bool() const { return true; } };
int main() { P p; bool b = p.operator bool(); return b; }

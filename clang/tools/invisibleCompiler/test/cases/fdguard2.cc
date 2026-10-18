int f() { return 1; }
int t() try { return f(); } catch (...) { return f(); }
int main() { return t(); }

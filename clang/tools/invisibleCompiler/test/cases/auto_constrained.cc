template <typename T> concept Small = sizeof(T) <= 8;
template <typename T> Small auto twice(T x) { return x * 2; }
int main() { return twice(3); }

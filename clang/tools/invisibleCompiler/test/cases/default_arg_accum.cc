int f(int a, int b, int c = 2);
int f(int a, int b  = 3, int c);
int f(int a = 1, int b, int c);

int main() {
    return f();
}

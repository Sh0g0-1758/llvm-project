// materialize-lambda turns a lambda in a function template into a struct template of the same
// parameters, here constrained by a concept, with two lambdas in one function, as libstdc++'s
// std::__cmp_cust::__fp_weak_ordering has.
// CHECK: struct __invisible_lambda_0
// CHECK: struct __invisible_lambda_1
namespace std {
template <class T> concept floating_point = __is_same(T, float) || __is_same(T, double);
namespace cust {
template <floating_point Tp> constexpr int weak(Tp e, Tp f) {
  auto cat = [](Tp fp) -> int {
    const int sign = __builtin_signbit(fp) ? -1 : 1;
    if (int inf = __builtin_isinf_sign(fp)) return 4 * inf;
    return 2 * sign;
  };
  if (e < f) return -1;
  else {
    auto nan_sign = [](Tp fp) -> int { return __builtin_isnan(fp) ? 1 : 0; };
    return cat(e) + nan_sign(f);
  }
}
}  // namespace cust
}  // namespace std
int main() { return std::cust::weak(1.0, 0.5) - 2; }

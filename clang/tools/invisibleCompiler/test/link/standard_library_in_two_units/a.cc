// Two translation units using the same instantiations of the standard library, whose
// specializations and monomorphized copies both define: they link, and the program runs the same.
#include <string>
#include <vector>
#include <cstdio>
std::string joined(const std::vector<std::string> &parts);
int main() {
  std::vector<std::string> parts{"in", "visible"};
  parts.push_back("!");
  std::string all = joined(parts);
  std::printf("%s %zu\n", all.c_str(), parts.size());
  return all.size() == 10 ? 0 : 1;
}

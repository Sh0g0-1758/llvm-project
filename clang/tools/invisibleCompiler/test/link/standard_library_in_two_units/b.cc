#include <string>
#include <vector>
std::string joined(const std::vector<std::string> &parts) {
  std::string all;
  for (const std::string &part : parts) all += part;
  std::vector<int> sizes;
  for (const std::string &part : parts) sizes.push_back(part.size());
  return all;
}

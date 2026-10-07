#include <array>
#include <algorithm>
#include <functional>
#include <random>
#include <string>

#include <uuid.h>

#include "util/uuid_generator.h"

namespace jiaolong {

std::string GenerateUuid() {
  std::random_device rd;
  auto seed_data = std::array<int, std::mt19937::state_size> {};
  std::generate(std::begin(seed_data), std::end(seed_data), std::ref(rd));
  std::seed_seq seq(std::begin(seed_data), std::end(seed_data));
  std::mt19937 generator(seq);
  uuids::uuid_random_generator gen{generator};

  const uuids::uuid id = gen();
  return uuids::to_string(id);
}

}  // namespace jiaolong

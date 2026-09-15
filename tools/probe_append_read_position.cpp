// Read-only probe of an existing artifact: checks Darwin a+b initial position.
#include <cstdio>
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  FILE* f = std::fopen(argv[1], "a+b");
  if (!f) return 3;
  const auto initial = std::ftell(f);
  unsigned char header[12]{};
  const auto first = std::fread(header, 1, sizeof header, f);
  std::rewind(f);
  const auto rewound = std::fread(header, 1, sizeof header, f);
  std::printf("initial_position=%ld initial_read=%zu rewound_read=%zu\n", initial, first, rewound);
  std::fclose(f);
}

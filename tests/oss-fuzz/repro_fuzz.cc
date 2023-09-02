// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size);

namespace
{
std::vector<uint8_t> ReadFile(const char * path)
{
    std::vector<uint8_t> buffer;
    std::ifstream file(path, std::ios::binary);
    buffer.resize(file.seekg(0, std::ios::end).tellg());
    file.seekg(0, std::ios::beg).read(reinterpret_cast<char *>(buffer.data()), buffer.size());
    return buffer;
}
} // namespace

int main(int argc, char * argv[])
{
    if (argc < 2) {
        std::cerr << "Missing reproducer file" << std::endl;
        return 1;
    }

    for (int i = 1; i < argc; ++i) {
        const std::vector<uint8_t> buffer = ReadFile(argv[i]);
        LLVMFuzzerTestOneInput(buffer.data(), buffer.size());
    }
    return 0;
}

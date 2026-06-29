#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "archive.h"

namespace {

void printUsage(std::ostream& out) {
    out << "usage:\n"
        << "  vector pack [--compress] <files...> <out.vec>\n"
        << "  vector unpack <archive.vec> [output-dir]\n";
}

int run(int argc, char** argv) {
    if (argc < 2) {
        printUsage(std::cerr);
        return 2;
    }

    const std::string command = argv[1];
    std::string error;
    if (command == "pack") {
        bool compress = false;
        int first_file = 2;
        if (argc > 2 && std::string(argv[2]) == "--compress") {
            compress = true;
            first_file = 3;
        }
        if (argc - first_file < 2) {
            printUsage(std::cerr);
            return 2;
        }

        std::vector<std::filesystem::path> inputs;
        for (int i = first_file; i < argc - 1; ++i) {
            inputs.emplace_back(argv[i]);
        }
        if (!vector::pack(inputs, argv[argc - 1], compress, &error)) {
            std::cerr << "vector: " << error << "\n";
            return 1;
        }
        return 0;
    }

    if (command == "unpack") {
        if (argc < 3 || argc > 4) {
            printUsage(std::cerr);
            return 2;
        }
        const std::filesystem::path output_dir = argc == 4 ? argv[3] : ".";
        if (!vector::unpack(argv[2], output_dir, &error)) {
            std::cerr << "vector: " << error << "\n";
            return 1;
        }
        return 0;
    }

    printUsage(std::cerr);
    return 2;
}

}  // namespace

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
int main(int argc, char** argv) {
    return run(argc, argv);
}

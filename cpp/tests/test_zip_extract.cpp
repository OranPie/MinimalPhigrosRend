#include "phigros/chart/chart_loader.hpp"
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cout << "Usage: test_zip_extract <zip_path> <file_in_zip>\n";
        return 1;
    }

    std::string zip_path = argv[1];
    std::string file_in_zip = argv[2];

    std::cout << "Extracting: " << file_in_zip << "\n";
    std::cout << "From: " << zip_path << "\n\n";

    auto data = phigros::chart::extract_zip_file(zip_path, file_in_zip);

    if (data.empty()) {
        std::cerr << "Failed to extract file\n";
        return 1;
    }

    std::cout << "Extracted " << data.size() << " bytes\n";

    // Show first 200 bytes
    std::cout << "\nFirst 200 bytes:\n";
    size_t preview_size = std::min(size_t(200), data.size());
    std::cout.write(reinterpret_cast<const char*>(data.data()), preview_size);
    std::cout << "\n";

    return 0;
}

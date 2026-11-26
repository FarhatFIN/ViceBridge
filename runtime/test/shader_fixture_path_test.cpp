#include "shader_fixture_path.h"
#include <cstdio>
#include <chrono>
#include <fstream>

int main() {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / ("leonida_fixture_path_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root / "generated");
    const auto previous = fs::current_path();
    int result = 0;
    try {
        fs::current_path(root);
        std::ofstream("generated/test.spv") << "packaged";
        const auto original = root / "build" / "test.spv";
        if (vktest::shader_fixture_path(original) != fs::path("generated/test.spv")) result = 1;
        fs::create_directory("build");
        const auto original_directory = root / "build" / "shader_library";
        fs::create_directory(original_directory);
        if (vktest::shader_fixture_directory(original_directory) != original_directory) result = 1;
        fs::create_directory("generated/shader_library");
        if (vktest::shader_fixture_directory(original_directory) != fs::path("generated/shader_library")) result = 1;
        fs::remove("generated/shader_library");
        std::ofstream("generated/shader_library") << "not a directory";
        if (vktest::shader_fixture_directory(original_directory) != original_directory) result = 1;
        const auto missing_directory = root / "build" / "missing_library";
        if (vktest::shader_fixture_directory(missing_directory) != missing_directory) result = 1;
        std::ofstream(original) << "local";
        if (vktest::shader_fixture_path(original) != original) result = 1;
        const auto missing = root / "build" / "missing.spv";
        if (vktest::shader_fixture_path(missing) != missing) result = 1;
        fs::remove(original);
        fs::remove("generated/test.spv");
        fs::create_directory("generated/test.spv");
        if (vktest::shader_fixture_path(original) != original) result = 1;
    } catch (...) { result = 1; }
    fs::current_path(previous);
    fs::remove_all(root);
    std::puts(result ? "shader fixture lookup failed" : "shader fixture lookup passed");
    return result;
}

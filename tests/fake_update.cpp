#include <fstream>
#include <filesystem>
#include <string>
int wmain(int argc, wchar_t **argv) {
    if (std::ifstream("fail-start.marker").good()) return 1;
    std::ofstream("update-installed.marker") << "started";
    if (argc == 3 && std::wstring(argv[1]) == L"--update-ready")
        std::ofstream(std::filesystem::path(std::wstring(argv[2]) + L".ready")) << "ready";
}

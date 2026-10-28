#include "AppConfig.h"
#include "Renderer.h"

#include <exception>
#include <iostream>

int main(int argc, char** argv) {
    try {
        const RunOptions options = ParseOptions(argc, argv);
        if (options.help) {
            PrintHelp();
            return 0;
        }

        Renderer renderer(options);
        return renderer.Run();
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
    } catch (...) {
        std::cerr << "Error: unexpected failure\n";
    }
    return 1;
}

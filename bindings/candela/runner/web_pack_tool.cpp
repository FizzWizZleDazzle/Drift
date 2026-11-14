// drift_web_pack <game folder> <out folder> <web runtime folder> [--dev]
//
// The page writer of `drift build --web`, on its own and without the
// compiler, built for Node.js: small and quick to start, for writing a page
// again on every save. Its pages compile the game when they load;
// `node drift_node.js build --web` writes pages with the game compiled in.

#include "web_pack.hpp"

#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    bool dev = argc == 5 && std::string(argv[4]) == "--dev";
    if (argc != 4 && !dev) {
        std::fprintf(stderr, "usage: drift_web_pack <game folder> <out folder> <web runtime folder> [--dev]\n");
        return 2;
    }
    std::string error, summary;
    if (!drift::web::buildWebPage(argv[1], argv[2], argv[3], dev, error, summary)) {
        std::fprintf(stderr, "drift_web_pack: %s\n", error.c_str());
        return 1;
    }
    std::printf("%s\n", summary.c_str());
    return 0;
}

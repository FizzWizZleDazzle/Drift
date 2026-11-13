// drift_web_pack <game folder> <out folder> <web runtime folder>
//
// The page writer of `drift build --web`, on its own, for the web build of
// this tree: there is no desktop `drift` there, so the pages of the Candela
// examples are written by this program, built for Node.js.

#include "web_pack.hpp"

#include <cstdio>

int main(int argc, char** argv) {
    if (argc != 4) {
        std::fprintf(stderr, "usage: drift_web_pack <game folder> <out folder> <web runtime folder>\n");
        return 2;
    }
    std::string error, summary;
    if (!drift::web::buildWebPage(argv[1], argv[2], argv[3], error, summary)) {
        std::fprintf(stderr, "drift_web_pack: %s\n", error.c_str());
        return 1;
    }
    std::printf("%s\n", summary.c_str());
    return 0;
}

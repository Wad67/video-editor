#include "app/Application.h"
#include <cstdio>
#include <cstring>
#include <string>

int main(int argc, char* argv[]) {
    std::string filePath;
    bool verbose = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            verbose = true;
        } else if (argv[i][0] != '-') {
            filePath = argv[i];
        }
    }

    Application app;
    app.setVerbose(verbose);
    if (!app.init(filePath)) {
        fprintf(stderr, "Failed to initialize application\n");
        return 1;
    }

    app.run();
    return 0;
}

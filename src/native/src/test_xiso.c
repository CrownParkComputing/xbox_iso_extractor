#include "xiso.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    if (argc != 3) {
        printf("Usage: %s <input.iso> <output_directory>\n", argv[0]);
        return 1;
    }

    printf("Initializing XISO library...\n");
    if (!xiso_init()) {
        printf("Failed to initialize: %s\n", xiso_get_last_error());
        return 1;
    }

    printf("Opening and verifying ISO file: %s\n", argv[1]);
    if (!xiso_extract(argv[1], argv[2])) {
        printf("Failed to process ISO: %s\n", xiso_get_last_error());
        xiso_cleanup();
        return 1;
    }

    printf("Test completed successfully!\n");
    xiso_cleanup();
    return 0;
}

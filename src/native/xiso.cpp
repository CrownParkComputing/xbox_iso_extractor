#include <cstdlib>
#include <string>
#include "xiso.h"

extern "C" {

// Enable debug output by default in C++ wrapper
static bool debug_initialized = false;

void init_debug() {
    if (!debug_initialized) {
        xiso_set_debug(true);
        debug_initialized = true;
    }
}

bool extract_iso(const char* iso_path, const char* output_path) {
    init_debug();
    
    if (!xiso_init()) {
        return false;
    }
    
    bool result = xiso_extract(iso_path, output_path);
    
    xiso_cleanup();
    return result;
}

const char* get_last_error() {
    return xiso_get_last_error();
}

} // extern "C"

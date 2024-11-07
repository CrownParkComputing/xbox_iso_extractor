#ifndef XISO_H
#define XISO_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// Public API functions
bool xiso_init(void);
void xiso_cleanup(void);
bool xiso_extract(const char* iso_path, const char* output_path);
bool xiso_list(const char* iso_path, char* output_buffer, size_t buffer_size);
const char* xiso_get_last_error(void);

// Optional configuration functions
void xiso_set_debug(bool enable);
void xiso_set_buffer_size(size_t size);

#endif // XISO_H

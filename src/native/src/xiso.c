#include "xiso.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdarg.h>
#include <ctype.h>

#if defined(_WIN32)
#include <io.h>
#define mkdir(path, mode) _mkdir(path)
#define O_BINARY _O_BINARY
#else
#include <unistd.h>
#include <sys/types.h>
#define O_BINARY 0
#endif

// Constants
#define XISO_HEADER_OFFSET           0x10000
#define XISO_SECTOR_SIZE            2048
#define XISO_HEADER_DATA           "MICROSOFT*XBOX*MEDIA"
#define XISO_HEADER_DATA_LENGTH     20
#define XISO_FILETIME_SIZE          8
#define XISO_UNUSED_SIZE            0x7c8
#define XISO_ROOT_DIRECTORY_SECTOR  0x108

// File entry constants
#define XISO_FILENAME_MAX_LENGTH     256
#define XISO_ATTRIBUTE_DIR          0x10
#define XISO_TABLE_OFFSET_SIZE       2
#define XISO_FILENAME_LENGTH_SIZE    1
#define XISO_SECTOR_OFFSET_SIZE      4
#define XISO_FILESIZE_SIZE           4
#define XISO_ATTRIBUTES_SIZE         1
#define XISO_PAD_SHORT             0xFFFF

// Additional offset checks for different formats
#define GLOBAL_LSEEK_OFFSET        0xFD90000ull
#define XGD3_LSEEK_OFFSET         0x2080000ull

// Debug macros
#define DEBUG_PRINT(...) printf(__VA_ARGS__)
#define DUMP_HEX(ptr, len) { \
    for(size_t i = 0; i < len; i++) { \
        if(i % 16 == 0) printf("\n%04zx: ", i); \
        printf("%02x ", ((unsigned char*)(ptr))[i]); \
    } \
    printf("\n"); \
}

typedef struct {
    uint16_t left_offset;    // 2 bytes
    uint16_t right_offset;   // 2 bytes
    uint32_t start_sector;   // 4 bytes
    uint32_t file_size;      // 4 bytes
    uint8_t attributes;      // 1 byte
    uint8_t filename_length; // 1 byte
    char filename[XISO_FILENAME_MAX_LENGTH];
} __attribute__((packed)) XisoEntry;

// Global variables
static char last_error[1024] = "";
static int iso_fd = -1;
static void* buffer = NULL;
static size_t buffer_size = 2 * 1024 * 1024; // 2MB buffer
static uint64_t xbox_disc_lseek = 0;
static char* list_buffer = NULL;
static size_t list_buffer_size = 0;
static size_t list_buffer_pos = 0;

// Function declarations
static void set_error(const char* format, ...);
static bool verify_header_at_offset(uint64_t offset, uint32_t* out_root_dir_sector, uint32_t* out_root_dir_size);
static bool verify_xiso(const char* filename, uint32_t* out_root_dir_sector, uint32_t* out_root_dir_size);
static bool read_entry(XisoEntry* entry, uint64_t dir_start);
static bool extract_file(const char* output_path, const XisoEntry* entry);
static bool extract_directory(const char* output_path, uint64_t dir_start);
static bool list_directory(const char* current_path, uint64_t dir_start);
static void append_to_list(const char* format, ...);

// Helper function implementations
static void set_error(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(last_error, sizeof(last_error) - 1, format, args);
    va_end(args);
    DEBUG_PRINT("Error: %s\n", last_error);
}

static void append_to_list(const char* format, ...) {
    if (!list_buffer || !list_buffer_size) return;

    va_list args;
    va_start(args, format);
    int written = vsnprintf(list_buffer + list_buffer_pos, 
                          list_buffer_size - list_buffer_pos, 
                          format, args);
    va_end(args);

    if (written > 0 && written < list_buffer_size - list_buffer_pos) {
        list_buffer_pos += written;
    }
}

static bool verify_header_at_offset(uint64_t offset, uint32_t* out_root_dir_sector, uint32_t* out_root_dir_size) {
    char header[XISO_HEADER_DATA_LENGTH];
    uint32_t root_dir_sector, root_dir_size;
    off_t seek_result;
    
    DEBUG_PRINT("Checking for header at offset 0x%llx\n", (unsigned long long)(XISO_HEADER_OFFSET + offset));
    
    seek_result = lseek(iso_fd, XISO_HEADER_OFFSET + offset, SEEK_SET);
    if (seek_result == -1) {
        DEBUG_PRINT("Failed to seek to header offset: %s\n", strerror(errno));
        return false;
    }
    
    ssize_t bytes_read = read(iso_fd, header, XISO_HEADER_DATA_LENGTH);
    if (bytes_read != XISO_HEADER_DATA_LENGTH) {
        DEBUG_PRINT("Failed to read header data (read %zd bytes): %s\n", bytes_read, strerror(errno));
        return false;
    }
    
    DEBUG_PRINT("Read header data:\n");
    DUMP_HEX(header, XISO_HEADER_DATA_LENGTH);
    
    if (memcmp(header, XISO_HEADER_DATA, XISO_HEADER_DATA_LENGTH) == 0) {
        bytes_read = read(iso_fd, &root_dir_sector, 4);
        if (bytes_read != 4) {
            DEBUG_PRINT("Failed to read root directory sector: %s\n", strerror(errno));
            return false;
        }
        
        bytes_read = read(iso_fd, &root_dir_size, 4);
        if (bytes_read != 4) {
            DEBUG_PRINT("Failed to read root directory size: %s\n", strerror(errno));
            return false;
        }
        
        // No endian conversion needed on little endian systems
        *out_root_dir_sector = root_dir_sector;
        *out_root_dir_size = root_dir_size;

        DEBUG_PRINT("Found valid header. Root dir sector: %u, size: %u\n", 
                   *out_root_dir_sector, *out_root_dir_size);

        // Skip filetime and unused data
        seek_result = lseek(iso_fd, XISO_FILETIME_SIZE + XISO_UNUSED_SIZE, SEEK_CUR);
        if (seek_result == -1) {
            DEBUG_PRINT("Failed to seek past filetime and unused data: %s\n", strerror(errno));
            return false;
        }
        
        // Verify trailing header
        bytes_read = read(iso_fd, header, XISO_HEADER_DATA_LENGTH);
        if (bytes_read != XISO_HEADER_DATA_LENGTH) {
            DEBUG_PRINT("Failed to read trailing header: %s\n", strerror(errno));
            return false;
        }
        
        if (memcmp(header, XISO_HEADER_DATA, XISO_HEADER_DATA_LENGTH) == 0) {
            xbox_disc_lseek = offset;
            DEBUG_PRINT("Found valid trailing header. Xbox disc offset: 0x%llx\n", 
                       (unsigned long long)xbox_disc_lseek);
            return true;
        }
        
        DEBUG_PRINT("Invalid trailing header\n");
        DUMP_HEX(header, XISO_HEADER_DATA_LENGTH);
    }
    
    return false;
}

static bool verify_xiso(const char* filename, uint32_t* out_root_dir_sector, uint32_t* out_root_dir_size) {
    DEBUG_PRINT("Verifying XISO file: %s\n", filename);
    
    // Try standard offset
    if (verify_header_at_offset(0, out_root_dir_sector, out_root_dir_size)) {
        DEBUG_PRINT("Found valid XBOX ISO header at standard offset\n");
        return true;
    }
    
    // Try GLOBAL_LSEEK_OFFSET
    if (verify_header_at_offset(GLOBAL_LSEEK_OFFSET, out_root_dir_sector, out_root_dir_size)) {
        DEBUG_PRINT("Found valid XBOX ISO header at global offset\n");
        return true;
    }
    
    // Try XGD3_LSEEK_OFFSET
    if (verify_header_at_offset(XGD3_LSEEK_OFFSET, out_root_dir_sector, out_root_dir_size)) {
        DEBUG_PRINT("Found valid XBOX ISO header at XGD3 offset\n");
        return true;
    }
    
    set_error("No valid XBOX ISO header found");
    return false;
}

static bool read_entry(XisoEntry* entry, uint64_t dir_start) {
    unsigned char raw_data[32];
    size_t raw_size = 0;
    
    memset(entry, 0, sizeof(XisoEntry));

    // Read raw data for debugging
    if (read(iso_fd, raw_data, sizeof(raw_data)) == sizeof(raw_data)) {
        raw_size = sizeof(raw_data);
        DEBUG_PRINT("\nRaw directory entry data:\n");
        DUMP_HEX(raw_data, sizeof(raw_data));
    }
    
    // Return to start
    if (lseek(iso_fd, -raw_size, SEEK_CUR) == -1) {
        set_error("Failed to seek back to entry start");
        return false;
    }

    // Check for sector padding
    uint16_t pad;
    if (read(iso_fd, &pad, 2) == 2 && pad == XISO_PAD_SHORT) {
        // Skip to next sector
        off_t pos = lseek(iso_fd, 0, SEEK_CUR);
        off_t sector_offset = pos % XISO_SECTOR_SIZE;
        if (sector_offset) {
            if (lseek(iso_fd, XISO_SECTOR_SIZE - sector_offset, SEEK_CUR) == -1) {
                set_error("Failed to seek to next sector");
                return false;
            }
        }
        return read_entry(entry, dir_start);
    }

    // Return to start
    if (lseek(iso_fd, -2, SEEK_CUR) == -1) {
        set_error("Failed to seek back after pad check");
        return false;
    }

    // Read directory entry structure
    uint16_t l_offset, r_offset;
    uint32_t sector, size;
    uint8_t attributes, filename_length;

    // Read offsets
    if (read(iso_fd, &l_offset, 2) != 2 || read(iso_fd, &r_offset, 2) != 2) {
        set_error("Failed to read offsets");
        return false;
    }

    // Read sector and size
    if (read(iso_fd, &sector, 4) != 4 || read(iso_fd, &size, 4) != 4) {
        set_error("Failed to read sector/size");
        return false;
    }

    // Read attributes and filename length
    if (read(iso_fd, &attributes, 1) != 1 || read(iso_fd, &filename_length, 1) != 1) {
        set_error("Failed to read attributes/filename length");
        return false;
    }

    // Store values directly without endian conversion
    entry->left_offset = l_offset;
    entry->right_offset = r_offset;
    entry->start_sector = sector;
    entry->file_size = size;
    entry->attributes = attributes;
    entry->filename_length = filename_length;

    // Read filename
    if (entry->filename_length > 0) {
        if (read(iso_fd, entry->filename, entry->filename_length) != entry->filename_length) {
            set_error("Failed to read filename");
            return false;
        }
        entry->filename[entry->filename_length] = '\0';
    }

    DEBUG_PRINT("Entry: name='%s', sector=%u, size=%u, attr=0x%02x\n",
                entry->filename, entry->start_sector, entry->file_size, entry->attributes);

    return true;
}

static bool extract_file(const char* output_path, const XisoEntry* entry) {
    char full_path[XISO_FILENAME_MAX_LENGTH * 2];
    int out_fd;
    ssize_t bytes_read, bytes_written;
    uint32_t bytes_remaining;

    snprintf(full_path, sizeof(full_path), "%s/%s", output_path, entry->filename);

    // Handle directories
    if (entry->attributes & XISO_ATTRIBUTE_DIR) {
        DEBUG_PRINT("Creating directory: %s\n", full_path);
        if (mkdir(full_path, 0755) != 0 && errno != EEXIST) {
            set_error("Failed to create directory: %s (%s)", full_path, strerror(errno));
            return false;
        }
        return true;
    }

    DEBUG_PRINT("Extracting file: %s (%u bytes)\n", full_path, entry->file_size);

    out_fd = open(full_path, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0644);
    if (out_fd == -1) {
        set_error("Failed to create file: %s (%s)", full_path, strerror(errno));
        return false;
    }

    if (lseek(iso_fd, entry->start_sector * XISO_SECTOR_SIZE + xbox_disc_lseek, SEEK_SET) == -1) {
        set_error("Failed to seek to file data");
        close(out_fd);
        return false;
    }

    bytes_remaining = entry->file_size;
    while (bytes_remaining > 0) {
        size_t to_read = bytes_remaining < buffer_size ? bytes_remaining : buffer_size;
        
        bytes_read = read(iso_fd, buffer, to_read);
        if (bytes_read <= 0) {
            set_error("Failed to read file data");
            close(out_fd);
            return false;
        }

        bytes_written = write(out_fd, buffer, bytes_read);
        if (bytes_written != bytes_read) {
            set_error("Failed to write file data");
            close(out_fd);
            return false;
        }

        bytes_remaining -= bytes_read;
    }

    close(out_fd);
    return true;
}

static bool process_directory_entry(const char* path, uint64_t dir_start, uint64_t entry_offset, bool is_listing) {
    XisoEntry entry;
    char new_path[XISO_FILENAME_MAX_LENGTH * 2];
    bool result = true;

    // Seek to entry
    if (lseek(iso_fd, entry_offset, SEEK_SET) == -1) {
        set_error("Failed to seek to entry");
        return false;
    }

    // Read entry
    if (!read_entry(&entry, dir_start)) {
        return false;
    }

    // Process entry
    if (is_listing) {
        if (entry.attributes & XISO_ATTRIBUTE_DIR) {
            append_to_list("%s%s/\n", path, entry.filename);
        } else {
            append_to_list("%s%s (%u bytes)\n", path, entry.filename, entry.file_size);
        }
    } else {
        if (!extract_file(path, &entry)) {
            return false;
        }
    }

    // Process subdirectory
    if ((entry.attributes & XISO_ATTRIBUTE_DIR) && entry.start_sector) {
        if (is_listing) {
            snprintf(new_path, sizeof(new_path), "%s%s/", path, entry.filename);
        } else {
            snprintf(new_path, sizeof(new_path), "%s/%s", path, entry.filename);
        }

        uint64_t subdir_start = entry.start_sector * XISO_SECTOR_SIZE + xbox_disc_lseek;
        
        if (is_listing) {
            result = list_directory(new_path, subdir_start);
        } else {
            result = extract_directory(new_path, subdir_start);
        }

        if (!result) return false;

        // Return to parent directory entry
        if (lseek(iso_fd, entry_offset, SEEK_SET) == -1) {
            set_error("Failed to return to parent directory");
            return false;
        }
    }

    // Process left subtree
    if (entry.left_offset) {
        result = process_directory_entry(path, dir_start, 
            dir_start + entry.left_offset * 4, is_listing);
        if (!result) return false;
    }

    // Process right subtree
    if (entry.right_offset) {
        result = process_directory_entry(path, dir_start,
            dir_start + entry.right_offset * 4, is_listing);
        if (!result) return false;
    }

    return true;
}

static bool list_directory(const char* current_path, uint64_t dir_start) {
    return process_directory_entry(current_path, dir_start, dir_start, true);
}

static bool extract_directory(const char* output_path, uint64_t dir_start) {
    DEBUG_PRINT("Processing directory at offset 0x%llx\n", (unsigned long long)dir_start);
    return process_directory_entry(output_path, dir_start, dir_start, false);
}

bool xiso_init(void) {
    DEBUG_PRINT("Initializing XISO library...\n");
    
    if (buffer) {
        DEBUG_PRINT("Buffer already allocated\n");
        return true;
    }
    
    buffer = malloc(buffer_size);
    if (!buffer) {
        set_error("Failed to allocate buffer");
        return false;
    }
    
    DEBUG_PRINT("Allocated %zu byte buffer\n", buffer_size);
    return true;
}

void xiso_cleanup(void) {
    if (iso_fd != -1) {
        close(iso_fd);
        iso_fd = -1;
    }
    free(buffer);
    buffer = NULL;
    DEBUG_PRINT("Cleaned up XISO library\n");
}

void xiso_set_debug(bool enable) {
    // Debug is now always enabled
}

void xiso_set_buffer_size(size_t size) {
    if (size > 0) {
        buffer_size = size;
        if (buffer) {
            free(buffer);
            buffer = malloc(buffer_size);
            DEBUG_PRINT("Reallocated buffer to %zu bytes\n", buffer_size);
        }
    }
}

const char* xiso_get_last_error(void) {
    return last_error;
}

bool xiso_list(const char* iso_path, char* output_buffer, size_t buffer_size) {
    uint32_t root_dir_sector, root_dir_size;
    struct stat st;
    
    DEBUG_PRINT("Starting XISO listing\n");
    DEBUG_PRINT("ISO path: %s\n", iso_path);
    
    if (!buffer) {
        set_error("XISO not initialized");
        return false;
    }

    // Check if ISO file exists and is readable
    if (stat(iso_path, &st) != 0) {
        set_error("Cannot access ISO file: %s (%s)", iso_path, strerror(errno));
        return false;
    }

    // Close any previously opened file
    if (iso_fd != -1) {
        close(iso_fd);
    }

    DEBUG_PRINT("Opening ISO file...\n");
    
    // Open the ISO file
    iso_fd = open(iso_path, O_RDONLY | O_BINARY);
    if (iso_fd == -1) {
        set_error("Failed to open ISO file: %s (%s)", iso_path, strerror(errno));
        return false;
    }

    DEBUG_PRINT("Verifying ISO format...\n");
    
    // Verify it's a valid Xbox ISO
    if (!verify_xiso(iso_path, &root_dir_sector, &root_dir_size)) {
        close(iso_fd);
        iso_fd = -1;
        return false;
    }

    // Set up list buffer
    list_buffer = output_buffer;
    list_buffer_size = buffer_size;
    list_buffer_pos = 0;

    // Start listing from root directory
    bool success = list_directory("", 
        root_dir_sector * XISO_SECTOR_SIZE + xbox_disc_lseek);

    close(iso_fd);
    iso_fd = -1;
    
    DEBUG_PRINT("Listing %s\n", success ? "completed successfully" : "failed");
    return success;
}

bool xiso_extract(const char* iso_path, const char* output_path) {
    uint32_t root_dir_sector, root_dir_size;
    struct stat st;
    
    DEBUG_PRINT("Starting XISO extraction\n");
    DEBUG_PRINT("ISO path: %s\n", iso_path);
    DEBUG_PRINT("Output path: %s\n", output_path);
    
    if (!buffer) {
        set_error("XISO not initialized");
        return false;
    }

    // Check if ISO file exists and is readable
    if (stat(iso_path, &st) != 0) {
        set_error("Cannot access ISO file: %s (%s)", iso_path, strerror(errno));
        return false;
    }

    // Close any previously opened file
    if (iso_fd != -1) {
        close(iso_fd);
    }

    DEBUG_PRINT("Opening ISO file...\n");
    
    // Open the ISO file
    iso_fd = open(iso_path, O_RDONLY | O_BINARY);
    if (iso_fd == -1) {
        set_error("Failed to open ISO file: %s (%s)", iso_path, strerror(errno));
        return false;
    }

    DEBUG_PRINT("Verifying ISO format...\n");
    
    // Verify it's a valid Xbox ISO
    if (!verify_xiso(iso_path, &root_dir_sector, &root_dir_size)) {
        close(iso_fd);
        iso_fd = -1;
        return false;
    }

    DEBUG_PRINT("Root directory sector: %u, size: %u\n", root_dir_sector, root_dir_size);
    DEBUG_PRINT("Beginning extraction at offset 0x%llx...\n", 
           (unsigned long long)(root_dir_sector * XISO_SECTOR_SIZE + xbox_disc_lseek));

    // Create root output directory
    if (mkdir(output_path, 0755) != 0 && errno != EEXIST) {
        set_error("Failed to create output directory: %s (%s)", output_path, strerror(errno));
        close(iso_fd);
        iso_fd = -1;
        return false;
    }

    // Start extraction from root directory
    bool success = extract_directory(output_path, 
        root_dir_sector * XISO_SECTOR_SIZE + xbox_disc_lseek);

    close(iso_fd);
    iso_fd = -1;
    
    DEBUG_PRINT("Extraction %s\n", success ? "completed successfully" : "failed");
    return success;
}

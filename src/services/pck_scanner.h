#pragma once

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include "extension_api.h"

using namespace godot;

struct PCKFileScanResult {
    enum FileScanError {
        FILE_OK,
        FILE_ENCRYPTED_ERR,
        UNTRUSTED_GDSCRIPT_ERR,
        UNTRUSTED_FILE_ERR,

        // Resources
        ENDIANNESS_ERR,
        RES_VERSION_ERR,
        GODOT_VERSION_ERR,
        SANDBOX_VIOLATION_ERR
    };

    enum SandboxViolations {
        // External resources
        UNTRUSTED_EXT_SCRIPT_VIOLATION,
        RESOURCE_SANDBOX_VIOLATION,

        // Internal resources
        UNTRUSTED_INT_SCRIPT_VIOLATION
    };

    String path;
    uint64_t offset;
    uint64_t size;
    PackedByteArray md5;

    Dictionary data;

    FileScanError status = FILE_OK;

    operator Dictionary() const;
};

struct PCKScanResult {
    enum PCKScanError {
        PCK_OK,
        FILE_ERR,
        MAGIC_NOT_FOUND_ERR,
        PACK_VERSION_ERR,
        GODOT_VERSION_ERR,
        PACK_ENCRYPTED_ERR,
        FILE_SCAN_ERR
    };

    String path;

    uint32_t format_version = 0;
    uint32_t version_major = 0;
    uint32_t version_minor = 0;
    uint32_t version_patch = 0;

    Vector<PCKFileScanResult> files;

    PCKScanError status = PCK_OK;

    operator Dictionary() const;
};

class PCKScanner {
    // Simplified FileAccess for reading binary resource files
    class ResourceFileAccess {
    public:
        virtual uint32_t get_32() = 0;
        virtual uint64_t get_64() = 0;
        virtual PackedByteArray get_buffer(int64_t p_size) = 0;
        virtual void seek(uint64_t p_position) = 0;

        bool real_is_double = false;

        String get_unicode_string();

        float get_float();
        double get_double();
        real_t get_real(); // get real
    };

    class ResourceCompressedFileAccess : public ResourceFileAccess {
        struct ReadBlock {
            uint32_t offset;
            uint32_t csize; // "Compressed size"?
        };

        Ref<FileAccess> file;
        FileAccess::CompressionMode mode;
        uint32_t block_size;
        uint32_t read_block_size;
        uint32_t read_total;
        Vector<ReadBlock> read_blocks;

        int current_block = 0;
        PackedByteArray block;
        int offset = 0;

        void read_block();

    public:
        uint8_t get_8();
        uint16_t get_16();
        uint32_t get_32() override;
        uint64_t get_64() override;
        PackedByteArray get_buffer(int64_t p_size) override;
        void seek(uint64_t p_position) override;

        ResourceCompressedFileAccess(Ref<FileAccess> p_file);
    };

    class ResourceUncompressedFileAccess : public ResourceFileAccess {
        Ref<FileAccess> file;
        uint64_t base_offset;

    public:
        uint32_t get_32() override { return file->get_32(); }
        uint64_t get_64() override { return file->get_64(); }
        PackedByteArray get_buffer(int64_t p_size) override { return file->get_buffer(p_size); }
        void seek(uint64_t p_position) override { return file->seek(base_offset + p_position); }

        ResourceUncompressedFileAccess(Ref<FileAccess> p_file, uint64_t p_base_offset) :
                file(p_file), base_offset(p_base_offset) {}
    };

    static void handle_binary_resource_internal(ResourceFileAccess &p_file, PCKFileScanResult &p_scan_result);
    static void handle_binary_resource(Ref<FileAccess> p_file, PCKFileScanResult &p_scan_result);
    static void handle_file(Ref<FileAccess> p_file, PCKFileScanResult &p_scan_result);

public:
    static PCKScanResult scan(const String &p_path);
};

const ApiEnum &get_pck_scan_error_enum();
const ApiEnum &get_pck_file_scan_error_enum();
const ApiEnum &get_sandbox_violations_enum();

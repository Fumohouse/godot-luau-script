#include "pck_scanner.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/resource_uid.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>

#include "services/sandbox_service.h"

using namespace godot;

/*
  Was rewriting the entire PCK and RES parsing necessary?
  For the level of granularity in scanning report I want, it arguably was.
  Some things are also rewritten because their APIs are not available in GDExtension.
  Hopefully this will not become a nightmare down the line.

  A lot of this is really similar to Godot code. See COPYRIGHT.txt for license information.
 */

#define PACK_HEADER_MAGIC 0x43504447 // GDPC
#define PACK_FORMAT_VERSION 2
#define RESOURCE_FORMAT_VERSION 5

/* GODOT ENUMS */

enum PackFlags {
    PACK_DIR_ENCRYPTED = 1 << 0
};

enum PackFileFlags {
    PACK_FILE_ENCRYPTED = 1 << 0
};

enum ResourceFormatBinaryFlags {
    FORMAT_FLAG_NAMED_SCENE_IDS = 1,
    FORMAT_FLAG_UIDS = 2,
    FORMAT_FLAG_REAL_T_IS_DOUBLE = 4,
    FORMAT_FLAG_HAS_SCRIPT_CLASS = 8,

    RESOURCE_RESERVED_FIELDS = 11
};

/* FILE ACCESS */
// Reference: https://github.com/godotengine/godot/blob/master/core/io/file_access_compressed.cpp#L55-L90

void PCKScanner::ResourceCompressedFileAccess::read_block() {
    const ReadBlock &read_block = read_blocks[current_block];
    file->seek(read_block.offset);

    block = file->get_buffer(read_block.csize).decompress(read_blocks.size() == 1 ? read_total : block_size, mode);
    offset = 0;

    // Limit how far we can read - does not affect block size when decompressing
    read_block_size = current_block == read_blocks.size() - 1 ? read_total % block_size : block_size;
}

uint8_t PCKScanner::ResourceCompressedFileAccess::get_8() {
    if (current_block >= read_blocks.size()) {
        // EOF
        return 0;
    }

    uint8_t byte = block.decode_u8(offset);

    offset++;

    if (offset >= read_block_size) {
        current_block++;

        if (current_block < read_blocks.size()) {
            read_block();
        }
    }

    return byte;
}

uint16_t PCKScanner::ResourceCompressedFileAccess::get_16() {
    uint8_t a = get_8();
    uint8_t b = get_8();

    return (b << 8) | a;
}

uint32_t PCKScanner::ResourceCompressedFileAccess::get_32() {
    uint16_t a = get_16();
    uint16_t b = get_16();

    return (b << 16) | a;
}

uint64_t PCKScanner::ResourceCompressedFileAccess::get_64() {
    uint64_t a = get_32();
    uint64_t b = get_32();

    return (b << 32) | a;
}

PackedByteArray PCKScanner::ResourceCompressedFileAccess::get_buffer(int64_t p_size) {
    // Sorry
    PackedByteArray buf;
    buf.resize(p_size);
    for (int i = 0; i < p_size; i++) {
        buf[i] = get_8();
    }

    return buf;
}

void PCKScanner::ResourceCompressedFileAccess::seek(uint64_t p_position) {
    uint32_t target_block = p_position / block_size;
    if (current_block != target_block) {
        current_block = target_block;
        read_block();
    }

    offset = p_position % block_size;
}

// Reference: https://github.com/godotengine/godot/blob/master/core/io/resource_format_binary.cpp#L886-L898
String PCKScanner::ResourceFileAccess::get_unicode_string() {
    uint32_t len = get_32();
    return get_buffer(len).get_string_from_utf8();
}

// Reference: https://github.com/godotengine/godot/blob/a574c0296b38d5f786f249b12e6251e562c528cc/core/io/file_access.cpp#L284C1-L290
union MarshallFloat {
    uint32_t i;
    float f;
};

union MarshallDouble {
    uint64_t i;
    double d;
};

float PCKScanner::ResourceFileAccess::get_float() {
    MarshallFloat mf;
    mf.i = get_32();

    return mf.f;
}

double PCKScanner::ResourceFileAccess::get_double() {
    MarshallDouble md;
    md.i = get_64();

    return md.d;
}

real_t PCKScanner::ResourceFileAccess::get_real() {
    return real_is_double ? get_double() : get_float();
}

PCKScanner::ResourceCompressedFileAccess::ResourceCompressedFileAccess(Ref<FileAccess> p_file) :
        file(p_file) {
    mode = static_cast<FileAccess::CompressionMode>(file->get_32());
    block_size = file->get_32();
    read_total = file->get_32();

    uint32_t block_count = (read_total / block_size) + 1;
    uint64_t block_offset = file->get_position() + block_count * 4; // Skip 4 bytes each block for csize

    for (int i = 0; i < block_count; i++) {
        ReadBlock block;
        block.offset = block_offset;
        block.csize = file->get_32();
        block_offset += block.csize;

        read_blocks.push_back(block);
    }

    read_block();
}

/* RESOURCE SCANNING */

struct IntResource {
    String path;
    uint64_t offset;
};

void PCKScanner::handle_binary_resource_internal(ResourceFileAccess &p_file, PCKFileScanResult &p_scan_result) {
    bool big_endian = p_file.get_32();
    if (big_endian) {
        p_scan_result.status = PCKFileScanResult::ENDIANNESS_ERR;
        return;
    }

    Dictionary &data = p_scan_result.data;
    PackedInt32Array violations;

    // 1. Read header

    // 1.1. Basic attributes
    bool use_real64 = p_file.get_32();

    uint32_t version_major = p_file.get_32();
    uint32_t version_minor = p_file.get_32();
    uint32_t format_version = p_file.get_32();

    data["version_major"] = version_major;
    data["version_minor"] = version_minor;
    data["format_version"] = format_version;

    if (format_version > RESOURCE_FORMAT_VERSION) {
        p_scan_result.status = PCKFileScanResult::RES_VERSION_ERR;
        return;
    }

    if (version_major > internal::godot_version.major) {
        p_scan_result.status = PCKFileScanResult::GODOT_VERSION_ERR;
        return;
    }

    String type = p_file.get_unicode_string();
    data["type"] = type;

    p_file.get_64(); // "importmd_ofs" - I don't know what this is
    uint32_t flags = p_file.get_32();

    bool using_named_scene_ids = (flags & FORMAT_FLAG_NAMED_SCENE_IDS) != 0;
    bool using_uids = (flags & FORMAT_FLAG_UIDS) != 0;
    bool real_is_double = (flags & FORMAT_FLAG_REAL_T_IS_DOUBLE) != 0;
    bool has_script_class = (flags & FORMAT_FLAG_HAS_SCRIPT_CLASS) != 0;

    data["using_named_scene_ids"] = using_named_scene_ids;
    data["using_uids"] = using_uids;
    data["real_is_double"] = real_is_double;

    p_file.real_is_double = real_is_double;

    // 1.2. UID
    int64_t uid;

    if (using_uids) {
        uid = p_file.get_64();
    } else {
        p_file.get_64();
        uid = ResourceUID::INVALID_ID;
    }

    data["uid"] = uid;

    // 1.3. Class
    String script_class;

    if (has_script_class) {
        script_class = p_file.get_unicode_string();
    }

    data["script_class"] = script_class;

    // 1.4. Reserved
    for (int i = 0; i < RESOURCE_RESERVED_FIELDS; i++) {
        p_file.get_32();
    }

    // 2. String table (unused by us)
    uint32_t string_table_size = p_file.get_32();

    for (int i = 0; i < string_table_size; i++) {
        p_file.get_unicode_string();
    }

    // 3. External resources
    uint32_t ext_resources_size = p_file.get_32();
    Array ext_resources;

    for (int i = 0; i < ext_resources_size; i++) {
        String type = p_file.get_unicode_string();
        String path = p_file.get_unicode_string();

        Dictionary res;
        res["type"] = type;
        res["path"] = path;

        ext_resources.push_back(res);

        if (type == "Script" && path.get_extension() != "lua") {
            violations.push_back(PCKFileScanResult::UNTRUSTED_EXT_SCRIPT_VIOLATION);
        }

        // If the file does not exist, assume it is in this PCK and allow access.
        if (FileAccess::file_exists(path) &&
                (!SandboxService::get_singleton() ||
                        !SandboxService::get_singleton()->resource_has_access(path, SandboxService::RESOURCE_READ_ONLY))) {
            violations.push_back(PCKFileScanResult::RESOURCE_SANDBOX_VIOLATION);
        }

        if (using_uids) {
            p_file.get_64(); // UID
        }
    }

    data["ext_resources"] = ext_resources;

    // 4. Internal resources

    // 4.1. Layout table
    uint32_t int_resources_size = p_file.get_32();
    Vector<IntResource> int_resources;
    int_resources.resize(int_resources_size);

    for (int i = 0; i < int_resources_size; i++) {
        IntResource res;
        res.path = p_file.get_unicode_string();
        res.offset = p_file.get_64();

        int_resources.set(i, res);
    }

    // 4.2. Read resources
    // Reference: https://github.com/godotengine/godot/blob/master/core/io/resource_format_binary.cpp#L730-L863
    Array int_resources_out;

    for (const IntResource &int_res : int_resources) {
        p_file.seek(int_res.offset);

        String type = p_file.get_unicode_string();
        int property_count = p_file.get_32();

        // I do not know of a way to smuggle a GDScript into properties, so I won't read them.

        Dictionary res;
        res["path"] = int_res.path;
        res["type"] = type;
        res["property_count"] = property_count;

        int_resources_out.push_back(res);

        if (type == "GDScript") {
            violations.push_back(PCKFileScanResult::UNTRUSTED_INT_SCRIPT_VIOLATION);
        }
    }

    data["int_resources"] = int_resources_out;

    // 5. Finish
    if (violations.size() > 0) {
        p_scan_result.status = PCKFileScanResult::SANDBOX_VIOLATION_ERR;
        data["violations"] = violations;
    }
}

void PCKScanner::handle_binary_resource(Ref<FileAccess> p_file, PCKFileScanResult &p_scan_result) {
    // Reference: https://github.com/godotengine/godot/blob/master/core/io/resource_format_binary.cpp#L947-L1083
    p_file->seek(p_scan_result.offset);

    String magic = p_file->get_buffer(4).get_string_from_utf8();

    if (magic == "RSCC") {
        // File is compressed (default for some scenes)
        ResourceCompressedFileAccess res_file(p_file);
        handle_binary_resource_internal(res_file, p_scan_result);
    } else if (magic == "RSRC") {
        // File is not uncompressed
        ResourceUncompressedFileAccess res_file(p_file, p_scan_result.offset);
        handle_binary_resource_internal(res_file, p_scan_result);
    } else {
        p_scan_result.status = PCKFileScanResult::UNTRUSTED_FILE_ERR;
    }
}

/* PCK READING */

void PCKScanner::handle_file(Ref<FileAccess> p_file, PCKFileScanResult &p_scan_result) {
    String extension = p_scan_result.path.get_extension();

    if (extension == "res" || extension == "scn") {
        handle_binary_resource(p_file, p_scan_result);
    } else if (extension == "gd") {
        p_scan_result.status = PCKFileScanResult::UNTRUSTED_GDSCRIPT_ERR;
    } else if (extension == "tres" || extension == "tscn") {
        // Shouldn't be possible but you never know
        p_scan_result.status = PCKFileScanResult::UNTRUSTED_FILE_ERR;
    }
}

PCKFileScanResult::operator Dictionary() const {
    Dictionary out;

    out["path"] = path;
    out["offset"] = offset;
    out["size"] = size;
    out["md5"] = md5;
    out["data"] = data;
    out["status"] = status;

    return out;
}

PCKScanResult::operator Dictionary() const {
    Dictionary out;

    out["path"] = path;

    out["format_version"] = format_version;
    out["version_major"] = version_major;
    out["version_minor"] = version_minor;
    out["version_patch"] = version_patch;

    Array files_out;

    for (const PCKFileScanResult &file : files) {
        files_out.push_back(file.operator Dictionary());
    }

    out["files"] = files_out;

    out["status"] = status;

    return out;
}

// Scan a PCK for sandboxing issues or unexpected content.
// Designed to be as strict as possible - compatibility with all Godot features should not be expected.
// Any PCK that passes the scan should be loaded without replacing files - Godot exports project files like project.binary by default.
PCKScanResult PCKScanner::scan(const String &p_path) {
    PCKScanResult result;
    result.path = p_path;

    // 1. Open file
    Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::READ);

    if (file.is_null()) {
        result.status = PCKScanResult::FILE_ERR;
        return result;
    }

    // 2. Read PCK
    // Reference: https://github.com/godotengine/godot/blob/fbe611e45eebe48e2fdf4065fc70acad1cca2e0e/core/io/file_access_pack.cpp#L130-L255
    // Only PCKs that start at offset 0 are supported.

    // 2.1. Format validation
    uint32_t magic = file->get_32();
    if (magic != PACK_HEADER_MAGIC) {
        result.status = PCKScanResult::MAGIC_NOT_FOUND_ERR;
        return result;
    }

    uint32_t format_version = file->get_32();
    uint32_t version_major = file->get_32();
    uint32_t version_minor = file->get_32();
    uint32_t version_patch = file->get_32();

    result.format_version = format_version;
    result.version_major = version_major;
    result.version_minor = version_minor;
    result.version_patch = version_patch;

    if (format_version != PACK_FORMAT_VERSION) {
        result.status = PCKScanResult::PACK_VERSION_ERR;
        return result;
    }

    if (version_major > internal::godot_version.major ||
            (version_major == internal::godot_version.major && version_minor > internal::godot_version.minor)) {
        result.status = PCKScanResult::GODOT_VERSION_ERR;
        return result;
    }

    // 2.2. Read file table
    uint32_t pack_flags = file->get_32();
    uint32_t file_base = file->get_64(); // Offset in this PCK of first file

    if (pack_flags & PACK_DIR_ENCRYPTED) {
        result.status = PCKScanResult::PACK_ENCRYPTED_ERR;
        return result;
    }

    // Reserved
    for (int i = 0; i < 16; i++) {
        file->get_32();
    }

    int file_count = file->get_32();
    result.files.resize(file_count);

    for (int i = 0; i < file_count; i++) {
        PCKFileScanResult file_result;

        uint32_t path_length = file->get_32();
        String file_path = file->get_buffer(path_length).get_string_from_utf8();

        uint32_t offset = file->get_64(); // File offset from file_base
        uint64_t size = file->get_64();
        PackedByteArray md5 = file->get_buffer(16);

        file_result.path = file_path;
        file_result.offset = file_base + offset;
        file_result.size = size;
        file_result.md5 = md5;

        uint32_t flags = file->get_32();

        if (flags & PACK_FILE_ENCRYPTED) {
            file_result.status = PCKFileScanResult::FILE_ENCRYPTED_ERR;
        }

        result.files.set(i, file_result);
    }

    // 2.3. Read files
    for (PCKFileScanResult &file_result : result.files) {
        handle_file(file, file_result);

        if (file_result.status != PCKFileScanResult::FILE_OK) {
            result.status = PCKScanResult::FILE_SCAN_ERR;
        }
    }

    return result;
}

const ApiEnum &get_pck_scan_error_enum() {
    static ApiEnum e = {
        "PCKScanError",
        false,
        {
                { "OK", PCKScanResult::PCK_OK },
                { "FILE_ERR", PCKScanResult::FILE_ERR },
                { "MAGIC_NOT_FOUND_ERR", PCKScanResult::MAGIC_NOT_FOUND_ERR },
                { "PACK_VERSION_ERR", PCKScanResult::PACK_VERSION_ERR },
                { "GODOT_VERSION_ERR", PCKScanResult::GODOT_VERSION_ERR },
                { "PACK_ENCRYPTED_ERR", PCKScanResult::PACK_ENCRYPTED_ERR },
                { "FILE_SCAN_ERR", PCKScanResult::FILE_SCAN_ERR },
        }
    };

    return e;
}

const ApiEnum &get_pck_file_scan_error_enum() {
    static ApiEnum e = {
        "PCKFileScanError",
        false,
        {
                { "OK", PCKFileScanResult::FILE_OK },
                { "FILE_ENCRYPTED_ERR", PCKFileScanResult::FILE_ENCRYPTED_ERR },
                { "UNTRUSTED_GDSCRIPT_ERR", PCKFileScanResult::UNTRUSTED_GDSCRIPT_ERR },
                { "UNTRUSTED_FILE_ERR", PCKFileScanResult::UNTRUSTED_FILE_ERR },
                { "ENDIANNESS_ERR", PCKFileScanResult::ENDIANNESS_ERR },
                { "RES_VERSION_ERR", PCKFileScanResult::RES_VERSION_ERR },
                { "GODOT_VERSION_ERR", PCKFileScanResult::GODOT_VERSION_ERR },
                { "SANDBOX_VIOLATION_ERR", PCKFileScanResult::SANDBOX_VIOLATION_ERR },
        }
    };

    return e;
}

const ApiEnum &get_sandbox_violations_enum() {
    static ApiEnum e = {
        "SandboxViolations",
        false,
        {
                { "UNTRUSTED_EXT_SCRIPT_VIOLATION", PCKFileScanResult::UNTRUSTED_EXT_SCRIPT_VIOLATION },
                { "RESOURCE_SANDBOX_VIOLATION", PCKFileScanResult::RESOURCE_SANDBOX_VIOLATION },
                { "UNTRUSTED_INT_SCRIPT_VIOLATION", PCKFileScanResult::UNTRUSTED_INT_SCRIPT_VIOLATION },
        }
    };

    return e;
}

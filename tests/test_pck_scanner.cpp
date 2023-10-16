#include <catch_amalgamated.hpp>

#include <godot_cpp/variant/packed_int32_array.hpp>

#include "services/pck_scanner.h"

TEST_CASE("pck scanner: header check") {
    SECTION("file doesn't exist") {
        PCKScanResult res = PCKScanner::scan("res://qwoietuoiwq/wohsdlgkh/zxcobnjkdg.pck");
        REQUIRE(res.status == PCKScanResult::FILE_ERR);
    }

    SECTION("no magic") {
        PCKScanResult res = PCKScanner::scan("res://icon.png");
        REQUIRE(res.status == PCKScanResult::MAGIC_NOT_FOUND_ERR);
    }

    SECTION("wrong format version") {
        PCKScanResult res = PCKScanner::scan("res://test_pcks/wrong_format_version.pck");
        REQUIRE(res.status == PCKScanResult::PACK_VERSION_ERR);
    }

    SECTION("wrong Godot version") {
        PCKScanResult res = PCKScanner::scan("res://test_pcks/wrong_godot_version.pck");
        REQUIRE(res.status == PCKScanResult::GODOT_VERSION_ERR);
    }

    SECTION("encrypted") {
        PCKScanResult res = PCKScanner::scan("res://test_pcks/encrypted.pck");
        REQUIRE(res.status == PCKScanResult::PACK_ENCRYPTED_ERR);
    }
}

TEST_CASE("pck scanner: file scanning") {
    PCKScanResult res = PCKScanner::scan("res://test_pcks/invalid.pck");
    REQUIRE(res.status == PCKScanResult::FILE_SCAN_ERR);

    bool shader_found = false;
    bool ext_gdscript_found = false;
    bool int_gdscript_res_found = false;
    bool int_gdscript_scn_found = false;
    bool glb_found = false;
    bool gdscript_found = false;

    for (const PCKFileScanResult &file : res.files) {
        if (file.path == "res://test_pcks/invalid/shader.gdshader") {
            shader_found = true;

            REQUIRE(file.status == PCKFileScanResult::FILE_OK);
        } else if (file.path == "res://.godot/exported/133200997/export-f81e2091fa44aee5846745922871b64c-gdscript_external.scn") {
            ext_gdscript_found = true;

            REQUIRE(file.status == PCKFileScanResult::SANDBOX_VIOLATION_ERR);

            PackedInt32Array violations = file.data["violations"];
            REQUIRE(violations.size() == 2);
            REQUIRE(violations.has(PCKFileScanResult::UNTRUSTED_EXT_SCRIPT_VIOLATION));
            // Ordinarily it would not but SandboxService is not available
            REQUIRE(violations.has(PCKFileScanResult::RESOURCE_SANDBOX_VIOLATION));
        } else if (file.path == "res://.godot/exported/133200997/export-deae03310136bb3d494e17b923a59233-gdscript_resource.res") {
            int_gdscript_res_found = true;

            REQUIRE(file.status == PCKFileScanResult::SANDBOX_VIOLATION_ERR);

            PackedInt32Array violations = file.data["violations"];
            REQUIRE(violations.size() == 1);
            REQUIRE(violations.has(PCKFileScanResult::UNTRUSTED_INT_SCRIPT_VIOLATION));
        } else if (file.path == "res://.godot/exported/133200997/export-242919a3b633072c2a3294b3d4c9cee8-gdscript_builtin.scn") {
            int_gdscript_scn_found = true;

            REQUIRE(file.status == PCKFileScanResult::SANDBOX_VIOLATION_ERR);

            PackedInt32Array violations = file.data["violations"];
            REQUIRE(violations.size() == 2);
            REQUIRE(violations.has(PCKFileScanResult::RESOURCE_SANDBOX_VIOLATION));
            REQUIRE(violations.has(PCKFileScanResult::UNTRUSTED_INT_SCRIPT_VIOLATION));
        } else if (file.path == "res://.godot/imported/model.glb-63ce3b4dc0361b13ea156e6a79ea5346.scn") {
            glb_found = true;

            REQUIRE(file.status == PCKFileScanResult::FILE_OK);
        } else if (file.path == "res://test_pcks/invalid/gdscript.gd") {
            gdscript_found = true;

            REQUIRE(file.status == PCKFileScanResult::UNTRUSTED_GDSCRIPT_ERR);
        }
    }

    REQUIRE(shader_found);
    REQUIRE(ext_gdscript_found);
    REQUIRE(int_gdscript_res_found);
    REQUIRE(int_gdscript_scn_found);
    REQUIRE(glb_found);
    REQUIRE(gdscript_found);
}

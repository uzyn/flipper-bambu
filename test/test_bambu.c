/**
 * Bambu Lab NFC Parser Tests
 *
 * Standalone C test suite that tests the actual production code
 * from plugin/bambu_parser.h. Loads real NFC dump files from
 * test/data/ and validates parsing.
 *
 * Build: gcc -o test_bambu test_bambu.c -lm
 * Run: ./test_bambu
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

// ============================================================================
// Mock Flipper Zero types for standalone testing
// These must be defined BEFORE including the shared parser header
// ============================================================================

typedef enum {
    MfClassicType1k,
    MfClassicType4k,
} MfClassicType;

typedef struct {
    uint8_t data[16];
} MfClassicBlock;

typedef struct {
    MfClassicType type;
    MfClassicBlock block[64];  // 1K has 64 blocks
} MfClassicData;

// ============================================================================
// Include the actual production code being tested
// ============================================================================

#include "../plugin/bambu_parser.h"
#include "../plugin/bambu_filaments.h"

// ============================================================================
// NFC file parser
// ============================================================================

static int parse_hex_byte(const char* str) {
    int result = 0;
    for (int i = 0; i < 2; i++) {
        char c = str[i];
        result <<= 4;
        if (c >= '0' && c <= '9') {
            result |= c - '0';
        } else if (c >= 'A' && c <= 'F') {
            result |= c - 'A' + 10;
        } else if (c >= 'a' && c <= 'f') {
            result |= c - 'a' + 10;
        } else if (c == '?') {
            return -1;  // Unknown data (sector trailer)
        } else {
            return -1;
        }
    }
    return result;
}

static bool load_nfc_file(const char* path, MfClassicData* data) {
    FILE* f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "Failed to open: %s\n", path);
        return false;
    }

    // Initialize
    memset(data, 0, sizeof(MfClassicData));
    data->type = MfClassicType1k;

    char line[256];
    bool found_type = false;

    while (fgets(line, sizeof(line), f)) {
        // Check for Mifare Classic type
        if (strncmp(line, "Mifare Classic type: 1K", 23) == 0) {
            data->type = MfClassicType1k;
            found_type = true;
        } else if (strncmp(line, "Mifare Classic type: 4K", 23) == 0) {
            data->type = MfClassicType4k;
            found_type = true;
        }
        // Parse block data
        else if (strncmp(line, "Block ", 6) == 0) {
            int block_num;
            char hex_data[64];
            // Parse "Block N: XX XX XX ..."
            if (sscanf(line, "Block %d: %[^\n]", &block_num, hex_data) == 2) {
                if (block_num >= 0 && block_num < 64) {
                    // Parse hex bytes
                    char* ptr = hex_data;
                    for (int i = 0; i < 16 && *ptr; i++) {
                        // Skip whitespace
                        while (*ptr == ' ') ptr++;
                        if (!*ptr) break;

                        int byte = parse_hex_byte(ptr);
                        if (byte >= 0) {
                            data->block[block_num].data[i] = (uint8_t)byte;
                        }
                        ptr += 2;
                    }
                }
            }
        }
    }

    fclose(f);
    return found_type;
}

// ============================================================================
// Test framework
// ============================================================================

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("  FAIL: %s\n", msg); \
        return false; \
    } \
} while(0)

#define TEST_ASSERT_EQ_STR(expected, actual, field) do { \
    if (strcmp(expected, actual) != 0) { \
        printf("  FAIL: %s - expected '%s', got '%s'\n", field, expected, actual); \
        return false; \
    } \
} while(0)

#define TEST_ASSERT_EQ_INT(expected, actual, field) do { \
    if ((expected) != (actual)) { \
        printf("  FAIL: %s - expected %d, got %d\n", field, (int)(expected), (int)(actual)); \
        return false; \
    } \
} while(0)

#define TEST_ASSERT_EQ_FLOAT(expected, actual, tolerance, field) do { \
    if (fabs((expected) - (actual)) > (tolerance)) { \
        printf("  FAIL: %s - expected %.2f, got %.2f\n", field, (double)(expected), (double)(actual)); \
        return false; \
    } \
} while(0)

// ============================================================================
// Expected values for test files
// ============================================================================

typedef struct {
    const char* filename;
    const char* material_id;
    const char* variant_id;
    const char* filament_code;
    const char* color_name;
    const char* filament_type;
    const char* detailed_type;
    uint16_t weight_grams;
    float diameter_mm;
    uint16_t drying_temp_c;
    uint16_t drying_hours;
    uint16_t hotend_max_c;
    uint16_t hotend_min_c;
    float nozzle_diameter_mm;
    float spool_width_mm;
    const char* production_date;
    uint16_t filament_length_m;
    uint8_t color_r;
    uint8_t color_g;
    uint8_t color_b;
    uint8_t color_a;
} ExpectedValues;

static const ExpectedValues expected_values[] = {
    {
        .filename = "Bambu_pink.nfc",
        .material_id = "GFA00",
        .variant_id = "A00-R3",
        .filament_code = "10204",
        .color_name = "Hot Pink",
        .filament_type = "PLA",
        .detailed_type = "PLA Basic",
        .weight_grams = 1000,
        .diameter_mm = 1.75f,
        .drying_temp_c = 55,
        .drying_hours = 8,
        .hotend_max_c = 230,
        .hotend_min_c = 190,
        .nozzle_diameter_mm = 0.2f,
        .spool_width_mm = 32.12f,
        .production_date = "2025_07_21_14_17",
        .filament_length_m = 330,
        .color_r = 0xF5,
        .color_g = 0x54,
        .color_b = 0x7C,
        .color_a = 0xFF,
    },
    {
        .filename = "Bambu_red.nfc",
        .material_id = "GFA01",
        .variant_id = "A01-R4",
        .filament_code = "11202",
        .color_name = "Dark Red",
        .filament_type = "PLA",
        .detailed_type = "PLA Matte",
        .weight_grams = 1000,
        .diameter_mm = 1.75f,
        .drying_temp_c = 55,
        .drying_hours = 8,
        .hotend_max_c = 230,
        .hotend_min_c = 190,
        .nozzle_diameter_mm = 0.2f,
        .spool_width_mm = 11.49f,
        .production_date = "2025_10_06_09_19",
        .filament_length_m = 315,
        .color_r = 0xBB,
        .color_g = 0x3D,
        .color_b = 0x43,
        .color_a = 0xFF,
    },
    {
        .filename = "Bambu_wood.nfc",
        .material_id = "GFA16",
        .variant_id = "A16-W0",
        .filament_code = "13106",
        .color_name = "White Oak",
        .filament_type = "PLA",
        .detailed_type = "PLA Wood",
        .weight_grams = 1000,
        .diameter_mm = 1.75f,
        .drying_temp_c = 60,
        .drying_hours = 6,
        .hotend_max_c = 230,
        .hotend_min_c = 190,
        .nozzle_diameter_mm = 0.2f,
        .spool_width_mm = 15.36f,
        .production_date = "2024_12_04_18_34",
        .filament_length_m = 330,
        .color_r = 0xD6,
        .color_g = 0xCC,
        .color_b = 0xA3,
        .color_a = 0xFF,
    },
    {
        .filename = "Bambu_abs.nfc",
        .material_id = "GFB00",
        .variant_id = "B00-D1",
        .filament_code = "40102",
        .color_name = "Silver",
        .filament_type = "ABS",
        .detailed_type = "ABS",
        .weight_grams = 1000,
        .diameter_mm = 1.75f,
        .drying_temp_c = 80,
        .drying_hours = 8,
        .hotend_max_c = 270,
        .hotend_min_c = 240,
        .nozzle_diameter_mm = 0.2f,
        .spool_width_mm = 66.25f,
        .production_date = "2025_05_15_14_32",
        .filament_length_m = 398,
        .color_r = 0x87,
        .color_g = 0x90,
        .color_b = 0x9A,
        .color_a = 0xFF,
    },
    {
        .filename = "Bambu_petg.nfc",
        .material_id = "GFG02",
        .variant_id = "G02-K0",
        .filament_code = "33102",
        .color_name = "Black",
        .filament_type = "PETG",
        .detailed_type = "PETG HF",
        .weight_grams = 1000,
        .diameter_mm = 1.75f,
        .drying_temp_c = 65,
        .drying_hours = 8,
        .hotend_max_c = 260,
        .hotend_min_c = 230,
        .nozzle_diameter_mm = 0.2f,
        .spool_width_mm = 6.66f,
        .production_date = "2025_06_23_10_46",
        .filament_length_m = 325,
        .color_r = 0x00,
        .color_g = 0x00,
        .color_b = 0x00,
        .color_a = 0xFF,
    },
    {
        .filename = "Bambu_translucent_blu.nfc",
        .material_id = "GFG01",
        .variant_id = "G01-B0",
        .filament_code = "32600",
        .color_name = "Translucent Light Blue",
        .filament_type = "PETG",
        .detailed_type = "PETG Translucent",
        .weight_grams = 1000,
        .diameter_mm = 1.75f,
        .drying_temp_c = 65,
        .drying_hours = 8,
        .hotend_max_c = 260,
        .hotend_min_c = 230,
        .nozzle_diameter_mm = 0.2f,
        .spool_width_mm = 2.01f,
        .production_date = "2025_08_19_09_52",
        .filament_length_m = 330,
        .color_r = 0x61,
        .color_g = 0xB0,
        .color_b = 0xFF,
        .color_a = 0x80,
    },
};

#define NUM_EXPECTED_VALUES (sizeof(expected_values) / sizeof(expected_values[0]))

// ============================================================================
// Test cases - using functions from the actual production code
// ============================================================================

static bool test_parse_file(const char* test_dir, const ExpectedValues* expected) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", test_dir, expected->filename);

    MfClassicData data;
    if (!load_nfc_file(path, &data)) {
        printf("  FAIL: Could not load file %s\n", path);
        return false;
    }

    // Test validation using production code
    TEST_ASSERT(bambu_tag_is_valid(&data), "bambu_tag_is_valid should return true");

    // Parse material ID and variant ID from Block 1 using production code
    const uint8_t* block1 = data.block[BLOCK_MATERIAL_IDS].data;
    char material_id[7] = {0};
    char variant_id[8] = {0};
    bambu_copy_ascii_string(material_id, &block1[8], 6);
    bambu_copy_ascii_string(variant_id, &block1[0], 7);

    TEST_ASSERT_EQ_STR(expected->material_id, material_id, "material_id");
    TEST_ASSERT_EQ_STR(expected->variant_id, variant_id, "variant_id");

    // Parse filament type from Block 2 using production code
    const uint8_t* block2 = data.block[BLOCK_FILAMENT_TYPE].data;
    char filament_type[17] = {0};
    bambu_copy_ascii_string(filament_type, block2, 16);
    TEST_ASSERT_EQ_STR(expected->filament_type, filament_type, "filament_type");

    // Parse detailed type from Block 4 using production code
    const uint8_t* block4 = data.block[BLOCK_DETAILED_TYPE].data;
    char detailed_type[17] = {0};
    bambu_copy_ascii_string(detailed_type, block4, 16);
    TEST_ASSERT_EQ_STR(expected->detailed_type, detailed_type, "detailed_type");

    // Parse color, weight, diameter from Block 5 using production code
    const uint8_t* block5 = data.block[BLOCK_COLOR_WEIGHT].data;
    uint8_t color_r = block5[0];
    uint8_t color_g = block5[1];
    uint8_t color_b = block5[2];
    uint8_t color_a = block5[3];
    uint16_t weight_grams = bambu_read_le16(&block5[4]);
    float diameter_mm = bambu_read_le_float(&block5[8]);
    TEST_ASSERT_EQ_INT(expected->color_r, color_r, "color_r");
    TEST_ASSERT_EQ_INT(expected->color_g, color_g, "color_g");
    TEST_ASSERT_EQ_INT(expected->color_b, color_b, "color_b");
    TEST_ASSERT_EQ_INT(expected->color_a, color_a, "color_a");
    TEST_ASSERT_EQ_INT(expected->weight_grams, weight_grams, "weight_grams");
    TEST_ASSERT_EQ_FLOAT(expected->diameter_mm, diameter_mm, 0.01f, "diameter_mm");

    // Parse temperatures from Block 6 using production code
    const uint8_t* block6 = data.block[BLOCK_TEMPERATURES].data;
    uint16_t drying_temp = bambu_read_le16(&block6[0]);
    uint16_t drying_hours = bambu_read_le16(&block6[2]);
    uint16_t hotend_max = bambu_read_le16(&block6[8]);
    uint16_t hotend_min = bambu_read_le16(&block6[10]);
    TEST_ASSERT_EQ_INT(expected->drying_temp_c, drying_temp, "drying_temp_c");
    TEST_ASSERT_EQ_INT(expected->drying_hours, drying_hours, "drying_hours");
    TEST_ASSERT_EQ_INT(expected->hotend_max_c, hotend_max, "hotend_max_c");
    TEST_ASSERT_EQ_INT(expected->hotend_min_c, hotend_min, "hotend_min_c");

    // Parse nozzle diameter from Block 8 using production code
    const uint8_t* block8 = data.block[BLOCK_NOZZLE].data;
    float nozzle_diameter = bambu_read_le_float(&block8[12]);
    TEST_ASSERT_EQ_FLOAT(expected->nozzle_diameter_mm, nozzle_diameter, 0.01f, "nozzle_diameter_mm");

    // Parse spool width from Block 10 using production code
    const uint8_t* block10 = data.block[BLOCK_SPOOL_WIDTH].data;
    uint16_t spool_width_raw = bambu_read_le16(&block10[4]);
    float spool_width_mm = (float)spool_width_raw / 100.0f;
    TEST_ASSERT_EQ_FLOAT(expected->spool_width_mm, spool_width_mm, 0.01f, "spool_width_mm");

    // Parse production date from Block 12 using production code
    const uint8_t* block12 = data.block[BLOCK_PRODUCTION_DATE].data;
    char production_date[17] = {0};
    bambu_copy_ascii_string(production_date, block12, 16);
    TEST_ASSERT_EQ_STR(expected->production_date, production_date, "production_date");

    // Parse filament length from Block 14 using production code
    const uint8_t* block14 = data.block[BLOCK_FILAMENT_LENGTH].data;
    uint16_t filament_length = bambu_read_le16(&block14[4]);
    TEST_ASSERT_EQ_INT(expected->filament_length_m, filament_length, "filament_length_m");

    // Test filament lookup using production code
    const BambuFilamentInfo* info = bambu_lookup_filament(variant_id);
    TEST_ASSERT(info != NULL, "filament lookup should succeed");
    TEST_ASSERT_EQ_STR(expected->filament_code, info->filament_code, "filament_code");
    TEST_ASSERT_EQ_STR(expected->color_name, info->color_name, "color_name");

    return true;
}

// Test rejection cases - tags that should NOT be detected as Bambu
static bool test_rejection_missing_gf_prefix(void) {
    MfClassicData data;
    memset(&data, 0, sizeof(data));
    data.type = MfClassicType1k;

    // Set up Block 1 WITHOUT "GF" prefix at bytes 8-9
    memcpy(data.block[BLOCK_MATERIAL_IDS].data, "A00-R3\x00\x00XX", 10);

    // Set up valid Block 2 (filament type)
    memcpy(data.block[BLOCK_FILAMENT_TYPE].data, "PLA\x00", 4);

    // Set up valid Block 4 (detailed type)
    memcpy(data.block[BLOCK_DETAILED_TYPE].data, "PLA Basic\x00", 10);

    // Set up valid Block 5 with valid diameter (1.75mm)
    uint8_t block5[16] = {0};
    float diameter = 1.75f;
    memcpy(&block5[8], &diameter, sizeof(float));
    memcpy(data.block[BLOCK_COLOR_WEIGHT].data, block5, 16);

    TEST_ASSERT(!bambu_tag_is_valid(&data), "should reject tag without GF prefix");
    return true;
}

static bool test_rejection_unknown_filament_type(void) {
    MfClassicData data;
    memset(&data, 0, sizeof(data));
    data.type = MfClassicType1k;

    // Set up valid Block 1 with GF prefix
    memcpy(data.block[BLOCK_MATERIAL_IDS].data, "A00-R3\x00\x00GFA00\x00", 14);

    // Set up Block 2 with UNKNOWN filament type
    memcpy(data.block[BLOCK_FILAMENT_TYPE].data, "UNKNOWN\x00", 8);

    // Set up valid Block 4
    memcpy(data.block[BLOCK_DETAILED_TYPE].data, "PLA Basic\x00", 10);

    // Set up valid Block 5 with valid diameter
    uint8_t block5[16] = {0};
    float diameter = 1.75f;
    memcpy(&block5[8], &diameter, sizeof(float));
    memcpy(data.block[BLOCK_COLOR_WEIGHT].data, block5, 16);

    TEST_ASSERT(!bambu_tag_is_valid(&data), "should reject tag with unknown filament type");
    return true;
}

static bool test_rejection_invalid_diameter(void) {
    MfClassicData data;
    memset(&data, 0, sizeof(data));
    data.type = MfClassicType1k;

    // Set up valid Block 1
    memcpy(data.block[BLOCK_MATERIAL_IDS].data, "A00-R3\x00\x00GFA00\x00", 14);

    // Set up valid Block 2
    memcpy(data.block[BLOCK_FILAMENT_TYPE].data, "PLA\x00", 4);

    // Set up valid Block 4
    memcpy(data.block[BLOCK_DETAILED_TYPE].data, "PLA Basic\x00", 10);

    // Set up Block 5 with INVALID diameter (0.5mm - too small)
    uint8_t block5[16] = {0};
    float diameter = 0.5f;
    memcpy(&block5[8], &diameter, sizeof(float));
    memcpy(data.block[BLOCK_COLOR_WEIGHT].data, block5, 16);

    TEST_ASSERT(!bambu_tag_is_valid(&data), "should reject tag with invalid diameter (0.5mm)");
    return true;
}

static bool test_rejection_non_printable_detailed_type(void) {
    MfClassicData data;
    memset(&data, 0, sizeof(data));
    data.type = MfClassicType1k;

    // Set up valid Block 1
    memcpy(data.block[BLOCK_MATERIAL_IDS].data, "A00-R3\x00\x00GFA00\x00", 14);

    // Set up valid Block 2
    memcpy(data.block[BLOCK_FILAMENT_TYPE].data, "PLA\x00", 4);

    // Set up Block 4 with NON-PRINTABLE characters (0x01, 0x02, etc.)
    uint8_t block4[16] = {0x01, 0x02, 0x03, 0x04, 0x00};
    memcpy(data.block[BLOCK_DETAILED_TYPE].data, block4, 16);

    // Set up valid Block 5 with valid diameter
    uint8_t block5[16] = {0};
    float diameter = 1.75f;
    memcpy(&block5[8], &diameter, sizeof(float));
    memcpy(data.block[BLOCK_COLOR_WEIGHT].data, block5, 16);

    TEST_ASSERT(!bambu_tag_is_valid(&data), "should reject tag with non-printable detailed type");
    return true;
}

static bool test_rejection_wrong_card_type(void) {
    MfClassicData data;
    memset(&data, 0, sizeof(data));
    data.type = MfClassicType4k;  // Wrong type - should be 1K

    // Set up valid Block 1
    memcpy(data.block[BLOCK_MATERIAL_IDS].data, "A00-R3\x00\x00GFA00\x00", 14);

    // Set up valid Block 2
    memcpy(data.block[BLOCK_FILAMENT_TYPE].data, "PLA\x00", 4);

    // Set up valid Block 4
    memcpy(data.block[BLOCK_DETAILED_TYPE].data, "PLA Basic\x00", 10);

    // Set up valid Block 5 with valid diameter
    uint8_t block5[16] = {0};
    float diameter = 1.75f;
    memcpy(&block5[8], &diameter, sizeof(float));
    memcpy(data.block[BLOCK_COLOR_WEIGHT].data, block5, 16);

    TEST_ASSERT(!bambu_tag_is_valid(&data), "should reject Mifare Classic 4K card");
    return true;
}

// Test helper functions from production code
static bool test_read_le16(void) {
    uint8_t data[] = {0xE8, 0x03};  // 1000 in little-endian
    TEST_ASSERT_EQ_INT(1000, bambu_read_le16(data), "bambu_read_le16");
    return true;
}

static bool test_read_le_float(void) {
    // 1.75 in IEEE 754 little-endian: 0x00 0x00 0xE0 0x3F
    uint8_t data[] = {0x00, 0x00, 0xE0, 0x3F};
    float result = bambu_read_le_float(data);
    TEST_ASSERT_EQ_FLOAT(1.75f, result, 0.001f, "bambu_read_le_float");
    return true;
}

static bool test_is_printable_ascii(void) {
    uint8_t printable[] = "PLA Basic\x00\x00\x00\x00\x00\x00";
    TEST_ASSERT(bambu_is_printable_ascii(printable, 16) == true, "should accept printable ASCII");

    uint8_t non_printable[] = {0x01, 0x02, 'A', 'B', 0x00};
    TEST_ASSERT(bambu_is_printable_ascii(non_printable, 5) == false, "should reject non-printable");

    uint8_t all_null[] = {0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT(bambu_is_printable_ascii(all_null, 4) == false, "should reject all-null (no printable)");

    return true;
}

static bool test_copy_ascii_string(void) {
    uint8_t src[] = "PLA Basic\x00\x00\x00\x00\x00\x00\x00";
    char dest[17] = {0};
    bambu_copy_ascii_string(dest, src, 16);
    TEST_ASSERT_EQ_STR("PLA Basic", dest, "bambu_copy_ascii_string");
    return true;
}

static bool test_filament_lookup(void) {
    const BambuFilamentInfo* info;

    // Test known variant
    info = bambu_lookup_filament("A00-R3");
    TEST_ASSERT(info != NULL, "should find A00-R3");
    TEST_ASSERT_EQ_STR("10204", info->filament_code, "filament_code for A00-R3");
    TEST_ASSERT_EQ_STR("Hot Pink", info->color_name, "color_name for A00-R3");

    // Test unknown variant
    info = bambu_lookup_filament("UNKNOWN");
    TEST_ASSERT(info == NULL, "should not find UNKNOWN variant");

    return true;
}

// ============================================================================
// Main test runner
// ============================================================================

static void run_test(const char* name, bool passed) {
    if (passed) {
        printf("  PASS: %s\n", name);
        tests_passed++;
    } else {
        tests_failed++;
    }
}

int main(int argc, char* argv[]) {
    const char* test_data_dir = "test/data";

    // Allow overriding test data directory
    if (argc > 1) {
        test_data_dir = argv[1];
    }

    printf("========================================\n");
    printf("Bambu Lab NFC Parser Tests\n");
    printf("Testing actual production code from:\n");
    printf("  - plugin/bambu_parser.h\n");
    printf("  - plugin/bambu_filaments.h\n");
    printf("========================================\n\n");

    // Helper function tests (from production code)
    printf("Helper Functions (from bambu_parser.h):\n");
    run_test("bambu_read_le16", test_read_le16());
    run_test("bambu_read_le_float", test_read_le_float());
    run_test("bambu_is_printable_ascii", test_is_printable_ascii());
    run_test("bambu_copy_ascii_string", test_copy_ascii_string());
    printf("\n");

    // Filament lookup tests (from production code)
    printf("Filament Lookup (from bambu_filaments.h):\n");
    run_test("bambu_lookup_filament", test_filament_lookup());
    printf("\n");

    // Rejection tests (testing production validation logic)
    printf("Rejection Tests (bambu_tag_is_valid):\n");
    run_test("reject_missing_gf_prefix", test_rejection_missing_gf_prefix());
    run_test("reject_unknown_filament_type", test_rejection_unknown_filament_type());
    run_test("reject_invalid_diameter", test_rejection_invalid_diameter());
    run_test("reject_non_printable_detailed_type", test_rejection_non_printable_detailed_type());
    run_test("reject_wrong_card_type", test_rejection_wrong_card_type());
    printf("\n");

    // File parsing tests (full integration with production code)
    printf("NFC File Parsing Tests:\n");
    printf("  Test data directory: %s\n", test_data_dir);
    for (size_t i = 0; i < NUM_EXPECTED_VALUES; i++) {
        char test_name[64];
        snprintf(test_name, sizeof(test_name), "parse_%s", expected_values[i].filename);
        run_test(test_name, test_parse_file(test_data_dir, &expected_values[i]));
    }
    printf("\n");

    // Summary
    printf("========================================\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("========================================\n");

    return tests_failed > 0 ? 1 : 0;
}

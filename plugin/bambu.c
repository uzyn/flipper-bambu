#include "nfc_supported_card_plugin.h"
#include <flipper_application/flipper_application.h>
#include <nfc/nfc_device.h>
#include <nfc/protocols/mf_classic/mf_classic.h>
#include <furi.h>
#include <string.h>

#include "bambu_filaments.h"
#include "bambu_parser.h"

#define TAG "Bambu"

// Main parse function: Extract and format all Bambu spool data
static bool bambu_parse(const NfcDevice* device, FuriString* parsed_data) {
    furi_assert(device);
    furi_assert(parsed_data);

    const MfClassicData* data = nfc_device_get_data(device, NfcProtocolMfClassic);

    // Quick type check
    if(data->type != MfClassicType1k) {
        return false;
    }

    // Verify this is a Bambu tag using our detection logic
    if(!bambu_tag_is_valid(data)) {
        return false;
    }

    // Parse Material ID and Variant ID from Block 1
    const uint8_t* block1 = data->block[BLOCK_MATERIAL_IDS].data;
    char material_id[7] = {0};  // "GFxxx" + null
    char variant_id[8] = {0};   // "xxx-Rx" + null
    bambu_copy_ascii_string(material_id, &block1[8], 6);
    bambu_copy_ascii_string(variant_id, &block1[0], 7);

    // Parse Filament Type from Block 2
    const uint8_t* block2 = data->block[BLOCK_FILAMENT_TYPE].data;
    char filament_type[17] = {0};
    bambu_copy_ascii_string(filament_type, block2, 16);

    // Parse Detailed Type from Block 4
    const uint8_t* block4 = data->block[BLOCK_DETAILED_TYPE].data;
    char detailed_type[17] = {0};
    bambu_copy_ascii_string(detailed_type, block4, 16);

    // Parse Color, Weight, Diameter from Block 5
    const uint8_t* block5 = data->block[BLOCK_COLOR_WEIGHT].data;
    uint8_t color_r = block5[0];
    uint8_t color_g = block5[1];
    uint8_t color_b = block5[2];
    uint8_t color_a = block5[3];
    uint16_t weight_grams = bambu_read_le16(&block5[4]);
    float diameter_mm = bambu_read_le_float(&block5[8]);

    // Parse Temperatures from Block 6
    const uint8_t* block6 = data->block[BLOCK_TEMPERATURES].data;
    uint16_t drying_temp = bambu_read_le16(&block6[0]);
    uint16_t drying_hours = bambu_read_le16(&block6[2]);
    uint16_t hotend_max = bambu_read_le16(&block6[8]);
    uint16_t hotend_min = bambu_read_le16(&block6[10]);

    // Parse Nozzle Diameter from Block 8 (float at bytes 12-15)
    const uint8_t* block8 = data->block[BLOCK_NOZZLE].data;
    float nozzle_diameter = bambu_read_le_float(&block8[12]);

    // Parse Spool Width from Block 10 (uint16 at bytes 4-5, value in mm*100)
    const uint8_t* block10 = data->block[BLOCK_SPOOL_WIDTH].data;
    uint16_t spool_width_raw = bambu_read_le16(&block10[4]);
    float spool_width_mm = (float)spool_width_raw / 100.0f;

    // Parse Production Date from Block 12 (ASCII YYYY_MM_DD_HH_MM)
    const uint8_t* block12 = data->block[BLOCK_PRODUCTION_DATE].data;
    char production_date[17] = {0};
    bambu_copy_ascii_string(production_date, block12, 16);

    // Format production date from "YYYY_MM_DD_HH_MM" to "YYYY-MM-DD HH:MM"
    // Only format if underscores are present at expected positions
    if(production_date[4] == '_' &&
       production_date[7] == '_' &&
       production_date[10] == '_' &&
       production_date[13] == '_') {
        production_date[4] = '-';
        production_date[7] = '-';
        production_date[10] = ' ';
        production_date[13] = ':';
    }

    // Parse Filament Length from Block 14 (uint16 at bytes 4-5, meters)
    const uint8_t* block14 = data->block[BLOCK_FILAMENT_LENGTH].data;
    uint16_t filament_length = bambu_read_le16(&block14[4]);

    // Look up filament info from variant_id
    const BambuFilamentInfo* filament_info = bambu_lookup_filament(variant_id);

    // Build formatted output
    furi_string_cat_printf(parsed_data, "\e#Bambu Lab Filament\n");
    // furi_string_cat_printf(parsed_data, "Type: %s\n", filament_type);
    furi_string_cat_printf(parsed_data, "Type: %s\n", detailed_type);

    // Display color: show name with hex if available, otherwise just hex
    // For hex code: show 6-digit if fully opaque, otherwise show "#RRGGBB @ XX%"
    if(filament_info != NULL) {
        if(color_a == 0xFF) {
            furi_string_cat_printf(parsed_data, "Color: %s (#%02X%02X%02X)\n",
                                  filament_info->color_name, color_r, color_g, color_b);
        } else {
            uint8_t alpha_percent = (color_a * 100) / 255;
            furi_string_cat_printf(parsed_data, "Color: %s (#%02X%02X%02X @ %u%%)\n",
                                  filament_info->color_name, color_r, color_g, color_b, alpha_percent);
        }
    } else {
        if(color_a == 0xFF) {
            furi_string_cat_printf(parsed_data, "Color: #%02X%02X%02X\n",
                                  color_r, color_g, color_b);
        } else {
            uint8_t alpha_percent = (color_a * 100) / 255;
            furi_string_cat_printf(parsed_data, "Color: #%02X%02X%02X @ %u%%\n",
                                  color_r, color_g, color_b, alpha_percent);
        }
    }

    if(filament_info != NULL) {
        furi_string_cat_printf(parsed_data, "Filament Code: %s\n", filament_info->filament_code);
    } else {
        furi_string_cat_printf(parsed_data, "Material ID: %s\n", material_id);
    }

    furi_string_cat_printf(parsed_data, "Prod: %s\n", production_date);


    furi_string_cat_printf(parsed_data, "\n\e#Configurations\n");
    furi_string_cat_printf(parsed_data, "Hotend: %u-%u C\n", hotend_min, hotend_max);
    furi_string_cat_printf(parsed_data, "Drying: %u C for %uh\n", drying_temp, drying_hours);
    furi_string_cat_printf(parsed_data, "Nozzle: >= %.2fmm\n", (double)nozzle_diameter);

    furi_string_cat_printf(parsed_data, "\n\e#Specifications\n");
    furi_string_cat_printf(parsed_data, "Weight: %ug\n", weight_grams);
    furi_string_cat_printf(parsed_data, "Diameter: %.2fmm\n", (double)diameter_mm);
    furi_string_cat_printf(parsed_data, "Spool Width: %.2fmm\n", (double)spool_width_mm);
    if(filament_length > 0) {
        furi_string_cat_printf(parsed_data, "Length: %um\n", filament_length);
    }

    return true;
}

static const NfcSupportedCardsPlugin bambu_plugin = {
    .protocol = NfcProtocolMfClassic,
    .verify = NULL,  // No early verify - validation done in parse()
    .read = NULL,    // No custom read - uses default MfClassic reader
    .parse = bambu_parse,
};

static const FlipperAppPluginDescriptor bambu_plugin_descriptor = {
    .appid = NFC_SUPPORTED_CARD_PLUGIN_APP_ID,
    .ep_api_version = NFC_SUPPORTED_CARD_PLUGIN_API_VERSION,
    .entry_point = &bambu_plugin,
};

const FlipperAppPluginDescriptor* bambu_plugin_ep(void) {
    return &bambu_plugin_descriptor;
}

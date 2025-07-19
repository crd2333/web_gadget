import re
import json
import os

def parse_definitions_to_json(nbt_cpp_path, blockinfo_h_path, blockinfo_cpp_path, tiles_h_path, json_output_path):
    """
    Parses block definitions from Mineways source files and creates a comprehensive
    JSON mapping from Minecraft IDs to block properties, including aliases.
    """
    # --- Preliminary Step: Count total definitions in blockInfo.cpp for verification ---
    try:
        with open(blockinfo_cpp_path, 'r', encoding='utf-8') as f:
            blockinfo_content = f.read()
        total_defs = len(re.findall(r'/\*\s*\d+\s*\*/', blockinfo_content))
        print(f"Verification: Found {total_defs} '/* id */' comments in blockInfo.cpp.")
    except Exception as e:
        print(f"Could not perform verification count on blockInfo.cpp: {e}")


    # --- Step 1: Parse BLOCK_ constants from blockInfo.h ---
    block_constants = {}
    try:
        with open(blockinfo_h_path, 'r', encoding='utf-8') as f:
            blockinfo_h_content = f.read()

        enum_match = re.search(r'enum\s+block_types\s*\{([\s\S]*?)\};', blockinfo_h_content)
        if enum_match:
            enum_content = enum_match.group(1)
            for line in enum_content.split(','):
                match = re.search(r'(\w+)\s*=\s*(\d+)', line)
                if match:
                    block_constants[match.group(1)] = int(match.group(2))
        print(f"Found {len(block_constants)} BLOCK_ constants in blockInfo.h.")
    except Exception as e:
        print(f"Could not parse blockInfo.h: {e}")


    # --- Step 2: Parse gBlockDefinitions from blockInfo.cpp ---
    block_details_by_id = {}
    block_defs_match = re.search(r'BlockDefinition\s+gBlockDefinitions\[.*?\]\s*=\s*\{([\s\S]*?)\};', blockinfo_content)
    if not block_defs_match:
        print("Error: Could not find gBlockDefinitions array.")
        return

    block_defs_content = block_defs_match.group(1)

    entry_regex_blockinfo = re.compile(
        r'\{\s*/\*\s*(\d+)\s*\*/[\s\S]*?,'      # 1: ID
        r'\s*(0x[0-9a-fA-F]+)\s*,'              # 2: read_color
        r'\s*([0-9.]+)f\s*,'                    # 3: read_alpha
        r'[\s\S]*?,\s*'                        # Skip intermediate fields
        r'((?:BLF_\w+\s*(?:\|)?\s*)+)\s*\}',    # 4: Capture all BLF flags
        re.MULTILINE
    )

    matches_in_blockinfo = entry_regex_blockinfo.findall(block_defs_content)
    print(f"Successfully parsed {len(matches_in_blockinfo)} records from blockInfo.cpp.")

    for match in matches_in_blockinfo:
        block_id_str, read_color_hex, read_alpha_str, flags_str = match
        block_id = int(block_id_str)
        is_whole = 'BLF_WHOLE' in flags_str
        color_hex_str = f"#{int(read_color_hex, 16):06x}"
        block_details_by_id[block_id] = {
            "color": color_hex_str, "alpha": float(read_alpha_str), "is_full_block": is_whole
        }

    final_block_map = {}

    # --- Step 3: Parse BlockTranslations from nbt.cpp ---
    try:
        with open(nbt_cpp_path, 'r', encoding='utf-8') as f:
            nbt_content = f.read()
    except FileNotFoundError:
        print(f"Error: File not found - {nbt_cpp_path}")
        return

    translations_match = re.search(r'BlockTranslator\s+BlockTranslations\[.*?\]\s*=\s*\{([\s\S]*?)\};', nbt_content)
    if not translations_match:
        print("Error: Could not find BlockTranslations array.")
        return

    translations_content = translations_match.group(1)

    entry_regex_nbt = re.compile(
        r'\{\s*\d+,\s*(\d+|BLOCK_\w+)\s*,' # 1: blockId (numeric or constant)
        r'[^,]+,\s*"(.*?)"\s*,'             # 2: official name
    )

    matches_in_nbt = entry_regex_nbt.findall(translations_content)
    print(f"Found {len(matches_in_nbt)} translation records in nbt.cpp.")

    for block_id_val, official_name in matches_in_nbt:
        block_id = -1
        if block_id_val.isdigit():
            block_id = int(block_id_val)
        elif block_id_val in block_constants:
            block_id = block_constants[block_id_val]

        if block_id != -1:
            official_name = official_name.strip()
            namespaced_id = f"minecraft:{official_name}"
            block_details = block_details_by_id.get(block_id)
            if block_details and namespaced_id not in final_block_map:
                 final_block_map[namespaced_id] = block_details

    # --- Step 4: Parse gTilesTable and gTilesAlternates from tiles.h ---
    try:
        with open(tiles_h_path, 'r', encoding='utf-8') as f:
            tiles_h_content = f.read()
    except FileNotFoundError:
        print(f"Error: File not found - {tiles_h_path}")
        return

    # Parse gTilesTable
    tile_name_to_id = {}
    tiles_match = re.search(r'gTilesTable\[.*?\]\s*=\s*\{([\s\S]*?)\};', tiles_h_content)
    if not tiles_match:
        print("Error: Could not find gTilesTable array.")
    else:
        tiles_content = tiles_match.group(1)
        entry_regex_tiles = re.compile(
            r'\{\s*\d+\s*,\s*\d+\s*,\s*(\d+)\s*,\s*[^,]+?\s*,\s*L?"(.*?)"\s*,\s*L?"(.*?)"\s*,[\s\S]*?\}'
        )
        matches_in_tiles = entry_regex_tiles.findall(tiles_content)
        print(f"Found {len(matches_in_tiles)} tile records in tiles.h.")
        for block_id_str, filename, alt_filename in matches_in_tiles:
            block_id = int(block_id_str)
            if filename:
                tile_name_to_id[filename.strip()] = block_id
            if alt_filename:
                tile_name_to_id[alt_filename.strip()] = block_id

    # Parse gTilesAlternates
    alternates_match = re.search(r'gTilesAlternates\[.*?\]\s*=\s*\{([\s\S]*?)\};', tiles_h_content)
    if not alternates_match:
        print("Warning: Could not find gTilesAlternates array.")
    else:
        alternates_content = alternates_match.group(1)
        entry_regex_alternates = re.compile(r'\{\s*L?"(.*?)"\s*,\s*L?"(.*?)"\s*\}')
        matches_in_alternates = entry_regex_alternates.findall(alternates_content)
        print(f"Found {len(matches_in_alternates)} alternate tile name records in tiles.h.")
        for alt_name, standard_name in matches_in_alternates:
            alt_name = alt_name.strip()
            standard_name = standard_name.strip()
            if standard_name in tile_name_to_id:
                tile_name_to_id[alt_name] = tile_name_to_id[standard_name]

    # Add tile names and aliases to the final map, filtering out MW_ prefixes ---
    for name, block_id in tile_name_to_id.items():
        # Filter out internal Mineways-specific names
        if name.startswith('MW_') or name.startswith('MWO_'):
            continue

        block_details = block_details_by_id.get(block_id)
        if not block_details:
            continue

        # Add with minecraft: prefix and without
        namespaced_id = f"minecraft:{name}"
        if namespaced_id not in final_block_map:
            final_block_map[namespaced_id] = block_details
        if name not in final_block_map:
             final_block_map[name] = block_details


    # --- Step 6: Save the final mapping to JSON ---
    try:
        with open(json_output_path, 'w', encoding='utf-8') as f:
            json.dump(final_block_map, f, indent=4, sort_keys=True)
        print(f"Successfully created '{json_output_path}' with {len(final_block_map)} final unique block mappings.")
    except Exception as e:
        print(f"An error occurred while writing the JSON file: {e}")

if __name__ == '__main__':
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)

    nbt_path = os.path.join(project_root, 'src_code_in_mineways', 'nbt.cpp')
    blockinfo_h_path = os.path.join(project_root, 'src_code_in_mineways', 'blockInfo.h')
    blockinfo_cpp_path = os.path.join(project_root, 'src_code_in_mineways', 'blockInfo.cpp')
    tiles_h_path = os.path.join(project_root, 'src_code_in_mineways', 'tiles.h')
    json_output = os.path.join(script_dir, 'block_mappings.json')

    parse_definitions_to_json(nbt_path, blockinfo_h_path, blockinfo_cpp_path, tiles_h_path, json_output)

/*
Copyright (c) 2010, Sean Kasun
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation
and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
THE POSSIBILITY OF SUCH DAMAGE.
*/


#include "stdafx.h"
#include <string.h>
#include <assert.h>

// We know we won't run into names longer than 100 characters. The old code was
// safe, but was also allocating strings all the time - seems slow.
#define MAX_NAME_LENGTH 100

// return a negative number, giving the line of the code where it returned
#define LINE_ERROR (-(__LINE__))

static int skipType(bfFile* pbf, int type);
static int skipList(bfFile* pbf);
static int skipCompound(bfFile* pbf);

static int readBiomePalette(bfFile* pbf, unsigned char* paletteBiomeEntry, int& entryIndex);
static int readPalette(int& returnCode, bfFile* pbf, int mcVersion, unsigned char* paletteBlockEntry, unsigned char* paletteDataEntry, int& entryIndex, char* unknownBlock, int unknownBlockID);
static int readBlockData(bfFile* pbf, int& bigbufflen, unsigned char* bigbuff);

typedef struct BlockTranslator {
    int hashSum;
    unsigned char blockId;
    unsigned char dataVal;
    char* name;
    unsigned long translateFlags;
} BlockTranslator;

typedef struct BiomeTranslator {
    int hashSum;
    unsigned char biomeID;
    char* name;
} BiomeTranslator;

// our bit shift code reader can read only up to 2^9 entries right now. TODO
#define MAX_PALETTE	512

static bool makeHash = true;
static bool makeBiomeHash = true;
static TranslationTuple* modTranslations = NULL;

// if defined, only those data values that have an effect on graphics display (vs. sound or
// simulation) are actually filled in. This is a good thing for instancing, but allows the
// code to be turned on in the future (though there are still a bunch of non-graphical values
// that we nonetheless don't track).
#define GRAPHICAL_ONLY

// Shows all data names and values and what uses them: https://minecraft.wiki/w/Java_Edition_data_values

// properties and which blocks use them:
// age: AGE_PROP
// attached: TRIPWIRE_PROP, TRIPWIRE_HOOK_PROP
// axis: AXIS_PROP, QUARTZ_PILLAR_PROP
// bites: CANDLE_CAKE_PROP
// bottom: true|false - scaffolding, for which we ignore the "distance" field
// conditional: COMMAND_BLOCK_PROP
// delay: REPEATER_PROP
// disarmed: TRIPWIRE_PROP
// distance: LEAF_PROP
// down|up: MUSHROOM_PROP, MUSHROOM_STEM_PROP
// east|north|west|south: MUSHROOM_PROP, MUSHROOM_STEM_PROP, WIRE_PROP
// eggs: EGG_PROP
// enabled: HOPPER_PROP
// extended: PISTON_PROP
// face: LEVER_PROP, BUTTON_PROP
// facing: DOOR_PROP, TORCH_PROP, STAIRS_PROP, LEVER_PROP, CHEST_PROP, FURNACE_PROP, FACING_PROP, BUTTON_PROP, SWNE_FACING_PROP, 
//     BED_PROP, DROPPER_PROP, TRAPDOOR_PROP, PISTON_PROP, PISTON_HEAD_PROP, COMMAND_BLOCK_PROP, HOPPER_PROP, OBSERVER_PROP,
//     REPEATER_PROP, COMPARATOR_PROP, HEAD_WALL_PROP
// falling: FLUID_PROP
// half: DOOR_PROP, TALL_FLOWER_PROP, STAIRS_PROP, TRAPDOOR_PROP
// hanging: LANTERN_PROP (really just sets dataVal directly to 0x1 if true)
// has_book: LECTERN_PROP
// hinge: DOOR_PROP
// in_wall: fence gate
// inverted: DAYLIGHT_PROP
// layers: SNOW_PROP
// leaves: LEAF_SIZE_PROP
// level: FLUID_PROP
// lit: TORCH_PROP (redstone only), FURNACE_PROP, REDSTONE_ORE_PROP
// locked: REPEATER_PROP
// mode: STRUCTURE_PROP, COMPARATOR_PROP
// moisture: FARMLAND_PROP
// north|east|south|west[|up|down]: VINE_PROP, TRIPWIRE_PROP
// occupied: BED_PROP
// open: DOOR_PROP, TRAPDOOR_PROP
// part: BED_PROP
// persistent: LEAF_PROP
// pickles: PICKLE_PROP
// power: WIRE_PROP, WT_PRESSURE_PROP, DAYLIGHT_PROP
// powered: DOOR_PROP, LEVER_PROP, RAIL_PROP, PRESSURE_PROP, BUTTON_PROP, TRAPDOOR_PROP, TRIPWIRE_PROP, TRIPWIRE_HOOK_PROP, REPEATER_PROP, COMPARATOR_PROP
// rotation: STANDING_SIGN_PROP, HEAD_PROP
// shape: STAIRS_PROP, RAIL_PROP
// short: PISTON_HEAD_PROP
// signal_fire: CAMPFIRE_PROP
// snowy: SNOWY_PROP
// stage: SAPLING_PROP
// triggered: DROPPER_PROP
// type: SLAB_PROP, CHEST_PROP, PISTON_HEAD_PROP
// waterlogged: so common that we always add it in and use the dedicated bit 0x10

// various properties of palette entries, can be used to differentiate how to use properties
#define NO_PROP				  0
// axis: x|y|z
#define AXIS_PROP			  1
// snowy: false|true
// note that the snowy prop actually does nothing; put here mostly to keep track; could be deleted.
#define SNOWY_PROP			  2
// facing: north|south|east|west
// half: upper|lower
// hinge: left|right
// open: true|false
// powered: true|false
#define DOOR_PROP			  3
// for water and lava
// See https://minecraft.wiki/w/Water#Block_states
// level: 1-7|8 when falling true - note that level 0 is the "source block" and higher means further away
#define FLUID_PROP			  4
// saplings
// stage: 0|1 - non-graphical, so ignored
#define SAPLING_PROP		  5
// persistent: true|false - non-graphical, so ignored
// distance: 1-7 - non-graphical, so ignored
#define LEAF_PROP			  6
// for sunflowers, etc.
// half: upper|lower
#define TALL_FLOWER_PROP	  7
// down|up|east|north|west|south: true|false
#define MUSHROOM_PROP		  8
#define MUSHROOM_STEM_PROP	  9
// type: top|bottom|double
// waterlogged: true|false
#define SLAB_PROP			 10
// facing: north|east|south|west
// lit: true|false - for redstone 
#define TORCH_PROP			 11
// facing: north|east|south|west
// half: top|bottom - note this is different from upper|lower for doors and sunflowers
// shape: straight|inner_left|inner_right|outer_left|outer_right - we ignore, deriving from neighbors
#define STAIRS_PROP			 12
// Redstone wire
// north|south|east|west: none|side|up https://minecraft.wiki/w/Redstone#Block_state - we ignore, it's from geometry
// power: 0-15
#define WIRE_PROP		     13
// Lever
// face: floor|ceiling|wall
// facing: north|west|south|east
// powered: true|false
#define LEVER_PROP			 14
// facing: north|west|south|east
// type: single|left|right
#define CHEST_PROP			 15
// wheat, frosted ice, cactus, sugar cane, etc.
// leaves: leaf size prop (for bamboo) - none/small/large
#define LEAF_SIZE_PROP		 16
// moisture: 0-7
#define FARMLAND_PROP		 17
// facing: north|west|south|east - like torches
// lit: true|false 
#define FURNACE_PROP		 18
// used for ladders and wall signs
// facing: north|west|south|east - like torches
#define FACING_PROP			 19
// rotation: 0-15
#define STANDING_SIGN_PROP	 20
// shape: north_south|east_west|north_east|north_west|south_east|south_west|ascending_north|ascending_south|ascending_east|ascending_west
// powered: true|false - all but rail itself
#define RAIL_PROP			 21
// powered: true|false
// may use higher bits for types of pressure plates? - TODO
#define PRESSURE_PROP		 22
// power: 0-15 - really, same as WIRE_PROP, but just in case
#define WT_PRESSURE_PROP	 23
// lit: true|false 
#define REDSTONE_ORE_PROP	 24
// face: floor|ceiling|wall
// facing: north|west|south|east
// powered: true|false
#define BUTTON_PROP			 25
// layers: 1-8
// no post process actually done, as dataVal is set directly
#define SNOW_PROP			 26
// facing: north|south|east|west
// open: true|false
// in_wall: true|false - If true, the gate is lowered by three pixels, to accommodate attaching more cleanly with normal and mossy Cobblestone Walls
// powered: true|false - ignored
#define FENCE_GATE_PROP		 27
// facing: south|west|north|east
#define SWNE_FACING_PROP	 28
// bites: 0-6; 7 means it's actually a normal candle
// if BIT_16 is flagged, it means there's a candle on top and that 0xf is the color
// lit: true or false, goes to BIT_32
#define CANDLE_CAKE_PROP	 29
// facing: south|west|north|east
// occupied: true|false - seems to be occupied only if someone else is occupying it? non-graphical, so ignored
// part: head|foot
#define BED_PROP			 30

#define EXTENDED_FACING_PROP 31
// facing: down|up|north|south|west|east
// triggered: true|false - ignore, non-graphical
#define DROPPER_PROP		 EXTENDED_FACING_PROP
// facing: down|up|north|south|west|east
// half: top|bottom
// open: true|false
// powered: true|false
// waterlogged: true|false
#define TRAPDOOR_PROP		 32
// facing: north|east|south|west
#define ANVIL_PROP			 33
// facing: down|up|north|south|west|east
// extended: true|false
#define PISTON_PROP			 EXTENDED_FACING_PROP
// facing: down|up|north|south|west|east
// extended: true|false - ignored, don't know what that is (block wouldn't exist otherwise, right?
// type: sticky|normal
// short: true|false - TODO, piston arm is shorter by 4 pixels, https://minecraft.wiki/w/Piston#Block_state_2 - not sure how to generate this state, so leaving it alone
#define PISTON_HEAD_PROP	 EXTENDED_FACING_PROP
// south|west|north|east: true|false
#define FENCE_PROP	 34
// facing: south|west|north|east
#define END_PORTAL_PROP		 35
// age: 0-3
// facing: north|east|south|west
#define COCOA_PROP			 36
// south|west|north|east|up: true|false - ignored, from context
// attached: true|false
// powered: true|false
// disarmed: true|false
#define TRIPWIRE_PROP		 37
// facing: south|west|north|east
// attached: true|false
// powered: true|false
#define TRIPWIRE_HOOK_PROP	 38
// facing: down|up|north|south|west|east
// conditional: true|false
#define COMMAND_BLOCK_PROP	 EXTENDED_FACING_PROP
// inverted: true|false
// power: 0-15
#define DAYLIGHT_PROP		 39
// enabled: true|false
// facing: down|north|south|west|east
#define HOPPER_PROP			 EXTENDED_FACING_PROP
// axis: x|y|z
#define QUARTZ_PILLAR_PROP	 40
// facing: down|up|south|north|east|west
// powered: true|false
#define OBSERVER_PROP		 EXTENDED_FACING_PROP
// mode: data|save|load|corner
// no post process actually done, as dataVal is set directly
#define STRUCTURE_PROP		 41
// delay: 1-4
// facing: north|east|south|west
// locked: true|false - ignored
// powered: true|false - ignored
#define REPEATER_PROP		 42
// facing: north|east|south|west
// powered: true|false
// delayed: 0-3
// locked: true|false
#define COMPARATOR_PROP		 43
// rotation: 0-15
#define HEAD_PROP			 44
// facing: north|south|east|west
#define HEAD_WALL_PROP		 45
// clear out dataVal, which might get "age" etc. dataVals before saving
#define TRULY_NO_PROP		 46
// waterlogged: true|false, 1 HIGH bit
// facing: south|north|west|east - 2 HIGH bits
#define FAN_PROP			 47
// pickles: 1-4
// waterlogged: true|false
#define PICKLE_PROP			 48
// eggs: 1-4
// hatch: 0-3?
#define EGG_PROP			 49
// for wall signs - basically a dropper, without the other stuff such as up, etc.
#define WALL_SIGN_PROP		 EXTENDED_FACING_PROP
// facing: down|up|north|south|west|east
// open: true|false
#define BARREL_PROP			 EXTENDED_FACING_PROP
// facing: north|south|west|east - done as 0,1,2,3 SWNE
#define EXTENDED_SWNE_FACING_PROP 50
// facing: north|south|west|east - done as 0,1,2,3 SWNE
// face: floor|ceiling|wall
#define GRINDSTONE_PROP		EXTENDED_SWNE_FACING_PROP
// facing: north|south|west|east - done as 0,1,2,3 SWNE
// has_book: true|false
// powered: true|false
#define LECTERN_PROP		EXTENDED_SWNE_FACING_PROP
// facing: north|south|west|east - done as 0,1,2,3 SWNE
// powered: true|false
// attachment: floor|ceiling|single_wall|double_wall
#define BELL_PROP		EXTENDED_SWNE_FACING_PROP
// hanging: true|false
#define LANTERN_PROP		51
// facing: north|south|west|east - done as 0,1,2,3 SWNE
// lit: true|false
// signal_fire: true|false
#define CAMPFIRE_PROP		EXTENDED_SWNE_FACING_PROP
// UGH, not implemented - too many bits! Need data values to be 16 bits to consider it. TODO
// east, north, south, west: low|none|tall
// up: false|true
// waterlogged
// which adds up to 9 bits, which is too many
#define WALL_PROP           TRULY_NO_PROP
// axis: 1 EW, 2 NS
#define NETHER_PORTAL_AXIS_PROP	52
// pumpkin and melon stems
// age:0-7
// facing: north|south|west|east - done as BIT_32|BIT_16 0,1,2,3 ESWN
#define HIGH_FACING_PROP    53
// candles: 1-4 maps to 0x00-0x30
// lit: adds one to the type
// waterlogged: the usual bit
#define CANDLE_PROP         54
// bottom two bits is sub-type.
// facing: 0-6 << 2 dropper_facing
#define AMETHYST_PROP       55
// thickness: 5 states
// vertical_direction: up/down
#define DRIPSTONE_PROP      56
// bottommost is stem
// facing: 0-3 door_facing << 1
// tilt: none/partial/unstable/full 0xc0 fields (0x0,0x4,0x8,0xC) << 1
#define BIG_DRIPLEAF_PROP   57
// facing: 0-3 door_facing
// half: lower/upper 0x0/0x4
#define SMALL_DRIPLEAF_PROP 58
// berries: 0x2 if berries
#define BERRIES_PROP        59
// age: 0-7 or 0-15, (0-3 for frosted ice)
// property is treated as NO_PROP; if something has just an age, it simply gets the value in dataVal - search on "age" (with quotes) to see code
#define AGE_PROP			60
// age and stage will eventually be ignored, hanging will not
#define PROPAGULE_PROP      61
// facing: SWNE 0x3
// sculk_sensor_phase: 0x10 (16 bit)
#define CALIBRATED_SCULK_SENSOR_PROP    62
// facing: SWNE 0x3
// flower_amount: 0xc 1-4
// or
// segment_amount: 1 / 2 / 3 / 4
#define PINK_PETALS_PROP    63
// age: 0-4
// half: upper/lower
#define PITCHER_CROP_PROP   64
// rotation: 0-15
// attached: true/false
#define ATTACHED_HANGING_SIGN   65
// orientation 0-11
// crafting 0-1 (0x10)
// triggered 0-1 (0x20)
#define CRAFTER_PROP        66
// lit true|false gives 0x8 for copper bulbs
#define BULB_PROP           67
// axis, like logs
// old: active true|false --> 0 or 2
// creaking_heart_state: uprooted 0, dormant 1, awake 2
// natural - ignored, not visual
#define CREAKING_HEART_PROP 68
// north/south/east/west - none, low, tall; only tall gets a 0x1 in the four 0x1e bits
// ignore bottom
#define PALE_MOSS_CARPET_PROP   69
// south|west|north|east|down|up: true|false
#define VINE_PROP   70
// facing, hydration 0-3 shifted up 2, waterlogged
#define GHAST_PROP  71

#define NUM_TRANS 1114

BlockTranslator BlockTranslations[NUM_TRANS] = {
    //hash ID data name flags
    // hash is computed once when 1.13 data is first read in.
    // second column is "traditional" type value, as found in blockInfo.cpp; third column is high-order bit and data value, fourth is Minecraft name
    // Note: the HIGH_BIT gets "transferred" to the type in MinewaysMap's IDBlock() method, about 100 lines in.
    // The list of names and data values: https://minecraft.wiki/w/Java_Edition_data_values
    // and older https://minecraft.wiki/w/Java_Edition_data_values/Pre-flattening#Block_IDs
    //hash,ID,BIT|dataval,  name, common properties flags
    { 0,   0,           0, "air", NO_PROP },
    { 0,   0,           0, "empty", NO_PROP },  // not sure this is necessary, but it is mentioned in https://minecraft.wiki/w/Java_Edition_data_values
    { 0, 166,           0, "barrier", NO_PROP },
    { 0,   1,           0, "stone", NO_PROP },
    { 0,   1,           1, "granite", NO_PROP },
    { 0,   1,           2, "polished_granite", NO_PROP },
    { 0,   1,           3, "diorite", NO_PROP },
    { 0,   1,           4, "polished_diorite", NO_PROP },
    { 0,   1,           5, "andesite", NO_PROP },
    { 0,   1,           6, "polished_andesite", NO_PROP },
    { 0, 170,           0, "hay_block", AXIS_PROP },
    { 0,   2,           0, "grass_block", SNOWY_PROP },
    { 0,   3,           0, "dirt", NO_PROP }, // no SNOWY_PROP
    { 0,   3,           1, "coarse_dirt", NO_PROP }, // note no SNOWY_PROP
    { 0,   3,           2, "podzol", SNOWY_PROP },
    { 0,   4,           0, "cobblestone", NO_PROP },
    { 0,   5,           0, "oak_planks", NO_PROP },
    { 0,   5,           1, "spruce_planks", NO_PROP },
    { 0,   5,           2, "birch_planks", NO_PROP },
    { 0,   5,           3, "jungle_planks", NO_PROP },
    { 0,   5,           4, "acacia_planks", NO_PROP },
    { 0,   5,           5, "dark_oak_planks", NO_PROP },
    { 0,   6,           0, "oak_sapling", SAPLING_PROP },
    { 0,   6,           1, "spruce_sapling", SAPLING_PROP },
    { 0,   6,           2, "birch_sapling", SAPLING_PROP },
    { 0,   6,           3, "jungle_sapling", SAPLING_PROP },
    { 0,   6,           4, "acacia_sapling", SAPLING_PROP },
    { 0,   6,           5, "dark_oak_sapling", SAPLING_PROP },
    { 0,  64,           0, "oak_door", DOOR_PROP },
    { 0, 193,           0, "spruce_door", DOOR_PROP },
    { 0, 194,           0, "birch_door", DOOR_PROP },
    { 0, 195,           0, "jungle_door", DOOR_PROP },
    { 0, 196,           0, "acacia_door", DOOR_PROP },
    { 0, 197,           0, "dark_oak_door", DOOR_PROP },
    { 0,   7,           0, "bedrock", NO_PROP },
    { 0,   9,           0, "water", FLUID_PROP },   // FLUID_PROP
    { 0,   9,           0, "flowing_water", FLUID_PROP },   // FLUID_PROP
    { 0,  11,           0, "lava", FLUID_PROP },   // FLUID_PROP
    { 0,  11,           0, "flowing_lava", FLUID_PROP },   // FLUID_PROP
    { 0,  12,           0, "sand", NO_PROP },
    { 0,  12,           1, "red_sand", NO_PROP },
    { 0,  24,           0, "sandstone", NO_PROP }, // TODO 1.13 check: For normal sandstone the bottom has a cracked pattern. The other types of sandstone have bottom faces same as the tops.
    { 0,  24,           1, "chiseled_sandstone", NO_PROP },
    { 0,  24,           2, "cut_sandstone", NO_PROP }, // aka smooth sandstone
    { 0, 179,           0, "red_sandstone", NO_PROP },
    { 0, 179,           1, "chiseled_red_sandstone", NO_PROP },
    { 0, 179,           2, "cut_red_sandstone", NO_PROP }, // aka smooth red sandstone
    { 0,  13,           0, "gravel", NO_PROP },
    { 0,  14,           0, "gold_ore", NO_PROP },
    { 0,  15,           0, "iron_ore", NO_PROP },
    { 0,  16,           0, "coal_ore", NO_PROP },
    { 0,  17,  BIT_16 | 0, "oak_wood", AXIS_PROP },	// same as logs below, but with a high bit set to mean that it's "wood" texture on the endcaps. 
    { 0,  17,  BIT_16 | 1, "spruce_wood", AXIS_PROP },
    { 0,  17,  BIT_16 | 2, "birch_wood", AXIS_PROP },
    { 0,  17,  BIT_16 | 3, "jungle_wood", AXIS_PROP },
    { 0, 162,  BIT_16 | 0, "acacia_wood", AXIS_PROP },
    { 0, 162,  BIT_16 | 1, "dark_oak_wood", AXIS_PROP },
    { 0,  17,           0, "oak_log", AXIS_PROP },
    { 0,  17,           1, "spruce_log", AXIS_PROP },
    { 0,  17,           2, "birch_log", AXIS_PROP },
    { 0,  17,           3, "jungle_log", AXIS_PROP },
    { 0, 162,           0, "acacia_log", AXIS_PROP },
    { 0, 162,           1, "dark_oak_log", AXIS_PROP },
    { 0,  18,           0, "oak_leaves", LEAF_PROP },
    { 0,  18,           1, "spruce_leaves", LEAF_PROP },
    { 0,  18,           2, "birch_leaves", LEAF_PROP },
    { 0,  18,           3, "jungle_leaves", LEAF_PROP },
    { 0, 161,           0, "acacia_leaves", LEAF_PROP },
    { 0, 161,           1, "dark_oak_leaves", LEAF_PROP },
    { 0,  32,           0, "dead_bush", NO_PROP },
    { 0,  31,           1, "grass", NO_PROP },
    { 0,  31,           1, "short_grass", NO_PROP }, // name change in 1.20.3, https://minecraft.wiki/w/Java_Edition_1.20.3
    { 0,  31,           2, "fern", NO_PROP },
    { 0,  19,           0, "sponge", NO_PROP },
    { 0,  19,           1, "wet_sponge", NO_PROP },
    { 0,  20,           0, "glass", NO_PROP },
    { 0,  95,           0, "white_stained_glass", NO_PROP },
    { 0,  95,           1, "orange_stained_glass", NO_PROP },
    { 0,  95,           2, "magenta_stained_glass", NO_PROP },
    { 0,  95,           3, "light_blue_stained_glass", NO_PROP },
    { 0,  95,           4, "yellow_stained_glass", NO_PROP },
    { 0,  95,           5, "lime_stained_glass", NO_PROP },
    { 0,  95,           6, "pink_stained_glass", NO_PROP },
    { 0,  95,           7, "gray_stained_glass", NO_PROP },
    { 0,  95,           8, "light_gray_stained_glass", NO_PROP },
    { 0,  95,           9, "cyan_stained_glass", NO_PROP },
    { 0,  95,          10, "purple_stained_glass", NO_PROP },
    { 0,  95,          11, "blue_stained_glass", NO_PROP },
    { 0,  95,          12, "brown_stained_glass", NO_PROP },
    { 0,  95,          13, "green_stained_glass", NO_PROP },
    { 0,  95,          14, "red_stained_glass", NO_PROP },
    { 0,  95,          15, "black_stained_glass", NO_PROP },
    { 0, 160,           0, "white_stained_glass_pane", NO_PROP },   // sadly, these all share a type so there are not 4 bits for directions, especially since it may waterlog
    { 0, 160,           1, "orange_stained_glass_pane", NO_PROP },
    { 0, 160,           2, "magenta_stained_glass_pane", NO_PROP },
    { 0, 160,           3, "light_blue_stained_glass_pane", NO_PROP },
    { 0, 160,           4, "yellow_stained_glass_pane", NO_PROP },
    { 0, 160,           5, "lime_stained_glass_pane", NO_PROP },
    { 0, 160,           6, "pink_stained_glass_pane", NO_PROP },
    { 0, 160,           7, "gray_stained_glass_pane", NO_PROP },
    { 0, 160,           8, "light_gray_stained_glass_pane", NO_PROP },
    { 0, 160,           9, "cyan_stained_glass_pane", NO_PROP },
    { 0, 160,          10, "purple_stained_glass_pane", NO_PROP },
    { 0, 160,          11, "blue_stained_glass_pane", NO_PROP },
    { 0, 160,          12, "brown_stained_glass_pane", NO_PROP },
    { 0, 160,          13, "green_stained_glass_pane", NO_PROP },
    { 0, 160,          14, "red_stained_glass_pane", NO_PROP },
    { 0, 160,          15, "black_stained_glass_pane", NO_PROP },
    { 0, 102,           0, "glass_pane", FENCE_PROP },
    { 0,  37,           0, "dandelion", NO_PROP },
    { 0,  38,           0, "poppy", NO_PROP },
    { 0,  38,           1, "blue_orchid", NO_PROP },
    { 0,  38,           2, "allium", NO_PROP },
    { 0,  38,           3, "azure_bluet", NO_PROP },
    { 0,  38,           4, "red_tulip", NO_PROP },
    { 0,  38,           5, "orange_tulip", NO_PROP },
    { 0,  38,           6, "white_tulip", NO_PROP },
    { 0,  38,           7, "pink_tulip", NO_PROP },
    { 0,  38,           8, "oxeye_daisy", NO_PROP },
    { 0, 175,           0, "sunflower", TALL_FLOWER_PROP },
    { 0, 175,           1, "lilac", TALL_FLOWER_PROP },
    { 0, 175,           2, "tall_grass", TALL_FLOWER_PROP },
    { 0, 175,           3, "large_fern", TALL_FLOWER_PROP },
    { 0, 175,           4, "rose_bush", TALL_FLOWER_PROP },
    { 0, 175,           5, "peony", TALL_FLOWER_PROP },
    { 0,  39,           0, "brown_mushroom", NO_PROP },
    { 0, 100,           0, "red_mushroom_block", MUSHROOM_PROP },
    { 0,  99,           0, "brown_mushroom_block", MUSHROOM_PROP },
    { 0, 100,           0, "mushroom_stem", MUSHROOM_STEM_PROP }, // red mushroom block chosen, arbitrarily; either is fine
    { 0,  41,           0, "gold_block", NO_PROP },
    { 0,  42,           0, "iron_block", NO_PROP },
    { 0,  44,           0, "smooth_stone_slab", SLAB_PROP },	// renamed in 1.14 from stone_slab in 1.13 - it means "the chiseled one" as it's traditionally been; the new 1.14 "stone_slab" means "pure flat stone"
    { 0,  44,           1, "sandstone_slab", SLAB_PROP },
    { 0, 182,           0, "red_sandstone_slab", SLAB_PROP }, // really, just uses 182 exclusively; sometimes rumored to be 205/0, but not so https://minecraft.wiki/w/Java_Edition_data_values#Stone_Slabs
    { 0,  44,           2, "petrified_oak_slab", SLAB_PROP },
    { 0,  44,           3, "cobblestone_slab", SLAB_PROP },
    { 0,  44,           4, "brick_slab", SLAB_PROP },
    { 0,  44,           5, "stone_brick_slab", SLAB_PROP },
    { 0,  44,           6, "nether_brick_slab", SLAB_PROP },
    { 0,  44,           7, "quartz_slab", SLAB_PROP },
    { 0, 126,           0, "oak_slab", SLAB_PROP },
    { 0, 126,           1, "spruce_slab", SLAB_PROP },
    { 0, 126,           2, "birch_slab", SLAB_PROP },
    { 0, 126,           3, "jungle_slab", SLAB_PROP },
    { 0, 126,           4, "acacia_slab", SLAB_PROP },
    { 0, 126,           5, "dark_oak_slab", SLAB_PROP },
    { 0,  45,           0, "bricks", NO_PROP },
    { 0, BLOCK_TNT,     0, "tnt", TRULY_NO_PROP },
    { 0,  47,           0, "bookshelf", NO_PROP },
    { 0,  48,           0, "mossy_cobblestone", NO_PROP },
    { 0,  49,           0, "obsidian", NO_PROP },
    { 0,  50,           0, "torch", TORCH_PROP },
    { 0,  50,           0, "wall_torch", TORCH_PROP },
    { 0, BLOCK_FIRE,    0, "fire", TRULY_NO_PROP }, // ignore age
    { 0,  52,           0, "spawner", NO_PROP },
    { 0,  53,           0, "oak_stairs", STAIRS_PROP },
    { 0, 134,           0, "spruce_stairs", STAIRS_PROP },
    { 0, 135,           0, "birch_stairs", STAIRS_PROP },
    { 0, 136,           0, "jungle_stairs", STAIRS_PROP },
    { 0, 163,           0, "acacia_stairs", STAIRS_PROP },
    { 0, 164,           0, "dark_oak_stairs", STAIRS_PROP },
    { 0,  54,           0, "chest", CHEST_PROP },
    { 0, 146,           0, "trapped_chest", CHEST_PROP },
    { 0,  55,           0, "redstone_wire", WIRE_PROP },  // WIRE_PROP
    { 0,  56,           0, "diamond_ore", NO_PROP },
    { 0,  93,           0, "repeater", REPEATER_PROP },
    { 0, 149,           0, "comparator", COMPARATOR_PROP },
    { 0, 173,           0, "coal_block", NO_PROP },
    { 0,  57,           0, "diamond_block", NO_PROP },
    { 0,  58,           0, "crafting_table", NO_PROP },
    { 0,  59,           0, "wheat", AGE_PROP },
    { 0,  60,           0, "farmland", FARMLAND_PROP },
    { 0,  61,           0, "furnace", FURNACE_PROP },
    { 0,  63,           0, "sign", STANDING_SIGN_PROP }, // 1.13 - in 1.14 it's oak_sign, acacia_sign, etc.
    { 0,  68,           0, "wall_sign", WALL_SIGN_PROP }, // 1.13 - in 1.14 it's oak_wall_sign, acacia_wall_sign, etc.
    { 0,  65,           0, "ladder", FACING_PROP },
    { 0,  66,           0, "rail", RAIL_PROP },   /* 200 */
    { 0,  27,           0, "powered_rail", RAIL_PROP },
    { 0, 157,           0, "activator_rail", RAIL_PROP },
    { 0,  28,           0, "detector_rail", RAIL_PROP },
    { 0,  67,           0, "cobblestone_stairs", STAIRS_PROP },
    { 0, 128,           0, "sandstone_stairs", STAIRS_PROP },
    { 0, 180,           0, "red_sandstone_stairs", STAIRS_PROP },
    { 0,  69,           0, "lever", LEVER_PROP },
    { 0,  70,           0, "stone_pressure_plate", PRESSURE_PROP },
    { 0,  72,           0, "oak_pressure_plate", PRESSURE_PROP },
    { 0, 147,           0, "light_weighted_pressure_plate", WT_PRESSURE_PROP },
    { 0, 148,           0, "heavy_weighted_pressure_plate", WT_PRESSURE_PROP },
    { 0,  71,           0, "iron_door", DOOR_PROP },
    { 0,  73,           0, "redstone_ore", REDSTONE_ORE_PROP },	// unlit by default
    { 0,  76,           0, "redstone_torch", TORCH_PROP },
    { 0,  76,           0, "redstone_wall_torch", TORCH_PROP },
    { 0,  77,           0, "stone_button", BUTTON_PROP },
    { 0, 143,           0, "oak_button", BUTTON_PROP },
    { 0,  78,           0, "snow", SNOW_PROP },
    { 0, 171,           0, "white_carpet", NO_PROP },
    { 0, 171,           1, "orange_carpet", NO_PROP },
    { 0, 171,           2, "magenta_carpet", NO_PROP },
    { 0, 171,           3, "light_blue_carpet", NO_PROP },
    { 0, 171,           4, "yellow_carpet", NO_PROP },
    { 0, 171,           5, "lime_carpet", NO_PROP },
    { 0, 171,           6, "pink_carpet", NO_PROP },
    { 0, 171,           7, "gray_carpet", NO_PROP },
    { 0, 171,           8, "light_gray_carpet", NO_PROP },
    { 0, 171,           9, "cyan_carpet", NO_PROP },
    { 0, 171,          10, "purple_carpet", NO_PROP },
    { 0, 171,          11, "blue_carpet", NO_PROP },
    { 0, 171,          12, "brown_carpet", NO_PROP },
    { 0, 171,          13, "green_carpet", NO_PROP },
    { 0, 171,          14, "red_carpet", NO_PROP },
    { 0, 171,          15, "black_carpet", NO_PROP },
    { 0,  79,           0, "ice", NO_PROP },
    { 0, 212,           0, "frosted_ice", AGE_PROP },
    { 0, 174,           0, "packed_ice", NO_PROP },
    { 0,  81,           0, "cactus", AGE_PROP },
    { 0,  82,           0, "clay", NO_PROP },
    { 0, 159,           0, "white_terracotta", NO_PROP },
    { 0, 159,           1, "orange_terracotta", NO_PROP },
    { 0, 159,           2, "magenta_terracotta", NO_PROP },
    { 0, 159,           3, "light_blue_terracotta", NO_PROP },
    { 0, 159,           4, "yellow_terracotta", NO_PROP },
    { 0, 159,           5, "lime_terracotta", NO_PROP },
    { 0, 159,           6, "pink_terracotta", NO_PROP },
    { 0, 159,           7, "gray_terracotta", NO_PROP },
    { 0, 159,           8, "light_gray_terracotta", NO_PROP },
    { 0, 159,           9, "cyan_terracotta", NO_PROP },
    { 0, 159,          10, "purple_terracotta", NO_PROP },
    { 0, 159,          11, "blue_terracotta", NO_PROP },
    { 0, 159,          12, "brown_terracotta", NO_PROP },
    { 0, 159,          13, "green_terracotta", NO_PROP },
    { 0, 159,          14, "red_terracotta", NO_PROP },
    { 0, 159,          15, "black_terracotta", NO_PROP },
    { 0, 172,           0, "terracotta", NO_PROP },
    { 0,  83,           0, "sugar_cane", AGE_PROP },
    { 0,  84,           0, "jukebox", NO_PROP },
    { 0,  85,           0, "oak_fence", FENCE_PROP },
    { 0, 188,           0, "spruce_fence", FENCE_PROP },
    { 0, 189,           0, "birch_fence", FENCE_PROP },
    { 0, 190,           0, "jungle_fence", FENCE_PROP },
    { 0, 191,           0, "dark_oak_fence", FENCE_PROP },
    { 0, 192,           0, "acacia_fence", FENCE_PROP },
    { 0, 107,           0, "oak_fence_gate", FENCE_GATE_PROP },
    { 0, 183,           0, "spruce_fence_gate", FENCE_GATE_PROP },
    { 0, 184,           0, "birch_fence_gate", FENCE_GATE_PROP },
    { 0, 185,           0, "jungle_fence_gate", FENCE_GATE_PROP },
    { 0, 186,           0, "dark_oak_fence_gate", FENCE_GATE_PROP },
    { 0, 187,           0, "acacia_fence_gate", FENCE_GATE_PROP },
    { 0, 104,           0, "pumpkin_stem", AGE_PROP },
    { 0, 104,     0x8 | 7, "attached_pumpkin_stem", HIGH_FACING_PROP }, // 0x8 means attached
    { 0,  86,           4, "pumpkin", NO_PROP }, //  uncarved pumpkin, same on all sides - dataVal 4
    { 0,  86,           0, "carved_pumpkin", SWNE_FACING_PROP },	// black carved pumpkin
    { 0,  91,           0, "jack_o_lantern", SWNE_FACING_PROP },
    { 0,  87,           0, "netherrack", NO_PROP },
    { 0, BLOCK_SOUL_SAND, 0, "soul_sand", NO_PROP },
    { 0,  89,           0, "glowstone", NO_PROP },
    { 0,  90,           0, "nether_portal", NETHER_PORTAL_AXIS_PROP }, // axis: portal's long edge runs east-west or north-south
    { 0,  35,           0, "white_wool", NO_PROP },
    { 0,  35,           1, "orange_wool", NO_PROP },
    { 0,  35,           2, "magenta_wool", NO_PROP },
    { 0,  35,           3, "light_blue_wool", NO_PROP },
    { 0,  35,           4, "yellow_wool", NO_PROP },
    { 0,  35,           5, "lime_wool", NO_PROP },
    { 0,  35,           6, "pink_wool", NO_PROP },
    { 0,  35,           7, "gray_wool", NO_PROP },
    { 0,  35,           8, "light_gray_wool", NO_PROP },
    { 0,  35,           9, "cyan_wool", NO_PROP },
    { 0,  35,          10, "purple_wool", NO_PROP },
    { 0,  35,          11, "blue_wool", NO_PROP },
    { 0,  35,          12, "brown_wool", NO_PROP },
    { 0,  35,          13, "green_wool", NO_PROP },
    { 0,  35,          14, "red_wool", NO_PROP },
    { 0,  35,          15, "black_wool", NO_PROP },
    { 0,  21,           0, "lapis_ore", NO_PROP },
    { 0,  22,           0, "lapis_block", NO_PROP },
    { 0,  23,           0, "dispenser", DROPPER_PROP },
    { 0, 158,           0, "dropper", DROPPER_PROP },
    { 0,  25,           0, "note_block", NO_PROP },	// pitch, powered, instrument - ignored
    { 0,  92,           0, "cake", CANDLE_CAKE_PROP },
    { 0,  26,           0, "bed", BED_PROP },   // 1.13 bed was renamed "red_bed"; we leave this in, just in case
    { 0,  96,           0, "oak_trapdoor", TRAPDOOR_PROP },
    { 0, 167,           0, "iron_trapdoor", TRAPDOOR_PROP },
    { 0,  30,           0, "cobweb", NO_PROP },
    { 0,  98,           0, "stone_bricks", NO_PROP },
    { 0,  98,           1, "mossy_stone_bricks", NO_PROP },
    { 0,  98,           2, "cracked_stone_bricks", NO_PROP },
    { 0,  98,           3, "chiseled_stone_bricks", NO_PROP },
    { 0,  97,           0, "infested_stone", NO_PROP }, // was called "monster egg"
    { 0,  97,           1, "infested_cobblestone", NO_PROP },
    { 0,  97,           2, "infested_stone_bricks", NO_PROP },
    { 0,  97,           3, "infested_mossy_stone_bricks", NO_PROP },
    { 0,  97,           4, "infested_cracked_stone_bricks", NO_PROP },
    { 0,  97,           5, "infested_chiseled_stone_bricks", NO_PROP },
    { 0,  33,           0, "piston", PISTON_PROP },
    { 0,  29,           0, "sticky_piston", PISTON_PROP },
    { 0, 101,           0, "iron_bars", FENCE_PROP },
    { 0, 103,           0, "melon", NO_PROP },
    { 0, 108,           0, "brick_stairs", STAIRS_PROP },
    { 0, 109,           0, "stone_brick_stairs", STAIRS_PROP },
    { 0, 106,           0, "vine", VINE_PROP },
    { 0, 112,           0, "nether_bricks", NO_PROP },
    { 0, 113,           0, "nether_brick_fence", FENCE_PROP },
    { 0, 114,           0, "nether_brick_stairs", STAIRS_PROP },
    { 0, 115,           0, "nether_wart", AGE_PROP },
    { 0, 118,           0, "cauldron", NO_PROP }, // level directly translates to dataVal, bottom two bits
    { 0, 116,           0, "enchanting_table", NO_PROP },
    { 0, 145,           0, "anvil", ANVIL_PROP },
    { 0, 145,           4, "chipped_anvil", ANVIL_PROP },
    { 0, 145,           8, "damaged_anvil", ANVIL_PROP },
    { 0, 121,           0, "end_stone", NO_PROP },
    { 0, 120,           0, "end_portal_frame", END_PORTAL_PROP },
    { 0, 110,           0, "mycelium", SNOWY_PROP },
    { 0, 111,           0, "lily_pad", NO_PROP },
    { 0, 122,           0, "dragon_egg", NO_PROP },
    { 0, 123,           0, "redstone_lamp", REDSTONE_ORE_PROP }, // goes to 124 when lit
    { 0, 127,           0, "cocoa", COCOA_PROP },
    { 0, 130,           0, "ender_chest", FACING_PROP }, // note that ender chest does not have "single" property that normal chests have; can be waterlogged
    { 0, 129,           0, "emerald_ore", NO_PROP },
    { 0, 133,           0, "emerald_block", NO_PROP },
    { 0, 152,           0, "redstone_block", NO_PROP },
    { 0, 132,           0, "tripwire", TRIPWIRE_PROP },
    { 0, 131,           0, "tripwire_hook", TRIPWIRE_HOOK_PROP },
    { 0, 137,           0, "command_block", COMMAND_BLOCK_PROP },
    { 0, 210,           0, "repeating_command_block", COMMAND_BLOCK_PROP },
    { 0, 211,           0, "chain_command_block", COMMAND_BLOCK_PROP },
    { 0, 138,           0, "beacon", NO_PROP },
    { 0, 139,           0, "cobblestone_wall", WALL_PROP },
    { 0, 139,           1, "mossy_cobblestone_wall", WALL_PROP },
    { 0, 141,           0, "carrots", NO_PROP },
    { 0, 142,           0, "potatoes", NO_PROP },
    { 0, 151,           0, "daylight_detector", DAYLIGHT_PROP },
    { 0, 153,           0, "nether_quartz_ore", NO_PROP },
    { 0, 154,           0, "hopper", HOPPER_PROP },
    { 0, 155,           0, "quartz_block", NO_PROP },	// has AXIS_PROP in Bedrock edition, but not here, https://minecraft.wiki/w/Block_of_Quartz
    { 0, 155,           1, "chiseled_quartz_block", NO_PROP },	// has AXIS_PROP in Bedrock edition, but not here, https://minecraft.wiki/w/Block_of_Quartz
    { 0, 155,           0, "quartz_pillar", QUARTZ_PILLAR_PROP },	// note this always has an axis, so will be set to 2,3,4
    { 0, 155,           5, "quartz_bricks", NO_PROP },
    { 0, 156,           0, "quartz_stairs", STAIRS_PROP },
    { 0, 165,           0, "slime_block", NO_PROP },
    { 0, 168,           0, "prismarine", NO_PROP },
    { 0, 168,           1, "prismarine_bricks", NO_PROP },
    { 0, 168,           2, "dark_prismarine", NO_PROP },
    { 0, 169,           0, "sea_lantern", NO_PROP },
    { 0, 198,           0, "end_rod", EXTENDED_FACING_PROP },
    { 0, 199,           0, "chorus_plant", VINE_PROP },
    { 0, 200,           0, "chorus_flower", NO_PROP },	// uses age
    { 0, 201,           0, "purpur_block", NO_PROP },
    { 0, 202,           0, "purpur_pillar", AXIS_PROP },
    { 0, 203,           0, "purpur_stairs", STAIRS_PROP },
    { 0, 205,           0, "purpur_slab", SLAB_PROP },	// allegedly data value is 1
    { 0, 206,           0, "end_stone_bricks", NO_PROP },
    { 0, 207,           0, "beetroots", AGE_PROP },
    { 0, 208,           0, "grass_path", NO_PROP }, //through 1.16 - note that in 1.17 this is renamed to dirt_path, and that's the name we'll use; left here for backward compatibility
    { 0, 213,           0, "magma_block", NO_PROP },
    { 0, 214,           0, "nether_wart_block", NO_PROP },
    { 0, 215,           0, "red_nether_bricks", NO_PROP },
    { 0, 216,           0, "bone_block", AXIS_PROP },
    { 0, 218,           0, "observer", OBSERVER_PROP },
    { 0, 229,           0, "shulker_box", EXTENDED_FACING_PROP },	// it's a pale purple one, but we just use the purple one TODO
    { 0, 219,           0, "white_shulker_box", EXTENDED_FACING_PROP },
    { 0, 220,           0, "orange_shulker_box", EXTENDED_FACING_PROP },
    { 0, 221,           0, "magenta_shulker_box", EXTENDED_FACING_PROP },
    { 0, 222,           0, "light_blue_shulker_box", EXTENDED_FACING_PROP },
    { 0, 223,           0, "yellow_shulker_box", EXTENDED_FACING_PROP },
    { 0, 224,           0, "lime_shulker_box", EXTENDED_FACING_PROP },
    { 0, 225,           0, "pink_shulker_box", EXTENDED_FACING_PROP },
    { 0, 226,           0, "gray_shulker_box", EXTENDED_FACING_PROP },
    { 0, 227,           0, "light_gray_shulker_box", EXTENDED_FACING_PROP },
    { 0, 228,           0, "cyan_shulker_box", EXTENDED_FACING_PROP },
    { 0, 229,           0, "purple_shulker_box", EXTENDED_FACING_PROP },
    { 0, 230,           0, "blue_shulker_box", EXTENDED_FACING_PROP },
    { 0, 231,           0, "brown_shulker_box", EXTENDED_FACING_PROP },
    { 0, 232,           0, "green_shulker_box", EXTENDED_FACING_PROP },
    { 0, 233,           0, "red_shulker_box", EXTENDED_FACING_PROP },
    { 0, 234,           0, "black_shulker_box", EXTENDED_FACING_PROP },
    { 0, 235,           0, "white_glazed_terracotta", SWNE_FACING_PROP },
    { 0, 236,           0, "orange_glazed_terracotta", SWNE_FACING_PROP },
    { 0, 237,           0, "magenta_glazed_terracotta", SWNE_FACING_PROP },
    { 0, 238,           0, "light_blue_glazed_terracotta", SWNE_FACING_PROP },
    { 0, 239,           0, "yellow_glazed_terracotta", SWNE_FACING_PROP },
    { 0, 240,           0, "lime_glazed_terracotta", SWNE_FACING_PROP },
    { 0, 241,           0, "pink_glazed_terracotta", SWNE_FACING_PROP },
    { 0, 242,           0, "gray_glazed_terracotta", SWNE_FACING_PROP },
    { 0, 243,           0, "light_gray_glazed_terracotta", SWNE_FACING_PROP },
    { 0, 244,           0, "cyan_glazed_terracotta", SWNE_FACING_PROP },
    { 0, 245,           0, "purple_glazed_terracotta", SWNE_FACING_PROP },
    { 0, 246,           0, "blue_glazed_terracotta", SWNE_FACING_PROP },
    { 0, 247,           0, "brown_glazed_terracotta", SWNE_FACING_PROP },
    { 0, 248,           0, "green_glazed_terracotta", SWNE_FACING_PROP },
    { 0, 249,           0, "red_glazed_terracotta", SWNE_FACING_PROP },
    { 0, 250,           0, "black_glazed_terracotta", SWNE_FACING_PROP },
    { 0, 251,          15, "black_concrete", NO_PROP },
    { 0, 251,          14, "red_concrete", NO_PROP },
    { 0, 251,          13, "green_concrete", NO_PROP },
    { 0, 251,          12, "brown_concrete", NO_PROP },
    { 0, 251,          11, "blue_concrete", NO_PROP },
    { 0, 251,          10, "purple_concrete", NO_PROP },
    { 0, 251,           9, "cyan_concrete", NO_PROP },
    { 0, 251,           8, "light_gray_concrete", NO_PROP },
    { 0, 251,           7, "gray_concrete", NO_PROP },
    { 0, 251,           6, "pink_concrete", NO_PROP },
    { 0, 251,           5, "lime_concrete", NO_PROP },
    { 0, 251,           4, "yellow_concrete", NO_PROP },
    { 0, 251,           3, "light_blue_concrete", NO_PROP },
    { 0, 251,           2, "magenta_concrete", NO_PROP },
    { 0, 251,           1, "orange_concrete", NO_PROP },
    { 0, 251,           0, "white_concrete", NO_PROP },
    { 0, 252,          15, "black_concrete_powder", NO_PROP },
    { 0, 252,          14, "red_concrete_powder", NO_PROP },
    { 0, 252,          13, "green_concrete_powder", NO_PROP },
    { 0, 252,          12, "brown_concrete_powder", NO_PROP },
    { 0, 252,          11, "blue_concrete_powder", NO_PROP },
    { 0, 252,          10, "purple_concrete_powder", NO_PROP },
    { 0, 252,           9, "cyan_concrete_powder", NO_PROP },
    { 0, 252,           8, "light_gray_concrete_powder", NO_PROP },
    { 0, 252,           7, "gray_concrete_powder", NO_PROP },
    { 0, 252,           6, "pink_concrete_powder", NO_PROP },
    { 0, 252,           5, "lime_concrete_powder", NO_PROP },
    { 0, 252,           4, "yellow_concrete_powder", NO_PROP },
    { 0, 252,           3, "light_blue_concrete_powder", NO_PROP },
    { 0, 252,           2, "magenta_concrete_powder", NO_PROP },
    { 0, 252,           1, "orange_concrete_powder", NO_PROP },
    { 0, 252,           0, "white_concrete_powder", NO_PROP },
    { 0,  34,           0, "piston_head", PISTON_HEAD_PROP },
    { 0,  34,           0, "moving_piston", PISTON_HEAD_PROP },	// not 100% sure that's what this is...
    { 0,  40,           0, "red_mushroom", NO_PROP },
    { 0,  80,           0, "snow_block", NO_PROP },
    { 0, 105,     0x8 | 7, "attached_melon_stem", HIGH_FACING_PROP },
    { 0, 105,           0, "melon_stem", AGE_PROP },
    { 0, 117,           0, "brewing_stand", NO_PROP },	// see has_bottle_0
    { 0, 119,           0, "end_portal", NO_PROP },
    { 0, BLOCK_FLOWER_POT,                        0, "flower_pot", NO_PROP },
    { 0, BLOCK_FLOWER_POT,        SAPLING_FIELD | 0, "potted_oak_sapling", NO_PROP },
    { 0, BLOCK_FLOWER_POT,        SAPLING_FIELD | 1, "potted_spruce_sapling", NO_PROP },
    { 0, BLOCK_FLOWER_POT,        SAPLING_FIELD | 2, "potted_birch_sapling", NO_PROP },
    { 0, BLOCK_FLOWER_POT,        SAPLING_FIELD | 3, "potted_jungle_sapling", NO_PROP },
    { 0, BLOCK_FLOWER_POT,        SAPLING_FIELD | 4, "potted_acacia_sapling", NO_PROP },
    { 0, BLOCK_FLOWER_POT,        SAPLING_FIELD | 5, "potted_dark_oak_sapling", NO_PROP },
    { 0, BLOCK_FLOWER_POT,      TALLGRASS_FIELD | 2, "potted_fern", NO_PROP },
    { 0, BLOCK_FLOWER_POT,  YELLOW_FLOWER_FIELD | 0, "potted_dandelion", NO_PROP },
    { 0, BLOCK_FLOWER_POT,     RED_FLOWER_FIELD | 0, "potted_poppy", NO_PROP },
    { 0, BLOCK_FLOWER_POT,     RED_FLOWER_FIELD | 1, "potted_blue_orchid", NO_PROP },
    { 0, BLOCK_FLOWER_POT,     RED_FLOWER_FIELD | 2, "potted_allium", NO_PROP },
    { 0, BLOCK_FLOWER_POT,     RED_FLOWER_FIELD | 3, "potted_azure_bluet", NO_PROP },
    { 0, BLOCK_FLOWER_POT,     RED_FLOWER_FIELD | 4, "potted_red_tulip", NO_PROP },
    { 0, BLOCK_FLOWER_POT,     RED_FLOWER_FIELD | 5, "potted_orange_tulip", NO_PROP },
    { 0, BLOCK_FLOWER_POT,     RED_FLOWER_FIELD | 6, "potted_white_tulip", NO_PROP },
    { 0, BLOCK_FLOWER_POT,     RED_FLOWER_FIELD | 7, "potted_pink_tulip", NO_PROP },
    { 0, BLOCK_FLOWER_POT,     RED_FLOWER_FIELD | 8, "potted_oxeye_daisy", NO_PROP },
    { 0, BLOCK_FLOWER_POT,   RED_MUSHROOM_FIELD | 0, "potted_red_mushroom", NO_PROP },
    { 0, BLOCK_FLOWER_POT, BROWN_MUSHROOM_FIELD | 0, "potted_brown_mushroom", NO_PROP },
    { 0, BLOCK_FLOWER_POT,       DEADBUSH_FIELD | 0, "potted_dead_bush", NO_PROP },
    { 0, BLOCK_FLOWER_POT,         CACTUS_FIELD | 0, "potted_cactus", NO_PROP },
    { 0, 144,           0, "skeleton_wall_skull", HEAD_WALL_PROP },
    { 0, 144,    0x80 | 0, "skeleton_skull", HEAD_PROP },
    { 0, 144,        1 << 4, "wither_skeleton_wall_skull", HEAD_WALL_PROP },
    { 0, 144, 0x80 | 1 << 4, "wither_skeleton_skull", HEAD_PROP },
    { 0, 144,        2 << 4, "zombie_wall_head", HEAD_WALL_PROP },
    { 0, 144, 0x80 | 2 << 4, "zombie_head", HEAD_PROP },
    { 0, 144,        3 << 4, "player_wall_head", HEAD_WALL_PROP },
    { 0, 144, 0x80 | 3 << 4, "player_head", HEAD_PROP },
    { 0, 144,        4 << 4, "creeper_wall_head", HEAD_WALL_PROP },
    { 0, 144, 0x80 | 4 << 4, "creeper_head", HEAD_PROP },
    { 0, 144,        5 << 4, "dragon_wall_head", HEAD_WALL_PROP },
    { 0, 144, 0x80 | 5 << 4, "dragon_head", HEAD_PROP },
    { 0, 209,           0, "end_gateway", NO_PROP },
    { 0, 217,           0, "structure_void", NO_PROP },
    { 0, 255,           0, "structure_block", STRUCTURE_PROP },
    { 0,   0,           0, "void_air", NO_PROP },	// consider these air until proven otherwise https://minecraft.wiki/w/Air
    { 0,   0,           0, "cave_air", NO_PROP },	// consider these air until proven otherwise https://minecraft.wiki/w/Air
    { 0, 205,           2, "prismarine_slab", SLAB_PROP }, // added to purpur slab and double slab, dataVal 2, just to be safe (see purpur_slab)
    { 0, 205,           3, "prismarine_brick_slab", SLAB_PROP }, // added to purpur slab and double slab, dataVal 3
    { 0, 205,           4, "dark_prismarine_slab", SLAB_PROP }, // added to purpur slab and double slab, dataVal 4
    { 0,   1,    HIGH_BIT, "prismarine_stairs", STAIRS_PROP },
    { 0,   2,    HIGH_BIT, "prismarine_brick_stairs", STAIRS_PROP },
    { 0,   3,    HIGH_BIT, "dark_prismarine_stairs", STAIRS_PROP },
    { 0,   4,    HIGH_BIT, "spruce_trapdoor", TRAPDOOR_PROP },
    { 0,   5,    HIGH_BIT, "birch_trapdoor", TRAPDOOR_PROP },
    { 0,   6,    HIGH_BIT, "jungle_trapdoor", TRAPDOOR_PROP },
    { 0,   7,    HIGH_BIT, "acacia_trapdoor", TRAPDOOR_PROP },
    { 0,   8,    HIGH_BIT, "dark_oak_trapdoor", TRAPDOOR_PROP },
    { 0,   9,    HIGH_BIT, "spruce_button", BUTTON_PROP },	// TODO++
    { 0,  10,    HIGH_BIT, "birch_button", BUTTON_PROP },
    { 0,  11,    HIGH_BIT, "jungle_button", BUTTON_PROP },
    { 0,  12,    HIGH_BIT, "acacia_button", BUTTON_PROP },
    { 0,  13,    HIGH_BIT, "dark_oak_button", BUTTON_PROP },
	{ 0,  70,           2, "spruce_pressure_plate", PRESSURE_PROP }, // lowest bit is powered
    { 0,  70,           4, "birch_pressure_plate", PRESSURE_PROP },
    { 0,  70,           6, "jungle_pressure_plate", PRESSURE_PROP },
    { 0,  70,           8, "acacia_pressure_plate", PRESSURE_PROP },
    { 0,  70,          10, "dark_oak_pressure_plate", PRESSURE_PROP },
    { 0,  19,  HIGH_BIT | 0, "stripped_oak_log", AXIS_PROP },
    { 0,  19,  HIGH_BIT | 1, "stripped_spruce_log", AXIS_PROP },
    { 0,  19,  HIGH_BIT | 2, "stripped_birch_log", AXIS_PROP },
    { 0,  19,  HIGH_BIT | 3, "stripped_jungle_log", AXIS_PROP },
    { 0,  20,  HIGH_BIT | 0, "stripped_acacia_log", AXIS_PROP },
    { 0,  20,  HIGH_BIT | 1, "stripped_dark_oak_log", AXIS_PROP },
    { 0,  21,  HIGH_BIT | 0, "stripped_oak_wood", AXIS_PROP },
    { 0,  21,  HIGH_BIT | 1, "stripped_spruce_wood", AXIS_PROP },
    { 0,  21,  HIGH_BIT | 2, "stripped_birch_wood", AXIS_PROP },
    { 0,  21,  HIGH_BIT | 3, "stripped_jungle_wood", AXIS_PROP },
    { 0,  22,  HIGH_BIT | 0, "stripped_acacia_wood", AXIS_PROP },
    { 0,  22,  HIGH_BIT | 1, "stripped_dark_oak_wood", AXIS_PROP },
    { 0, 176,           0, "ominous_banner", STANDING_SIGN_PROP },  // maybe not a real thing, but it's listed in the 1.17.1\assets\minecraft\lang\en_us.json file as a block, so let's be safe
    { 0, 176,           0, "white_banner", STANDING_SIGN_PROP },
    { 0,  23,    HIGH_BIT, "orange_banner", STANDING_SIGN_PROP },	// we could crush these a bit into four banners per entry by using bits 32 and 64 for different types.
    { 0,  24,    HIGH_BIT, "magenta_banner", STANDING_SIGN_PROP },
    { 0,  25,    HIGH_BIT, "light_blue_banner", STANDING_SIGN_PROP },
    { 0,  26,    HIGH_BIT, "yellow_banner", STANDING_SIGN_PROP },
    { 0,  27,    HIGH_BIT, "lime_banner", STANDING_SIGN_PROP },
    { 0,  28,    HIGH_BIT, "pink_banner", STANDING_SIGN_PROP },
    { 0,  29,    HIGH_BIT, "gray_banner", STANDING_SIGN_PROP },
    { 0,  30,    HIGH_BIT, "light_gray_banner", STANDING_SIGN_PROP },
    { 0,  31,    HIGH_BIT, "cyan_banner", STANDING_SIGN_PROP },
    { 0,  32,    HIGH_BIT, "purple_banner", STANDING_SIGN_PROP },
    { 0,  33,    HIGH_BIT, "blue_banner", STANDING_SIGN_PROP },
    { 0,  34,    HIGH_BIT, "brown_banner", STANDING_SIGN_PROP },
    { 0,  35,    HIGH_BIT, "green_banner", STANDING_SIGN_PROP },
    { 0,  36,    HIGH_BIT, "red_banner", STANDING_SIGN_PROP },
    { 0,  37,    HIGH_BIT, "black_banner", STANDING_SIGN_PROP }, // TODO++ colors need to be added
    { 0, 177,           0, "white_wall_banner", FACING_PROP },
    { 0,  38,    HIGH_BIT, "orange_wall_banner", FACING_PROP },
    { 0,  39,    HIGH_BIT, "magenta_wall_banner", FACING_PROP },
    { 0,  40,    HIGH_BIT, "light_blue_wall_banner", FACING_PROP },
    { 0,  41,    HIGH_BIT, "yellow_wall_banner", FACING_PROP },
    { 0,  42,    HIGH_BIT, "lime_wall_banner", FACING_PROP },
    { 0,  43,    HIGH_BIT, "pink_wall_banner", FACING_PROP },
    { 0,  44,    HIGH_BIT, "gray_wall_banner", FACING_PROP },
    { 0,  45,    HIGH_BIT, "light_gray_wall_banner", FACING_PROP },
    { 0,  46,    HIGH_BIT, "cyan_wall_banner", FACING_PROP },
    { 0,  47,    HIGH_BIT, "purple_wall_banner", FACING_PROP },
    { 0,  48,    HIGH_BIT, "blue_wall_banner", FACING_PROP },
    { 0,  49,    HIGH_BIT, "brown_wall_banner", FACING_PROP },
    { 0,  50,    HIGH_BIT, "green_wall_banner", FACING_PROP },
    { 0,  51,    HIGH_BIT, "red_wall_banner", FACING_PROP },
    { 0,  52,    HIGH_BIT, "black_wall_banner", FACING_PROP },
    { 0,  53,    HIGH_BIT, "tall_seagrass", TALL_FLOWER_PROP },
    { 0,  54,    HIGH_BIT, "seagrass", NO_PROP },
    { 0,  55,  HIGH_BIT | 0, "smooth_stone", NO_PROP },
    { 0,  55,  HIGH_BIT | 1, "smooth_sandstone", NO_PROP },
    { 0,  55,  HIGH_BIT | 2, "smooth_red_sandstone", NO_PROP },
    { 0,  55,  HIGH_BIT | 3, "smooth_quartz", NO_PROP },
    { 0,  56,    HIGH_BIT, "blue_ice", NO_PROP },
    { 0,  57,    HIGH_BIT, "dried_kelp_block", NO_PROP },
    { 0,  58,  HIGH_BIT | 0, "kelp_plant", TRULY_NO_PROP }, // the lower part
    { 0,  58,  HIGH_BIT | 1, "kelp", TRULY_NO_PROP }, // the top, growing part; don't care about the age
    { 0,   9,      BIT_16, "bubble_column", 0x0 },	// consider as full block of water for now, need to investigate if there's anything to static render (I don't think so...?)
    { 0,  59,  HIGH_BIT | 0, "tube_coral_block", NO_PROP },
    { 0,  59,  HIGH_BIT | 1, "brain_coral_block", NO_PROP },
    { 0,  59,  HIGH_BIT | 2, "bubble_coral_block", NO_PROP },
    { 0,  59,  HIGH_BIT | 3, "fire_coral_block", NO_PROP },
    { 0,  59,  HIGH_BIT | 4, "horn_coral_block", NO_PROP },
    { 0,  60,  HIGH_BIT | 0, "dead_tube_coral_block", NO_PROP },
    { 0,  60,  HIGH_BIT | 1, "dead_brain_coral_block", NO_PROP },
    { 0,  60,  HIGH_BIT | 2, "dead_bubble_coral_block", NO_PROP },
    { 0,  60,  HIGH_BIT | 3, "dead_fire_coral_block", NO_PROP },
    { 0,  60,  HIGH_BIT | 4, "dead_horn_coral_block", NO_PROP },
    { 0,  61,  HIGH_BIT | 0, "tube_coral", NO_PROP },
    { 0,  61,  HIGH_BIT | 1, "brain_coral", NO_PROP },
    { 0,  61,  HIGH_BIT | 2, "bubble_coral", NO_PROP },
    { 0,  61,  HIGH_BIT | 3, "fire_coral", NO_PROP },
    { 0,  61,  HIGH_BIT | 4, "horn_coral", NO_PROP },
    { 0,  62,  HIGH_BIT | 0, "tube_coral_fan", NO_PROP },	// here's where we go nuts: using 7 bits (one waterlogged)
    { 0,  62,  HIGH_BIT | 1, "brain_coral_fan", NO_PROP },
    { 0,  62,  HIGH_BIT | 2, "bubble_coral_fan", NO_PROP },
    { 0,  62,  HIGH_BIT | 3, "fire_coral_fan", NO_PROP },
    { 0,  62,  HIGH_BIT | 4, "horn_coral_fan", NO_PROP },
    { 0,  63,  HIGH_BIT | 0, "dead_tube_coral_fan", NO_PROP },
    { 0,  63,  HIGH_BIT | 1, "dead_brain_coral_fan", NO_PROP },
    { 0,  63,  HIGH_BIT | 2, "dead_bubble_coral_fan", NO_PROP },
    { 0,  63,  HIGH_BIT | 3, "dead_fire_coral_fan", NO_PROP },
    { 0,  63,  HIGH_BIT | 4, "dead_horn_coral_fan", NO_PROP },
    { 0,  64,  HIGH_BIT | 0, "tube_coral_wall_fan", FAN_PROP },
    { 0,  64,  HIGH_BIT | 1, "brain_coral_wall_fan", FAN_PROP },
    { 0,  64,  HIGH_BIT | 2, "bubble_coral_wall_fan", FAN_PROP },
    { 0,  64,  HIGH_BIT | 3, "fire_coral_wall_fan", FAN_PROP },
    { 0,  64,  HIGH_BIT | 4, "horn_coral_wall_fan", FAN_PROP },
    { 0,  65,  HIGH_BIT | 0, "dead_tube_coral_wall_fan", FAN_PROP },
    { 0,  65,  HIGH_BIT | 1, "dead_brain_coral_wall_fan", FAN_PROP },
    { 0,  65,  HIGH_BIT | 2, "dead_bubble_coral_wall_fan", FAN_PROP },
    { 0,  65,  HIGH_BIT | 3, "dead_fire_coral_wall_fan", FAN_PROP },
    { 0,  65,  HIGH_BIT | 4, "dead_horn_coral_wall_fan", FAN_PROP },
    { 0,  66,    HIGH_BIT, "conduit", NO_PROP },
    { 0,  67,    HIGH_BIT, "sea_pickle", PICKLE_PROP },
    { 0,  68,    HIGH_BIT, "turtle_egg", EGG_PROP },
    { 0,  26,           0, "black_bed", BED_PROP }, // TODO+ bed colors should have separate blocks or whatever
    { 0,  26,           0, "red_bed", BED_PROP },
    { 0,  26,           0, "green_bed", BED_PROP },
    { 0,  26,           0, "brown_bed", BED_PROP },
    { 0,  26,           0, "blue_bed", BED_PROP },
    { 0,  26,           0, "purple_bed", BED_PROP },
    { 0,  26,           0, "cyan_bed", BED_PROP },
    { 0,  26,           0, "light_gray_bed", BED_PROP },
    { 0,  26,           0, "gray_bed", BED_PROP },
    { 0,  26,           0, "pink_bed", BED_PROP },
    { 0,  26,           0, "lime_bed", BED_PROP },
    { 0,  26,           0, "yellow_bed", BED_PROP },
    { 0,  26,           0, "light_blue_bed", BED_PROP },
    { 0,  26,           0, "magenta_bed", BED_PROP },
    { 0,  26,           0, "orange_bed", BED_PROP },
    { 0,  26,           0, "white_bed", BED_PROP },
    { 0,  69,  HIGH_BIT | 0, "dead_tube_coral", NO_PROP },
    { 0,  69,  HIGH_BIT | 1, "dead_brain_coral", NO_PROP },
    { 0,  69,  HIGH_BIT | 2, "dead_bubble_coral", NO_PROP },
    { 0,  69,  HIGH_BIT | 3, "dead_fire_coral", NO_PROP },
    { 0,  69,  HIGH_BIT | 4, "dead_horn_coral", NO_PROP },
    { 0,  63,           0, "oak_sign", STANDING_SIGN_PROP }, // in 1.14 it's no longer just "sign", it's oak_sign, acacia_sign, etc. - use bits 16, 32, 64 for the 6 types
    { 0,  63,      BIT_16, "spruce_sign", STANDING_SIGN_PROP },
    { 0,  63,      BIT_32, "birch_sign", STANDING_SIGN_PROP },
    { 0,  63,BIT_32 | BIT_16, "jungle_sign", STANDING_SIGN_PROP },
    { 0,  70,      HIGH_BIT, "acacia_sign", STANDING_SIGN_PROP },
    { 0,  70,HIGH_BIT | BIT_16, "dark_oak_sign", STANDING_SIGN_PROP },
    { 0,  68,           0, "oak_wall_sign", WALL_SIGN_PROP }, // in 1.14 it's oak_wall_sign, acacia_wall_sign, etc.
    { 0,  68,       BIT_8, "spruce_wall_sign", WALL_SIGN_PROP },
    { 0,  68,      BIT_16, "birch_wall_sign", WALL_SIGN_PROP },
    { 0,  68,BIT_16 | BIT_8, "jungle_wall_sign", WALL_SIGN_PROP },
    { 0,  68,      BIT_32, "acacia_wall_sign", WALL_SIGN_PROP },
    { 0,  68,BIT_32 | BIT_8, "dark_oak_wall_sign", WALL_SIGN_PROP },
    { 0,  38,           9, "cornflower", NO_PROP },
    { 0,  38,          10, "lily_of_the_valley", NO_PROP },
    { 0,  38,          11, "wither_rose", NO_PROP },
    { 0,  71,    HIGH_BIT, "sweet_berry_bush", AGE_PROP },
    { 0, BLOCK_FLOWER_POT,     RED_FLOWER_FIELD | 9, "potted_cornflower", NO_PROP },
    { 0, BLOCK_FLOWER_POT,     RED_FLOWER_FIELD | 10, "potted_lily_of_the_valley", NO_PROP },
    { 0, BLOCK_FLOWER_POT,     RED_FLOWER_FIELD | 11, "potted_wither_rose", NO_PROP },
    { 0, BLOCK_FLOWER_POT,         BAMBOO_FIELD | 0, "potted_bamboo", NO_PROP },
    { 0,   6,	           6, "bamboo_sapling", SAPLING_PROP },	// put with the other saplings
    { 0,  72,	    HIGH_BIT, "bamboo", LEAF_SIZE_PROP },
    { 0, 182,	           1, "cut_red_sandstone_slab", SLAB_PROP }, // added to red_sandstone_slab and double slab
    { 0, 182,	           2, "smooth_red_sandstone_slab", SLAB_PROP },
    { 0, 182,	           3, "cut_sandstone_slab", SLAB_PROP },
    { 0, 182,	           4, "smooth_sandstone_slab", SLAB_PROP },
    { 0, 182,	           5, "granite_slab", SLAB_PROP },
    { 0, 182,	           6, "polished_granite_slab", SLAB_PROP },
    { 0, 182,	           7, "smooth_quartz_slab", SLAB_PROP },
    { 0, 205,	           5, "red_nether_brick_slab", SLAB_PROP }, // added to purpur slab and double slab, dataVal 4
    { 0, 205,	           6, "mossy_stone_brick_slab", SLAB_PROP },
    { 0, 205,	           7, "mossy_cobblestone_slab", SLAB_PROP },
    { 0,  74,	HIGH_BIT | 0, "andesite_slab", SLAB_PROP },
    { 0,  74,	HIGH_BIT | 1, "polished_andesite_slab", SLAB_PROP },
    { 0,  74,	HIGH_BIT | 2, "diorite_slab", SLAB_PROP },
    { 0,  74,	HIGH_BIT | 3, "polished_diorite_slab", SLAB_PROP },
    { 0,  74,	HIGH_BIT | 4, "end_stone_brick_slab", SLAB_PROP },
    { 0,  74,	HIGH_BIT | 5, "stone_slab", SLAB_PROP },	// the 1.14 stone_slab is entirely "normal" stone, no chiseling - it's a new slab type; 1.13 used this to mean what is now "smooth_stone_slab", and so we rename that in the nbt.cpp code.
    { 0, 109,	    HIGH_BIT, "stone_stairs", STAIRS_PROP },
    { 0, 110,	    HIGH_BIT, "granite_stairs", STAIRS_PROP },
    { 0, 111,       HIGH_BIT, "polished_granite_stairs", STAIRS_PROP },
    { 0, 112,	    HIGH_BIT, "smooth_quartz_stairs", STAIRS_PROP },
    { 0, 113,	    HIGH_BIT, "diorite_stairs", STAIRS_PROP },
    { 0, 114,       HIGH_BIT, "polished_diorite_stairs", STAIRS_PROP },
    { 0, 115,	    HIGH_BIT, "end_stone_brick_stairs", STAIRS_PROP },
    { 0, 116,	    HIGH_BIT, "andesite_stairs", STAIRS_PROP },
    { 0, 117,       HIGH_BIT, "polished_andesite_stairs", STAIRS_PROP },
    { 0, 118,	    HIGH_BIT, "red_nether_brick_stairs", STAIRS_PROP },
    { 0, 119,	    HIGH_BIT, "mossy_stone_brick_stairs", STAIRS_PROP },
    { 0, 120,       HIGH_BIT, "mossy_cobblestone_stairs", STAIRS_PROP },
    { 0, 121,	    HIGH_BIT, "smooth_sandstone_stairs", STAIRS_PROP },
    { 0, 122,	    HIGH_BIT, "smooth_red_sandstone_stairs", STAIRS_PROP },
    { 0, 139,              2, "brick_wall", WALL_PROP },
    { 0, 139,              3, "granite_wall", WALL_PROP },
    { 0, 139,              4, "diorite_wall", WALL_PROP },
    { 0, 139,              5, "andesite_wall", WALL_PROP },
    { 0, 139,              6, "prismarine_wall", WALL_PROP },
    { 0, 139,              7, "stone_brick_wall", WALL_PROP },
    { 0, 139,              8, "mossy_stone_brick_wall", WALL_PROP },
    { 0, 139,              9, "end_stone_brick_wall", WALL_PROP },
    { 0, 139,             10, "nether_brick_wall", WALL_PROP },
    { 0, 139,             11, "red_nether_brick_wall", WALL_PROP },
    { 0, 139,             12, "sandstone_wall", WALL_PROP },
    { 0, 139,             13, "red_sandstone_wall", WALL_PROP },
    { 0,  75,		HIGH_BIT, "jigsaw", EXTENDED_FACING_PROP },
    { 0,  76,       HIGH_BIT, "composter", NO_PROP }, // level directly translates to dataVal
    { 0, BLOCK_FURNACE,	      BIT_16, "loom", FACING_PROP },	// add to furnace and burning furnace
    { 0, BLOCK_FURNACE,	      BIT_32, "smoker", FURNACE_PROP },
    { 0, BLOCK_FURNACE,BIT_32 | BIT_16, "blast_furnace", FURNACE_PROP },
    { 0,  77,       HIGH_BIT, "barrel", BARREL_PROP },
    { 0,  78,       HIGH_BIT, "stonecutter", SWNE_FACING_PROP },	// use just the lower two bits instead of three for facing. S=0, etc.
    { 0, BLOCK_CRAFTING_TABLE,	1, "cartography_table", NO_PROP },
    { 0, BLOCK_CRAFTING_TABLE,	2, "fletching_table", NO_PROP },
    { 0, BLOCK_CRAFTING_TABLE,	3, "smithing_table", NO_PROP },
    { 0,  79,       HIGH_BIT, "grindstone", GRINDSTONE_PROP }, // facing SWNE and face: floor|ceiling|wall
    { 0,  80,       HIGH_BIT, "lectern", LECTERN_PROP },
    { 0,  81,       HIGH_BIT, "bell", BELL_PROP },
    { 0,  82,       HIGH_BIT, "lantern", LANTERN_PROP },	// uses just "hanging" for bit 0x1
    { 0,  83,       HIGH_BIT, "campfire", CAMPFIRE_PROP },
    { 0,  84,       HIGH_BIT, "scaffolding", NO_PROP },	// uses just "bottom" for bit 0x1
    { 0,  85,       HIGH_BIT, "bee_nest", EXTENDED_SWNE_FACING_PROP },	// facing is 0x3, honey_level is 0x01C, nest/hive is 0x20
    { 0,  85,HIGH_BIT | BIT_32, "beehive", EXTENDED_SWNE_FACING_PROP },
    { 0,  86,       HIGH_BIT, "honey_block", NO_PROP },
    { 0,  87,       HIGH_BIT, "honeycomb_block", NO_PROP },
    { 0, BLOCK_SOUL_SAND,  1, "soul_soil", NO_PROP },	// with soul sand
    { 0, 214,			   1, "warped_wart_block", NO_PROP },
    { 0, 216,			   1, "basalt", AXIS_PROP },
    { 0, 216,			   2, "polished_basalt", AXIS_PROP },
    { 0,   3,              3, "crimson_nylium", NO_PROP }, // note no SNOWY_PROP
    { 0,   3,              4, "warped_nylium", NO_PROP }, // note no SNOWY_PROP
    { 0,  40,              1, "crimson_fungus", NO_PROP },
    { 0,  40,              2, "warped_fungus", NO_PROP },
    { 0,  31,			   3, "nether_sprouts", NO_PROP },
    { 0,  31,              4, "crimson_roots", NO_PROP },	// We *don't* put these two as types of red poppy, but *do* make them this way when put in a pot.
    { 0,  31,              5, "warped_roots", NO_PROP },	// This is done because the "in the pot" tile is different than the "in the wild" version, so this made it easier. Ugh.
    { 0, BLOCK_FLOWER_POT,     RED_FLOWER_FIELD | 12, "potted_crimson_fungus", NO_PROP },
    { 0, BLOCK_FLOWER_POT,     RED_FLOWER_FIELD | 13, "potted_warped_fungus", NO_PROP },
    { 0, BLOCK_FLOWER_POT,     RED_FLOWER_FIELD | 14, "potted_crimson_roots", NO_PROP },
    { 0, BLOCK_FLOWER_POT,     RED_FLOWER_FIELD | 15, "potted_warped_roots", NO_PROP },
    { 0,  89,              1, "shroomlight", NO_PROP },
    { 0, 162,     BIT_16 | 2, "crimson_hyphae", AXIS_PROP },	// same as logs below, but with a high bit set to mean that it's "wood" texture on the endcaps. 
    { 0, 162,     BIT_16 | 3, "warped_hyphae", AXIS_PROP },	// same as logs below, but with a high bit set to mean that it's "wood" texture on the endcaps. 
    { 0, 162,              2, "crimson_stem", AXIS_PROP },	// log equivalent
    { 0, 162,              3, "warped_stem", AXIS_PROP },
    { 0,  20,   HIGH_BIT | 2, "stripped_crimson_stem", AXIS_PROP },	// extension of stripped acacia (log)
    { 0,  20,   HIGH_BIT | 3, "stripped_warped_stem", AXIS_PROP },
    { 0,  22,   HIGH_BIT | 2, "stripped_crimson_hyphae", AXIS_PROP },	// extension of stripped acacia wood
    { 0,  22,   HIGH_BIT | 3, "stripped_warped_hyphae", AXIS_PROP },
    { 0,   5,              6, "crimson_planks", NO_PROP },
    { 0,   5,              7, "warped_planks", NO_PROP },
    { 0,   1,              7, "blackstone", NO_PROP },
    { 0,   1,              8, "chiseled_polished_blackstone", NO_PROP },
    { 0,   1,              9, "polished_blackstone", NO_PROP },
    { 0,   1,             10, "gilded_blackstone", NO_PROP },
    { 0,   1,             11, "polished_blackstone_bricks", NO_PROP },
    { 0,   1,             12, "cracked_polished_blackstone_bricks", NO_PROP },
    { 0,   1,             13, "netherite_block", NO_PROP },
    { 0,   1,             14, "ancient_debris", NO_PROP },
    { 0,   1,             15, "nether_gold_ore", NO_PROP },
    { 0, 112,              1, "chiseled_nether_bricks", NO_PROP },
    { 0, 112,              2, "cracked_nether_bricks", NO_PROP },
    { 0, BLOCK_CRAFTING_TABLE,	4, "lodestone", NO_PROP },
    { 0,  88,       HIGH_BIT, "crying_obsidian", NO_PROP },
    { 0, BLOCK_TNT,		   1, "target", TRULY_NO_PROP },
    { 0,  89,       HIGH_BIT, "respawn_anchor", NO_PROP },
    { 0, 139,             14, "blackstone_wall", WALL_PROP },
    { 0, 139,             15, "polished_blackstone_wall", WALL_PROP },
    { 0, 139,             16, "polished_blackstone_brick_wall", WALL_PROP },	// yeah, that's right, 16 baby - no data values used for walls, it's all implied in Mineways
    { 0, 123,       HIGH_BIT, "crimson_stairs", STAIRS_PROP },
    { 0, 124,       HIGH_BIT, "warped_stairs", STAIRS_PROP },
    { 0, 125,       HIGH_BIT, "blackstone_stairs", STAIRS_PROP },
    { 0, 126,       HIGH_BIT, "polished_blackstone_stairs", STAIRS_PROP },
    { 0, 127,       HIGH_BIT, "polished_blackstone_brick_stairs", STAIRS_PROP },
    { 0,  90,       HIGH_BIT, "crimson_trapdoor", TRAPDOOR_PROP },
    { 0,  91,       HIGH_BIT, "warped_trapdoor", TRAPDOOR_PROP },
    { 0,  92,       HIGH_BIT, "crimson_button", BUTTON_PROP },
    { 0,  93,       HIGH_BIT, "warped_button", BUTTON_PROP },
    { 0,  94,       HIGH_BIT, "polished_blackstone_button", BUTTON_PROP },
    { 0,  95,       HIGH_BIT, "crimson_fence", FENCE_PROP },
    { 0,  96,       HIGH_BIT, "warped_fence", FENCE_PROP },
    { 0,  97,       HIGH_BIT, "crimson_fence_gate", FENCE_GATE_PROP },
    { 0,  98,       HIGH_BIT, "warped_fence_gate", FENCE_GATE_PROP },
    { 0,  99,       HIGH_BIT, "crimson_door", DOOR_PROP },
    { 0, 100,       HIGH_BIT, "warped_door", DOOR_PROP },
    { 0,  70,          12, "crimson_pressure_plate", PRESSURE_PROP },
    { 0,  70,          14, "warped_pressure_plate", PRESSURE_PROP },
    { 0,  70,          16, "polished_blackstone_pressure_plate", PRESSURE_PROP },
    { 0, 105,       HIGH_BIT, "crimson_slab", SLAB_PROP },	// new set of slabs - note that 104 is used by the corresponding double slabs
    { 0, 105,   HIGH_BIT | 1, "warped_slab", SLAB_PROP },
    { 0, 105,   HIGH_BIT | 2, "blackstone_slab", SLAB_PROP },
    { 0, 105,   HIGH_BIT | 3, "polished_blackstone_slab", SLAB_PROP },
    { 0, 105,   HIGH_BIT | 4, "polished_blackstone_brick_slab", SLAB_PROP },
    { 0,  70, HIGH_BIT | BIT_32, "crimson_sign", STANDING_SIGN_PROP },
    { 0,  70, HIGH_BIT | BIT_32 | BIT_16, "warped_sign", STANDING_SIGN_PROP },
    { 0,  68, BIT_32 | BIT_16, "crimson_wall_sign", WALL_SIGN_PROP },
    { 0,  68, BIT_32 | BIT_16 | BIT_8, "warped_wall_sign", WALL_SIGN_PROP },
    { 0, BLOCK_FIRE,  BIT_16, "soul_fire", NO_PROP },
    { 0, 106,       HIGH_BIT, "soul_torch", TORCH_PROP },	// was soul_fire_torch in an earlier 1.16 beta, like 16
    { 0, 106,       HIGH_BIT, "soul_wall_torch", TORCH_PROP },	// was soul_fire_torch in an earlier 1.16 beta, like 16
    { 0,  82, HIGH_BIT | 0x2, "soul_lantern", LANTERN_PROP },	// uses just "hanging" for bit 0x1
    { 0,  83, HIGH_BIT | 0x8, "soul_campfire", CAMPFIRE_PROP },
    { 0, 107,       HIGH_BIT, "weeping_vines_plant", TRULY_NO_PROP },
    { 0, 107, HIGH_BIT | BIT_32, "weeping_vines", TRULY_NO_PROP },
    { 0, 107,       HIGH_BIT | 1, "twisting_vines_plant", TRULY_NO_PROP },
    { 0, 107, HIGH_BIT | BIT_32 | 1, "twisting_vines", TRULY_NO_PROP },
    { 0, 108,       HIGH_BIT, "chain", AXIS_PROP },
    { 0, 128,       HIGH_BIT, "candle", CANDLE_PROP },  // 129 is lit
    { 0, 130,   HIGH_BIT |  0, "white_candle", CANDLE_PROP }, // 131 is lit
    { 0, 130,   HIGH_BIT |  1, "orange_candle", CANDLE_PROP },
    { 0, 130,   HIGH_BIT |  2, "magenta_candle", CANDLE_PROP },
    { 0, 130,   HIGH_BIT |  3, "light_blue_candle", CANDLE_PROP },
    { 0, 130,   HIGH_BIT |  4, "yellow_candle", CANDLE_PROP },
    { 0, 130,   HIGH_BIT |  5, "lime_candle", CANDLE_PROP },
    { 0, 130,   HIGH_BIT |  6, "pink_candle", CANDLE_PROP },
    { 0, 130,   HIGH_BIT |  7, "gray_candle", CANDLE_PROP },
    { 0, 130,   HIGH_BIT |  8, "light_gray_candle", CANDLE_PROP },
    { 0, 130,   HIGH_BIT |  9, "cyan_candle", CANDLE_PROP },
    { 0, 130,   HIGH_BIT | 10, "purple_candle", CANDLE_PROP },
    { 0, 130,   HIGH_BIT | 11, "blue_candle", CANDLE_PROP },
    { 0, 130,   HIGH_BIT | 12, "brown_candle", CANDLE_PROP },
    { 0, 130,   HIGH_BIT | 13, "green_candle", CANDLE_PROP },
    { 0, 130,   HIGH_BIT | 14, "red_candle", CANDLE_PROP },
    { 0, 130,   HIGH_BIT | 15, "black_candle", CANDLE_PROP },
    { 0,  92,            0x7, "candle_cake", CANDLE_CAKE_PROP },
    { 0,  92,     BIT_16 | 0, "white_candle_cake", CANDLE_CAKE_PROP },  // funky: cake can be either with a single candle, lit or not, OR have a bite taken out of it. 
    { 0,  92,     BIT_16 | 1, "orange_candle_cake", CANDLE_CAKE_PROP },
    { 0,  92,     BIT_16 | 2, "magenta_candle_cake", CANDLE_CAKE_PROP },
    { 0,  92,     BIT_16 | 3, "light_blue_candle_cake", CANDLE_CAKE_PROP },
    { 0,  92,     BIT_16 | 4, "yellow_candle_cake", CANDLE_CAKE_PROP },
    { 0,  92,     BIT_16 | 5, "lime_candle_cake", CANDLE_CAKE_PROP },
    { 0,  92,     BIT_16 | 6, "pink_candle_cake", CANDLE_CAKE_PROP },
    { 0,  92,     BIT_16 | 7, "gray_candle_cake", CANDLE_CAKE_PROP },
    { 0,  92,     BIT_16 | 8, "light_gray_candle_cake", CANDLE_CAKE_PROP },
    { 0,  92,     BIT_16 | 9, "cyan_candle_cake", CANDLE_CAKE_PROP },
    { 0,  92,     BIT_16 | 10, "purple_candle_cake", CANDLE_CAKE_PROP },
    { 0,  92,     BIT_16 | 11, "blue_candle_cake", CANDLE_CAKE_PROP },
    { 0,  92,     BIT_16 | 12, "brown_candle_cake", CANDLE_CAKE_PROP },
    { 0,  92,     BIT_16 | 13, "green_candle_cake", CANDLE_CAKE_PROP },
    { 0,  92,     BIT_16 | 14, "red_candle_cake", CANDLE_CAKE_PROP },
    { 0,  92,     BIT_16 | 15, "black_candle_cake", CANDLE_CAKE_PROP },
    { 0, 132,   HIGH_BIT | 0, "amethyst_block", NO_PROP },
    { 0, 133,   HIGH_BIT | 0, "small_amethyst_bud", AMETHYST_PROP }, // 2 bits for type, 3 bits for direction
    { 0, 133,   HIGH_BIT | 1, "medium_amethyst_bud", AMETHYST_PROP }, // 2 bits for type, 3 bits for direction
    { 0, 133,   HIGH_BIT | 2, "large_amethyst_bud", AMETHYST_PROP }, // 2 bits for type, 3 bits for direction
    { 0, 133,   HIGH_BIT | 3, "amethyst_cluster", AMETHYST_PROP }, // 2 bits for type, 3 bits for direction
    { 0, 132,   HIGH_BIT | 1, "budding_amethyst", NO_PROP },
    { 0, 132,   HIGH_BIT | 2, "calcite", NO_PROP },
    { 0, 132,   HIGH_BIT | 3, "tuff", NO_PROP },
    { 0,  20,              1, "tinted_glass", NO_PROP },  // stuffed in with glass
    { 0, 132,   HIGH_BIT | 4, "dripstone_block", NO_PROP },
    { 0, 134,       HIGH_BIT, "pointed_dripstone", DRIPSTONE_PROP },    // 5 thickness, vertical_direction: up/down
    { 0, 132,   HIGH_BIT | 5, "copper_ore", NO_PROP },
    { 0, 132,   HIGH_BIT | 6, "deepslate_copper_ore", NO_PROP },
    { 0, 132,   HIGH_BIT | 7, "copper_block", NO_PROP },
    { 0, 132,   HIGH_BIT | 8, "exposed_copper", NO_PROP },
    { 0, 132,   HIGH_BIT | 9, "weathered_copper", NO_PROP },
    { 0, 132,  HIGH_BIT | 10, "oxidized_copper", NO_PROP },
    { 0, 132,  HIGH_BIT | 11, "cut_copper", NO_PROP },
    { 0, 132,  HIGH_BIT | 12, "exposed_cut_copper", NO_PROP },
    { 0, 132,  HIGH_BIT | 13, "weathered_cut_copper", NO_PROP },
    { 0, 132,  HIGH_BIT | 14, "oxidized_cut_copper", NO_PROP },
    { 0, 135,	    HIGH_BIT, "cut_copper_stairs", STAIRS_PROP },
    { 0, 136,	    HIGH_BIT, "exposed_cut_copper_stairs", STAIRS_PROP },
    { 0, 137,	    HIGH_BIT, "weathered_cut_copper_stairs", STAIRS_PROP },
    { 0, 138,	    HIGH_BIT, "oxidized_cut_copper_stairs", STAIRS_PROP },
    { 0, 142,	HIGH_BIT | 0, "cut_copper_slab", SLAB_PROP },
    { 0, 142,	HIGH_BIT | 1, "exposed_cut_copper_slab", SLAB_PROP },
    { 0, 142,	HIGH_BIT | 2, "weathered_cut_copper_slab", SLAB_PROP },
    { 0, 142,	HIGH_BIT | 3, "oxidized_cut_copper_slab", SLAB_PROP },
    { 0, 132,  HIGH_BIT | 15, "waxed_copper_block", NO_PROP },
    { 0, 132,  HIGH_BIT | 16, "waxed_exposed_copper", NO_PROP },
    { 0, 132,  HIGH_BIT | 17, "waxed_weathered_copper", NO_PROP },
    { 0, 132,  HIGH_BIT | 18, "waxed_oxidized_copper", NO_PROP },
    { 0, 132,  HIGH_BIT | 19, "waxed_cut_copper", NO_PROP },
    { 0, 132,  HIGH_BIT | 20, "waxed_exposed_cut_copper", NO_PROP },
    { 0, 132,  HIGH_BIT | 21, "waxed_weathered_cut_copper", NO_PROP },
    { 0, 132,  HIGH_BIT | 22, "waxed_oxidized_cut_copper", NO_PROP },
    { 0, 143,	    HIGH_BIT, "waxed_cut_copper_stairs", STAIRS_PROP },
    { 0, 145,	    HIGH_BIT, "waxed_exposed_cut_copper_stairs", STAIRS_PROP },
    { 0, 146,	    HIGH_BIT, "waxed_weathered_cut_copper_stairs", STAIRS_PROP },
    { 0, 147,	    HIGH_BIT, "waxed_oxidized_cut_copper_stairs", STAIRS_PROP },
    { 0, 142,	HIGH_BIT | 4, "waxed_cut_copper_slab", SLAB_PROP },
    { 0, 142,	HIGH_BIT | 5, "waxed_exposed_cut_copper_slab", SLAB_PROP },
    { 0, 142,	HIGH_BIT | 6, "waxed_weathered_cut_copper_slab", SLAB_PROP },
    { 0, 142,	HIGH_BIT | 7, "waxed_oxidized_cut_copper_slab", SLAB_PROP },
    { 0, 139,	    HIGH_BIT, "lightning_rod", EXTENDED_FACING_PROP },
    { 0, 148,	    HIGH_BIT, "cave_vines", BERRIES_PROP },
    { 0, 148,	HIGH_BIT | 1, "cave_vines_plant", BERRIES_PROP },    // ignore the age
    { 0, 150,	    HIGH_BIT, "spore_blossom", NO_PROP },
    { 0, 151,	    HIGH_BIT, "azalea", NO_PROP },
    { 0, BLOCK_FLOWER_POT,         AZALEA_FIELD | 0, "potted_azalea_bush", NO_PROP },
    { 0, BLOCK_FLOWER_POT,         AZALEA_FIELD | 1, "potted_flowering_azalea_bush", NO_PROP },
    { 0, 151,	HIGH_BIT | 1, "flowering_azalea", NO_PROP },
    { 0, 161,	           2, "azalea_leaves", NO_PROP },
    { 0, 161,	           3, "flowering_azalea_leaves", NO_PROP },
    { 0, 171,             16, "moss_carpet", NO_PROP },
    { 0, 132,  HIGH_BIT | 23, "moss_block", NO_PROP },
    { 0, 152,	    HIGH_BIT, "big_dripleaf", BIG_DRIPLEAF_PROP },
    { 0, 152,	HIGH_BIT | 1, "big_dripleaf_stem", BIG_DRIPLEAF_PROP },
    { 0, 153,	    HIGH_BIT, "small_dripleaf", SMALL_DRIPLEAF_PROP },
    { 0, 132,  HIGH_BIT | 24, "rooted_dirt", NO_PROP },
    { 0, 107,   HIGH_BIT | 2, "hanging_roots", TRULY_NO_PROP },  // weeping vines
    { 0, 132,  HIGH_BIT | 25, "powder_snow", NO_PROP },
    { 0, 154,       HIGH_BIT, "glow_lichen", VINE_PROP },
    { 0, 155,       HIGH_BIT, "sculk_sensor", CALIBRATED_SCULK_SENSOR_PROP },   // doesn't really need facing for this one, but sculk_sensor_phase is used
    { 0, 216,              3, "deepslate", AXIS_PROP }, // with bone block, basalt, etc.
    { 0, 132,  HIGH_BIT | 26, "cobbled_deepslate", NO_PROP },
    { 0, 142,	HIGH_BIT | BIT_16 | 0, "cobbled_deepslate_slab", SLAB_PROP },    // double slab is 136, traditional (and a waste)
    { 0, 156,	    HIGH_BIT, "cobbled_deepslate_stairs", STAIRS_PROP },
    { 0, 139,             17, "cobbled_deepslate_wall", WALL_PROP },	// no data values used for walls, it's all implied in Mineways
    { 0, 132,  HIGH_BIT | 27, "chiseled_deepslate", NO_PROP },
    { 0, 132,  HIGH_BIT | 28, "polished_deepslate", NO_PROP },
    { 0, 142,	HIGH_BIT | BIT_16 | 1, "polished_deepslate_slab", SLAB_PROP },
    { 0, 157,	    HIGH_BIT, "polished_deepslate_stairs", STAIRS_PROP },
    { 0, 139,             18, "polished_deepslate_wall", WALL_PROP },	// no data values used for walls, it's all implied in Mineways
    { 0, 132,  HIGH_BIT | 29, "deepslate_bricks", NO_PROP },
    { 0, 142,	HIGH_BIT | BIT_16 | 2, "deepslate_brick_slab", SLAB_PROP },
    { 0, 158,	    HIGH_BIT, "deepslate_brick_stairs", STAIRS_PROP },
    { 0, 139,             19, "deepslate_brick_wall", WALL_PROP },	// no data values used for walls, it's all implied in Mineways
    { 0, 132,  HIGH_BIT | 30, "deepslate_tiles", NO_PROP },
    { 0, 142,	HIGH_BIT | BIT_16 | 3, "deepslate_tile_slab", SLAB_PROP },
    { 0, 159,	    HIGH_BIT, "deepslate_tile_stairs", STAIRS_PROP },
    { 0, 139,             20, "deepslate_tile_wall", WALL_PROP },	// no data values used for walls, it's all implied in Mineways
    { 0, 132,  HIGH_BIT | 31, "cracked_deepslate_bricks", NO_PROP },
    { 0, 132,  HIGH_BIT | 32, "cracked_deepslate_tiles", NO_PROP },
    { 0, 216,     BIT_16 | 0, "infested_deepslate", AXIS_PROP }, // with bone block, basalt, etc. - continues deepslate
    { 0, 132,  HIGH_BIT | 33, "smooth_basalt", NO_PROP },   // note this form of basalt is simply a block, no directionality like other basalt
    { 0, 132,  HIGH_BIT | 34, "raw_iron_block", NO_PROP },
    { 0, 132,  HIGH_BIT | 35, "raw_copper_block", NO_PROP },
    { 0, 132,  HIGH_BIT | 36, "raw_gold_block", NO_PROP },
    { 0, 208,              0, "dirt_path", NO_PROP },   // in 1.17 renamed to dirt path and given textures https://minecraft.wiki/w/Dirt_Path
    { 0, 132,  HIGH_BIT | 37, "deepslate_coal_ore", NO_PROP },
    { 0, 132,  HIGH_BIT | 38, "deepslate_iron_ore", NO_PROP },  // copper done way earlier, so be it...
    { 0, 132,  HIGH_BIT | 39, "deepslate_gold_ore", NO_PROP },
    { 0, 132,  HIGH_BIT | 40, "deepslate_redstone_ore", NO_PROP },
    { 0, 132,  HIGH_BIT | 41, "deepslate_emerald_ore", NO_PROP },
    { 0, 132,  HIGH_BIT | 42, "deepslate_lapis_ore", NO_PROP },
    { 0, 132,  HIGH_BIT | 43, "deepslate_diamond_ore", NO_PROP },
    { 0, 118,            0x0, "water_cauldron", NO_PROP }, // I assume this is the same as a cauldron, basically, with the level > 0, https://minecraft.wiki/w/Cauldron
    { 0, 118,            0x4, "lava_cauldron", NO_PROP }, // level directly translates to dataVal, bottom two bits
    { 0, 118,            0x8, "powder_snow_cauldron", NO_PROP }, // level directly translates to dataVal, bottom two bits
    { 0, 0,                0, "light", NO_PROP },   // for now, just make it air, since it normally doesn't appear
    { 0, 160,       HIGH_BIT, "mangrove_log", AXIS_PROP },
    { 0, 160, HIGH_BIT | BIT_16, "mangrove_wood", AXIS_PROP },	// same as log, but with a high bit set to mean that it's "wood" texture on the endcaps. 
    { 0,   5,              8, "mangrove_planks", NO_PROP },
    { 0, 162,       HIGH_BIT, "mangrove_door", DOOR_PROP },
    { 0, 163,       HIGH_BIT, "mangrove_trapdoor", TRAPDOOR_PROP },
    { 0, 164,       HIGH_BIT, "mangrove_propagule", PROPAGULE_PROP },   // also has hanging property, waterlogged prop
    { 0, BLOCK_FLOWER_POT,        SAPLING_FIELD | 6, "potted_mangrove_propagule", NO_PROP },
    { 0, 165,       HIGH_BIT, "mangrove_roots", NO_PROP },
    { 0, 166,       HIGH_BIT, "muddy_mangrove_roots", AXIS_PROP },
    { 0, 167,   HIGH_BIT | 0, "stripped_mangrove_log", AXIS_PROP },
    { 0, 168,   HIGH_BIT | 0, "stripped_mangrove_wood", AXIS_PROP },
    { 0, 181,       HIGH_BIT, "mangrove_leaves", LEAF_PROP },
    { 0,  74,	HIGH_BIT | 6, "mangrove_slab", SLAB_PROP },
    { 0,  74,	HIGH_BIT | 7, "mud_brick_slab", SLAB_PROP },
    { 0, 169,	    HIGH_BIT, "mangrove_stairs", STAIRS_PROP },
    { 0, 170,	    HIGH_BIT, "mud_brick_stairs", STAIRS_PROP },
    { 0, 171,       HIGH_BIT, "mangrove_sign", STANDING_SIGN_PROP },
    { 0, 172,       HIGH_BIT, "mangrove_wall_sign", WALL_SIGN_PROP },
    { 0,  70,          18, "mangrove_pressure_plate", PRESSURE_PROP },
    { 0, 174,       HIGH_BIT, "mangrove_button", BUTTON_PROP },
    { 0, 175,       HIGH_BIT, "mangrove_fence", FENCE_PROP },
    { 0, 176,       HIGH_BIT, "mangrove_fence_gate", FENCE_GATE_PROP },
    { 0, 132,  HIGH_BIT | 44, "mud", NO_PROP },
    { 0, 132,  HIGH_BIT | 45, "mud_bricks", NO_PROP },
    { 0, 132,  HIGH_BIT | 46, "packed_mud", NO_PROP },
    { 0, 139,             21, "mud_brick_wall", WALL_PROP },	// no data values used for walls, it's all implied in Mineways
    { 0,   3,              5, "reinforced_deepslate", NO_PROP },
    { 0, 132,  HIGH_BIT | 47, "sculk", NO_PROP },
    { 0,  88,   HIGH_BIT | 1, "sculk_catalyst", NO_PROP },  // part of crying obsidian, as it emits
    { 0, 177,       HIGH_BIT, "sculk_shrieker", NO_PROP },
    { 0, 178,       HIGH_BIT, "sculk_vein", VINE_PROP },
    { 0, 179,       HIGH_BIT, "frogspawn", NO_PROP },
    { 0, 180,       HIGH_BIT, "ochre_froglight", AXIS_PROP },
    { 0, 180,   HIGH_BIT | 1, "verdant_froglight", AXIS_PROP },
    { 0, 180,   HIGH_BIT | 2, "pearlescent_froglight", AXIS_PROP },
    { 0, 161,       HIGH_BIT, "decorated_pot", TRULY_NO_PROP }, // well, waterlogged
    { 0, 155, HIGH_BIT | 0x4, "calibrated_sculk_sensor", CALIBRATED_SCULK_SENSOR_PROP }, // also power and sculk_sensor_phase, but not needed so not saved
    { 0, 182,       HIGH_BIT, "cherry_button", BUTTON_PROP },
    { 0, 183,       HIGH_BIT, "cherry_door", DOOR_PROP },
    { 0, 184,       HIGH_BIT, "cherry_fence", FENCE_PROP },
    { 0, 185,       HIGH_BIT, "cherry_fence_gate", FENCE_GATE_PROP },
    { 0, 181,   HIGH_BIT | 1, "cherry_leaves", LEAF_PROP },
    { 0, 160,   HIGH_BIT | 1, "cherry_log", AXIS_PROP },
    { 0,   5,              9, "cherry_planks", NO_PROP },
    { 0,  70,          20, "cherry_pressure_plate", PRESSURE_PROP },
    { 0,   6,	           7, "cherry_sapling", SAPLING_PROP },	// put with the other saplings
    { 0, 171, HIGH_BIT | BIT_16, "cherry_sign", STANDING_SIGN_PROP },
    { 0, 126,              6, "cherry_slab", SLAB_PROP },
    { 0, 187,	    HIGH_BIT, "cherry_stairs", STAIRS_PROP },
    { 0, 188,       HIGH_BIT, "cherry_trapdoor", TRAPDOOR_PROP },
    { 0, 172, HIGH_BIT | BIT_8, "cherry_wall_sign", WALL_SIGN_PROP },
    { 0, 160, HIGH_BIT | BIT_16 | 1, "cherry_wood", AXIS_PROP },
    { 0, 167,   HIGH_BIT | 1, "stripped_cherry_log", AXIS_PROP },
    { 0, 168,   HIGH_BIT | 1, "stripped_cherry_wood", AXIS_PROP },
    { 0, BLOCK_FLOWER_POT,        SAPLING_FIELD | 7, "potted_cherry_sapling", NO_PROP },
    { 0, 170,              1, "bamboo_block", AXIS_PROP },
    { 0, 189,       HIGH_BIT, "bamboo_button", BUTTON_PROP },
    { 0, 190,       HIGH_BIT, "bamboo_door", DOOR_PROP },
    { 0, 191,       HIGH_BIT, "bamboo_fence", FENCE_PROP },
    { 0, 192,       HIGH_BIT, "bamboo_fence_gate", FENCE_GATE_PROP },
    { 0,   5,             10, "bamboo_planks", NO_PROP },
    { 0,  70,          22, "bamboo_pressure_plate", PRESSURE_PROP },
    { 0, 171, HIGH_BIT | BIT_32, "bamboo_sign", STANDING_SIGN_PROP },
    { 0, 126,              7, "bamboo_slab", SLAB_PROP },
    { 0, 194,	    HIGH_BIT, "bamboo_stairs", STAIRS_PROP },
    { 0, 195,       HIGH_BIT, "bamboo_trapdoor", TRAPDOOR_PROP },
    { 0, 172, HIGH_BIT | BIT_16, "bamboo_wall_sign", WALL_SIGN_PROP },
    { 0,   5,             11, "bamboo_mosaic", NO_PROP },
    { 0, 105,   HIGH_BIT | 5, "bamboo_mosaic_slab", SLAB_PROP },
    { 0, 196,	    HIGH_BIT, "bamboo_mosaic_stairs", STAIRS_PROP },
    { 0, 170,              2, "stripped_bamboo_block", AXIS_PROP },
    { 0,  47,         BIT_16, "chiseled_bookshelf", NO_PROP },
    { 0, 197,	    HIGH_BIT, "pink_petals", PINK_PETALS_PROP },
    { 0, 198,       HIGH_BIT, "pitcher_crop", PITCHER_CROP_PROP },
    { 0, 175,              6, "pitcher_plant", TALL_FLOWER_PROP },
    { 0, 199,       HIGH_BIT, "sniffer_egg", EGG_PROP }, // hatch property is only one used, 0xC
    { 0, 200,       HIGH_BIT, "suspicious_gravel", NO_PROP },   // dusted property
    { 0, 200,   HIGH_BIT | 4, "suspicious_sand", NO_PROP },   // dusted property
    { 0, 201,       HIGH_BIT, "torchflower_crop", NO_PROP },    // just age
    { 0,  37,              1, "torchflower", NO_PROP },
    { 0, BLOCK_FLOWER_POT,  YELLOW_FLOWER_FIELD | 1, "potted_torchflower", NO_PROP },
    { 0, 202,       HIGH_BIT, "oak_wall_hanging_sign", SWNE_FACING_PROP },
    { 0, 202, HIGH_BIT | (1 << 2), "spruce_wall_hanging_sign", SWNE_FACING_PROP },
    { 0, 202, HIGH_BIT | (2 << 2), "birch_wall_hanging_sign", SWNE_FACING_PROP },
    { 0, 202, HIGH_BIT | (3 << 2), "jungle_wall_hanging_sign", SWNE_FACING_PROP },
    { 0, 202, HIGH_BIT | (4 << 2), "acacia_wall_hanging_sign", SWNE_FACING_PROP },
    { 0, 202, HIGH_BIT | (5 << 2), "dark_oak_wall_hanging_sign", SWNE_FACING_PROP },
    { 0, 202, HIGH_BIT | (6 << 2), "crimson_wall_hanging_sign", SWNE_FACING_PROP },
    { 0, 202, HIGH_BIT | (7 << 2), "warped_wall_hanging_sign", SWNE_FACING_PROP },
    { 0, 202, HIGH_BIT | (8 << 2), "mangrove_wall_hanging_sign", SWNE_FACING_PROP },
    { 0, 202, HIGH_BIT | (9 << 2), "cherry_wall_hanging_sign", SWNE_FACING_PROP },
    { 0, 202, HIGH_BIT | (10 << 2), "bamboo_wall_hanging_sign", SWNE_FACING_PROP },
    { 0, 203,       HIGH_BIT, "oak_hanging_sign", ATTACHED_HANGING_SIGN },
    { 0, 203, HIGH_BIT | BIT_32, "spruce_hanging_sign", ATTACHED_HANGING_SIGN },
    { 0, 204,       HIGH_BIT, "birch_hanging_sign", ATTACHED_HANGING_SIGN },
    { 0, 204, HIGH_BIT | BIT_32, "jungle_hanging_sign", ATTACHED_HANGING_SIGN },
    { 0, 205,       HIGH_BIT, "acacia_hanging_sign", ATTACHED_HANGING_SIGN },
    { 0, 205, HIGH_BIT | BIT_32, "dark_oak_hanging_sign", ATTACHED_HANGING_SIGN },
    { 0, 206,       HIGH_BIT, "crimson_hanging_sign", ATTACHED_HANGING_SIGN },
    { 0, 206, HIGH_BIT | BIT_32, "warped_hanging_sign", ATTACHED_HANGING_SIGN },
    { 0, 207,       HIGH_BIT, "mangrove_hanging_sign", ATTACHED_HANGING_SIGN },
    { 0, 207, HIGH_BIT | BIT_32, "cherry_hanging_sign", ATTACHED_HANGING_SIGN },
    { 0, 208,       HIGH_BIT, "bamboo_hanging_sign", ATTACHED_HANGING_SIGN },
    { 0, 144,        6 << 4, "piglin_wall_head", HEAD_WALL_PROP },
    { 0, 144, 0x80 | 6 << 4, "piglin_head", HEAD_PROP },
    { 0, 209,       HIGH_BIT, "trial_spawner", NO_PROP },
    { 0, 210,       HIGH_BIT, "vault", NO_PROP },
    { 0, 211,       HIGH_BIT, "crafter", CRAFTER_PROP },
    { 0, 212,       HIGH_BIT, "heavy_core", NO_PROP },
    { 0, 132,  HIGH_BIT | 48, "polished_tuff", NO_PROP },
    { 0, 213,       HIGH_BIT, "copper_bulb", BULB_PROP },
    { 0, 213,   HIGH_BIT | 1, "exposed_copper_bulb", BULB_PROP },
    { 0, 213,   HIGH_BIT | 2, "weathered_copper_bulb", BULB_PROP },
    { 0, 213,   HIGH_BIT | 3, "oxidized_copper_bulb", BULB_PROP },
    { 0, 213,   HIGH_BIT | 4, "waxed_copper_bulb", BULB_PROP },
    { 0, 213,   HIGH_BIT | 5, "waxed_exposed_copper_bulb", BULB_PROP },
    { 0, 213,   HIGH_BIT | 6, "waxed_weathered_copper_bulb", BULB_PROP },
    { 0, 213,   HIGH_BIT | 7, "waxed_oxidized_copper_bulb", BULB_PROP },
    { 0, 132,  HIGH_BIT | 49, "chiseled_copper", BULB_PROP },
    { 0, 132,  HIGH_BIT | 50, "exposed_chiseled_copper", BULB_PROP },
    { 0, 132,  HIGH_BIT | 51, "weathered_chiseled_copper", BULB_PROP },
    { 0, 132,  HIGH_BIT | 52, "oxidized_chiseled_copper", BULB_PROP },
    { 0, 132,  HIGH_BIT | 53, "waxed_chiseled_copper", BULB_PROP },
    { 0, 132,  HIGH_BIT | 54, "waxed_exposed_chiseled_copper", BULB_PROP },
    { 0, 132,  HIGH_BIT | 55, "waxed_weathered_chiseled_copper", BULB_PROP },
    { 0, 132,  HIGH_BIT | 56, "waxed_oxidized_chiseled_copper", BULB_PROP },
    { 0, 214,       HIGH_BIT, "copper_grate", BULB_PROP },
    { 0, 214,   HIGH_BIT | 1, "exposed_copper_grate", BULB_PROP },
    { 0, 214,   HIGH_BIT | 2, "weathered_copper_grate", BULB_PROP },
    { 0, 214,   HIGH_BIT | 3, "oxidized_copper_grate", BULB_PROP },
    { 0, 214,   HIGH_BIT | 4, "waxed_copper_grate", BULB_PROP },
    { 0, 214,   HIGH_BIT | 5, "waxed_exposed_copper_grate", BULB_PROP },
    { 0, 214,   HIGH_BIT | 6, "waxed_weathered_copper_grate", BULB_PROP },
    { 0, 214,   HIGH_BIT | 7, "waxed_oxidized_copper_grate", BULB_PROP },
    { 0, 142,   HIGH_BIT | BIT_16 | 4, "tuff_slab", SLAB_PROP },
    { 0, 142,   HIGH_BIT | BIT_16 | 5, "polished_tuff_slab", SLAB_PROP },
    { 0, 142,   HIGH_BIT | BIT_16 | 6, "tuff_brick_slab", SLAB_PROP },
    { 0, 132,  HIGH_BIT | 57, "tuff_bricks", NO_PROP },
    { 0, 132,  HIGH_BIT | 58, "chiseled_tuff", NO_PROP },
    { 0, 132,  HIGH_BIT | 59, "chiseled_tuff_bricks", NO_PROP },
    { 0, 215,	    HIGH_BIT, "tuff_stairs", STAIRS_PROP },
    { 0, 216,	    HIGH_BIT, "polished_tuff_stairs", STAIRS_PROP },
    { 0, 217,	    HIGH_BIT, "tuff_brick_stairs", STAIRS_PROP },
    { 0, 139,	          22, "tuff_wall", WALL_PROP },     // no data values used for walls, it's all implied in Mineways
    { 0, 139,	          23, "polished_tuff_wall", WALL_PROP },
    { 0, 139,	          24, "tuff_brick_wall", WALL_PROP },
    { 0, 218,       HIGH_BIT, "copper_trapdoor", TRAPDOOR_PROP },
    { 0, 219,       HIGH_BIT, "exposed_copper_trapdoor", TRAPDOOR_PROP },
    { 0, 220,       HIGH_BIT, "weathered_copper_trapdoor", TRAPDOOR_PROP },
    { 0, 221,       HIGH_BIT, "oxidized_copper_trapdoor", TRAPDOOR_PROP },
    { 0, 222,       HIGH_BIT, "waxed_copper_trapdoor", TRAPDOOR_PROP },
    { 0, 223,       HIGH_BIT, "waxed_exposed_copper_trapdoor", TRAPDOOR_PROP },
    { 0, 224,       HIGH_BIT, "waxed_weathered_copper_trapdoor", TRAPDOOR_PROP },
    { 0, 225,       HIGH_BIT, "waxed_oxidized_copper_trapdoor", TRAPDOOR_PROP },
    { 0, 226,       HIGH_BIT, "copper_door", DOOR_PROP },
    { 0, 227,       HIGH_BIT, "exposed_copper_door", DOOR_PROP },
    { 0, 228,       HIGH_BIT, "weathered_copper_door", DOOR_PROP },
    { 0, 229,       HIGH_BIT, "oxidized_copper_door", DOOR_PROP },
    { 0, 230,       HIGH_BIT, "waxed_copper_door", DOOR_PROP },
    { 0, 231,       HIGH_BIT, "waxed_exposed_copper_door", DOOR_PROP },
    { 0, 232,       HIGH_BIT, "waxed_weathered_copper_door", DOOR_PROP },
    { 0, 233,       HIGH_BIT, "waxed_oxidized_copper_door", DOOR_PROP },
    { 0,  37,              2, "closed_eyeblossom", NO_PROP },
    { 0,  37,              3, "open_eyeblossom", NO_PROP },
    { 0, 193,       HIGH_BIT, "creaking_heart", CREAKING_HEART_PROP },
    { 0, 102,       HIGH_BIT, "pale_hanging_moss", NO_PROP },   // "tip" is always looked for and add 0x1
    { 0, 132,  HIGH_BIT | 60, "pale_moss_block", NO_PROP },
    { 0, 103,       HIGH_BIT, "pale_moss_carpet", PALE_MOSS_CARPET_PROP },
    { 0,  14,       HIGH_BIT, "pale_oak_button", BUTTON_PROP },
    { 0,  15,       HIGH_BIT, "pale_oak_door", DOOR_PROP },
    { 0,  16,       HIGH_BIT, "pale_oak_fence", FENCE_PROP },
    { 0,  17,       HIGH_BIT, "pale_oak_fence_gate", FENCE_GATE_PROP },
    { 0, 208, HIGH_BIT | BIT_32, "pale_oak_hanging_sign", ATTACHED_HANGING_SIGN },    // atop bamboo_hanging_sign
    { 0, 181,   HIGH_BIT | 2, "pale_oak_leaves", LEAF_PROP },
    { 0, 160,   HIGH_BIT | 2, "pale_oak_log", AXIS_PROP },
    { 0,   5,             12, "pale_oak_planks", NO_PROP },
    { 0,  70,             24, "pale_oak_pressure_plate", PRESSURE_PROP },
    { 0,  37,              4, "pale_oak_sapling", NO_PROP },
    { 0, 171, HIGH_BIT | BIT_32 | BIT_16, "pale_oak_sign", STANDING_SIGN_PROP },
    { 0, 105,   HIGH_BIT | 6, "pale_oak_slab", SLAB_PROP },
    { 0,  18,       HIGH_BIT, "pale_oak_stairs", STAIRS_PROP },
    { 0, 101,       HIGH_BIT, "pale_oak_trapdoor", TRAPDOOR_PROP },
    { 0, 202, HIGH_BIT | (11 << 2), "pale_oak_wall_hanging_sign", SWNE_FACING_PROP },
    { 0, 172, HIGH_BIT | BIT_16 | BIT_8, "pale_oak_wall_sign", WALL_SIGN_PROP },
    { 0, 160, HIGH_BIT | BIT_16 | 2, "pale_oak_wood", AXIS_PROP },
    { 0, BLOCK_FLOWER_POT,  YELLOW_FLOWER_FIELD | 2, "potted_closed_eyeblossom", NO_PROP },
    { 0, BLOCK_FLOWER_POT,  YELLOW_FLOWER_FIELD | 3, "potted_open_eyeblossom", NO_PROP },
    { 0, BLOCK_FLOWER_POT,  YELLOW_FLOWER_FIELD | 4, "potted_pale_oak_sapling", NO_PROP },  // darn, all the sapling spots are filled up!
    { 0, 132,  HIGH_BIT | 61, "chiseled_resin_bricks", NO_PROP },
    { 0, 132,  HIGH_BIT | 62, "resin_block", NO_PROP },
    { 0, 105,   HIGH_BIT | 7, "resin_brick_slab", SLAB_PROP },
    { 0, 173,       HIGH_BIT, "resin_brick_stairs", STAIRS_PROP },
    { 0, 139,	          25, "resin_brick_wall", WALL_PROP },
    { 0, 132,  HIGH_BIT | 63, "resin_bricks", NO_PROP },
    { 0, 186,       HIGH_BIT, "resin_clump", VINE_PROP },
    { 0, 167,   HIGH_BIT | 2, "stripped_pale_oak_log", AXIS_PROP },
    { 0, 168,   HIGH_BIT | 2, "stripped_pale_oak_wood", AXIS_PROP },
    { 0, 197,  HIGH_BIT | 16, "leaf_litter", PINK_PETALS_PROP },
    { 0, 197,  HIGH_BIT | 32, "wildflowers", PINK_PETALS_PROP },
    { 0, 234,       HIGH_BIT, "test_block", NO_PROP },
    { 0,   1,             16, "test_instance_block", NO_PROP },
    { 0,  31,              6, "bush", NO_PROP },
    { 0,  31,              7, "cactus_flower", NO_PROP },
    { 0,  31,              8, "short_dry_grass", NO_PROP },
    { 0,  31,              9, "tall_dry_grass", NO_PROP },
    { 0,  31,             10, "firefly_bush", NO_PROP },
    { 0, 235,       HIGH_BIT, "dried_ghast", GHAST_PROP },

    // 1.20.3 additions (short_grass added next to "grass", above), https://minecraft.wiki/w/Java_Edition_1.20.3#General_2

 // Note: 140, 144 are reserved for the extra bit needed for BLOCK_FLOWER_POT and BLOCK_HEAD, so don't use these HIGH_BIT values
};


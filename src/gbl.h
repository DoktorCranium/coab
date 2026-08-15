/* gbl.h - the engine's global state. Ported from Classes/Gbl.cs.
 *
 * Gbl.cs is a bag of ~500 static fields, which is what the DOS binary's data
 * segment decompiled into. Keeping it a single global struct preserves that
 * shape and keeps the translated overlay code readable next to its C# original.
 *
 * Only the fields the ported subsystems actually use are declared so far; the
 * struct grows as more of engine/ is translated.
 */
#ifndef COAB_GBL_H
#define COAB_GBL_H

#include "coab.h"
#include "area.h"
#include "combat.h"
#include "dax.h"
#include "ecl.h"
#include "enums.h"
#include "geo.h"
#include "resttime.h"

/* Classes/Gbl.cs: MenuColorSet. The three colours a prompt is drawn in: the
 * highlighted word, the rest of the menu text, and the line of prompt text in
 * front of it. */
typedef struct {
    int highlight;
    int foreground;
    int prompt;
} MenuColorSet;

/* Classes/Gbl.cs: enum SoundType */
typedef enum {
    SOUND_TYPE_PC = 0,
    SOUND_TYPE_NONE = 1
} SoundType;

#define GBL_FONT_GLYPHS   177   /* dax_8x8d1_201 holds 177 8-byte glyphs */
#define GBL_SYMBOL_SETS   5

/* engine/seg001.cs: new CombatIcon[26]. Ids 0..0x0b are the loaded monster and
 * party sprites, 0x19 is the one COMSPR entry loaded by hand at startup. */
#define GBL_COMBAT_ICON_COUNT 26

/* engine/seg001.cs: new DaxBlock(0, 0x30, 3, 0x18) - 0x30 cells of 24x24
 * isometric tile, three 8-pixel columns wide and 24 rows high. */
#define GBL_24X24_CELLS  0x30
#define GBL_24X24_WIDTH  3
#define GBL_24X24_HEIGHT 0x18

/* The DAX basename the animated picture was last loaded from ("PIC", "FINAL",
 * "HEAD", ... - the chapter number is appended before the load). Longer than
 * anything the game builds; the C# held a string. */
#define GBL_DAX_NAME_MAX 32

/* Classes/Gbl.cs: SetBlock. Which WALLDEF block each of the three wall sets
 * holds; a set that has never been loaded has -1 for both, which is what the C#
 * constructor and Reset() left behind. */
typedef struct {
    int set_id;                       /* byte_1D53C[di] */
    int block_id;                     /* byte_1D53A[di] */
} SetBlock;

#define GBL_SET_BLOCKS 3

/* Classes/Gbl.cs: player_array is 256 entries and MaxCombatantCount is 0xff.
 * Both are indexed from 1 - "God damm 1-n arrays", as engine/seg001.cs puts it -
 * so the arrays hold one more entry than there can be combatants. */
#define GBL_PLAYER_ARRAY        256
#define GBL_MAX_COMBATANT_COUNT 0xff

/* gbl.TeamList was a List<Player>: the party first, then every monster an
 * encounter has loaded. engine/ovr017.cs allows eight party members (its
 * icon_slot array) and engine/ovr003.cs stops loading monsters at 63, so 80
 * entries is the worst case with room to spare. */
#define GBL_TEAM_LIST_MAX 80

/* How many items may be lying on the ground at once - see ground_items. */
#define GBL_GROUND_ITEMS_MAX 256

/* unk_1AE24. One flag per character, sized as the original sized it: 0x48
 * entries, which is nine more than the largest team the game can build. See
 * affects_timed_out. */
#define GBL_AFFECTS_TIMED_OUT 0x48

/* How many gas clouds one fight may have hanging over it. The original list was
 * unbounded; a caster adds one per cast and only four squares' worth of cloud
 * fits around a target, so this is far more than a fight ever holds. */
#define GBL_GAS_CLOUD_MAX 32

/* How many combatants one spell may be aimed at - see spell_targets. An area
 * spell takes in everyone it reaches, so the limit is the number of combatants a
 * fight can hold. */
#define GBL_SPELL_TARGETS_MAX GBL_MAX_COMBATANT_COUNT

/* Classes/Gbl.cs: combat_round_no_action_value, a const of 15. How many rounds
 * of nobody doing anything a fight puts up with. */
#define GBL_COMBAT_ROUND_NO_ACTION_VALUE 15

/* gbl.unk_1D972. The strings the instruction being run has in hand: an ECL
 * instruction carries up to a handful of them, either unpacked out of the script
 * or copied out of the machine's memory, and the handlers read them back by the
 * position they arrived in. Fifteen slots, as the original had, indexed from 1
 * because vm_load_cmd_sets numbers them as it numbers operands.
 *
 * The length is this port's own: the C# held a string of any size, and the DOS
 * build unpacked into a fixed buffer whose size the disassembly does not pin
 * down. 0x100 is comfortably past the longest text the scripts hold - a screen
 * of prompt text is 40 by 8 - and an over-long string is truncated with a
 * warning rather than run off the end. */
#define GBL_ECL_STRINGS    16
#define GBL_ECL_STRING_MAX 0x100

/* gbl.vmCallStack, dword_1D91A. Where each Gosub means to return to. An
 * unbounded Stack<ushort> in the C#; the scripts nest a handful deep, so this is
 * far more than any of them uses and overflow is logged rather than silently
 * dropping a return address. */
#define GBL_VM_CALL_STACK_MAX 64

/* gbl.compare_flags. The six ways the last comparison could have gone, in the
 * order the conditional jumps index them: equal, not equal, below, above, below
 * or equal, above or equal. */
#define GBL_COMPARE_FLAGS 6

typedef struct {
    /* --- text cursor, in 8x8 cells --- */
    int  text_x_col;                  /* gbl.textXCol */
    int  text_y_col;                  /* gbl.textYCol */

    /* --- presentation options --- */
    SoundType sound_type;             /* gbl.soundType, defaults to None */
    bool pics_on;                     /* gbl.PicsOn */
    bool animations_on;               /* gbl.AnimationsOn */

    /* --- pacing --- */
    int  game_speed_var;              /* gbl.game_speed_var; 4 normally, 9 in demo */
    bool delay_between_characters;    /* gbl.DelayBetweenCharacters */

    /* --- current chapter, selects which numbered DAX files are used --- */
    u8   game_area;                   /* gbl.game_area */
    u8   game_area_backup;            /* gbl.game_area_backup */

    bool in_demo;                     /* gbl.inDemo */
    bool print_commands;              /* gbl.printCommands */

    /* --- the 8x8 text font: 177 glyphs of 8 rows --- */
    u8   dax_8x8d1_201[GBL_FONT_GLYPHS][8];
    u8   mono_char_data[8];           /* scratch glyph handed to DisplayMono8x8 */

    /* --- 8x8 tile/symbol banks loaded from 8X8D<area>.DAX --- */
    DaxBlock *symbol_8x8_set[GBL_SYMBOL_SETS];
    DaxBlock *cursor_bkup;            /* 1x8 scratch picture */
    DaxBlock *cursor;

    /* --- the isometric 24x24 tile bank the wilderness map is drawn from --- */
    DaxBlock *dax_24x24_set;          /* gbl.dax24x24Set, dword_1C8F8 */
    /* gbl.dword_1C8FC. DrawIsoTile reaches for this for tile ids over 0x7f;
     * nothing in the game ever fills it in, so those ids draw nothing. Kept
     * because the id test is what stops them drawing the wrong tile. */
    DaxBlock *dword_1C8FC;

    /* --- the loaded combat sprites --- */
    CombatIcon combat_icons[GBL_COMBAT_ICON_COUNT];

    /* --- where the party is: the two halves of the saved area record. Both are
     * allocated once by gbl_init and live for the whole run, as the C#'s
     * `new Area1()` / `new Area2()` in InitFirst did. --- */
    Area1 *area_ptr;                  /* gbl.area_ptr */
    Area2 *area2_ptr;                 /* gbl.area2_ptr */

    GameState game_state;             /* gbl.game_state */
    GameState last_game_state;        /* gbl.last_game_state, byte_1B2E4 */

    /* --- the picture shown in the top-left corner of a text screen. Up to
     * eight animation frames, cycled by the prompt loop while it waits. --- */
    DaxArray pic_frames;              /* gbl.byte_1D556 */
    char last_dax_file[GBL_DAX_NAME_MAX];  /* gbl.lastDaxFile */
    u8   last_dax_block_id;           /* gbl.lastDaxBlockId, byte_1D5B4 */

    /* gbl.byte_1D5AB and gbl.byte_1D5B5. The two above, remembered across
     * camping: ovr016 saves them before it takes the screen and puts the picture
     * back from them afterwards. The interpreter clears them whenever it starts a
     * script over, so a camp interrupted in one map cannot restore the last
     * picture of another. */
    char saved_dax_file[GBL_DAX_NAME_MAX];
    u8   saved_dax_block_id;

    /* --- the full-screen picture behind the wilderness map --- */
    DaxBlock *bigpic_dax;             /* gbl.bigpic_dax, word_1D5B6 */
    u8   bigpic_block_id;             /* gbl.bigpic_block_id, byte_1D5BA */
    bool can_draw_bigpic;             /* gbl.can_draw_bigpic, byte_1D8AA */

    /* --- the talking-head portrait: a head and an optional body --- */
    u8   current_head_id;             /* gbl.current_head_id, 0xff = none */
    DaxBlock *head_dax;               /* gbl.headX_dax */
    u8   current_body_id;             /* gbl.current_body_id */
    DaxBlock *body_dax;               /* gbl.bodyX_dax */

    /* --- menu prompt state (ovr027) --- */
    int  menu_selected_word;          /* gbl.menuSelectedWord, byte_1D5BE */
    int  menu_screen_index;           /* gbl.menuScreenIndex, first visible row */
    /* gbl.displayInput_specialKeyPressed, byte_1D5BF. displayInput reports this
     * both through an out-parameter and here; some callers read it later. */
    bool display_input_special_key_pressed;

    /* --- menu prompt timeout (ovr027.displayInput) --- */
    int  display_input_seconds_to_wait;
    char display_input_timeout_value;

    /* --- the 3D dungeon view (ovr031, ovr029) --- */
    GeoBlock *geo_ptr;                /* gbl.geo_ptr, the loaded 16x16 map */
    WallDefs  wall_def;               /* gbl.wallDef, three sets of wall tiles */
    SetBlock  set_blocks[GBL_SET_BLOCKS];   /* gbl.setBlocks */

    int  map_pos_x;                   /* gbl.mapPosX, byte_1D539; 0 is map left */
    int  map_pos_y;                   /* gbl.mapPosY, byte_1D53A; 0 is map top */
    u8   map_direction;               /* gbl.mapDirection: 0 N, 2 E, 4 S, 6 W */
    u8   map_wall_type;               /* gbl.mapWallType, byte_1D53C */
    u8   map_wall_roof;               /* gbl.mapWallRoof, byte_1D53D */
    bool map_area_display;            /* gbl.mapAreaDisplay: the overhead map */

    /* gbl.can_bash_door, can_pick_door and can_knock_door. One try at each per
     * square: dungeon.c clears the one that was used, and walking into the next
     * square sets all three again. A door that would not open therefore stays
     * shut until the party walks away and comes back. */
    bool can_bash_door;
    bool can_pick_door;
    bool can_knock_door;

    /* gbl.positionChanged, byte_1EE92. Set whenever the party's square or facing
     * moves, whether they walked or a script put them there; the movement loop
     * redraws the view and re-runs the map's search script when it sees it. */
    bool position_changed;

    int  sky_colour;                  /* gbl.sky_colour, byte_1D534 */
    DaxBlock *sky_dax_250;            /* the moon */
    DaxBlock *sky_dax_251;            /* the sun */
    DaxBlock *sky_dax_252;            /* the ground in front of the party */

    u8   ecl_block_id;                /* gbl.EclBlockId */
    bool party_killed;                /* gbl.party_killed, byte_1B2F0 */

    /* --- the ECL script interpreter (ovr008, ovr003) ---
     *
     * ECL is the game's own bytecode: every map, encounter and conversation is a
     * script. Both blocks are allocated once by gbl_init, as the C#'s `new` in
     * seg001.InitFirst did, and loading a saved game overwrites them in place
     * rather than replacing them the way ovr017 did. */
    EclBlock *ecl_ptr;                /* gbl.ecl_ptr: the script and its data */
    EclVars  *ecl_vars;               /* gbl.stru_1B2CA: its scratch memory */

    /* gbl.ecl_offset. Where the interpreter is in the script. Biased by 0x8000,
     * which is where the DOS build mapped the block, so it wraps on the way back
     * down to a block offset - see ecl.h. */
    u16  ecl_offset;

    /* gbl.cmd_opps. The operands of the instruction being run, numbered from 1;
     * entry 0 goes unused. */
    EclOp cmd_opps[ECL_CMD_OPS_LIMIT];

    /* gbl.unk_1D972. The strings that instruction carries, also numbered from 1. */
    char ecl_strings[GBL_ECL_STRINGS][GBL_ECL_STRING_MAX];

    /* gbl.vmCallStack. Gosub's return addresses. */
    u16  vm_call_stack[GBL_VM_CALL_STACK_MAX];
    int  vm_call_depth;

    /* gbl.compare_flags. How the last Compare went; the conditional jumps pick
     * one of the six. */
    bool compare_flags[GBL_COMPARE_FLAGS];

    /* gbl.command. The opcode being run. It is here rather than a local because
     * six of the handlers cover more than one opcode and read it back to find
     * out which one they are - Add and Subtract are the same routine, and so are
     * If = through If >=. */
    u8   command;

    /* gbl.stopVM, byte_1B2ED. Set by Exit and by NewECL to end the interpreter's
     * loop; RunEclVm clears it again on the way out, so a script that ends does
     * not stop the next one before it starts. */
    bool stop_vm;

    /* gbl.vmFlag01, byte_1B2EE. "The script was replaced": NewECL sets it, and
     * every caller of RunEclVm tests it to find out that the block it was running
     * is gone and its own idea of where to go next is meaningless. */
    bool vm_flag01;

    /* gbl.restore_player_ptr, byte_1AB0A. Load Character moved the selection, so
     * Exit has to put it back to whoever the player had chosen. */
    bool restore_player_ptr;

    /* gbl.sprite_block_id and gbl.pic_block_id, byte_1EE73 and byte_1EE74. The
     * art the encounter in progress is drawn from; kept because Approach and the
     * encounter menu redraw it at each new distance without being told which
     * blocks again. */
    u8   sprite_block_id;
    u8   pic_block_id;

    /* gbl.numLoadedMonsters, byte_1EE75, and gbl.monstersLoaded, byte_1EE71. How
     * many monsters the script has loaded - 63 is the most a fight holds - and
     * whether there are any, which is what tells Combat to start a fight rather
     * than go straight to the treasure. */
    int  num_loaded_monsters;
    bool monsters_loaded;

    /* gbl.bottomTextHasBeenCleared, byte_1EE79. Cleared by every instruction that
     * writes to the text area, so that whatever runs next knows to clear it. */
    bool bottom_text_has_been_cleared;

    /* gbl.filesLoaded, byte_1AB09, and gbl.byte_1AB0B / gbl.byte_1AB0C. Which of
     * Load Files and Load Pieces the script has run: between them they say
     * whether the map, the wall sets and the party summary are up to date. */
    bool files_loaded;
    bool byte_1AB0B;
    bool byte_1AB0C;

    /* gbl.search_flag_bkup, word_1AB0D. The area's search flags while a search
     * script runs with them forced to 1, so that the script cannot see the search
     * it is itself part of. */
    int  search_flag_bkup;

    /* gbl.gameSaved, byte_1B2EF, and gbl.gameWon, byte_1B2F1. */
    bool game_saved;
    bool game_won;

    /* gbl.silent_training and gbl.can_train_no_more. A newly created character is
     * trained up from first level with no screen and no questions asked:
     * ovr017.SilentTrainPlayer turns silent_training on and calls
     * partymenu_train_player over and over until it sets can_train_no_more,
     * which is how the training code says the character has run out of
     * experience. Nothing else reads either. */
    bool silent_training;
    bool can_train_no_more;

    /* gbl.import_from. Which game's saved characters the Add Character list is
     * built from - this one, Pool of Radiance or Hillsfar. */
    ImportSource import_from;

    /* gbl.encounter_flags, byte_1EE72. What the encounter about to start has
     * already put on screen: [0] its sprite is loaded, [1] its picture is drawn.
     * vm_show_encounter_art keeps them, so that walking towards a monster does
     * not reload the art every step. */
    bool encounter_flags[2];

    /* gbl.monster_icon_id, byte_1D92D. Which combat icon slot the next monster
     * loaded gets; vm_init_ecl starts it at 8, above the party's own eight. */
    u8   monster_icon_id;

    /* The five entry points every script declares in its header, in the order
     * vm_init_ecl reads them (word_1B2D3 to word_1B2DB). */
    u16  vm_run_addr_1;               /* gbl.vm_run_addr_1: the movement handler */
    u16  search_location_addr;        /* gbl.SearchLocationAddr */
    u16  pre_camp_check_addr;         /* gbl.PreCampCheckAddr */
    u16  camp_interrupted_addr;       /* gbl.CampInterruptedAddr */
    u16  ecl_initial_entry_point;     /* gbl.ecl_initial_entryPoint */

    /* gbl.reload_ecl_and_pictures, byte_1B2EB. Set while the game is picking a
     * script back up - a saved game, or coming out of a fight - which is what
     * stops vm_init_ecl resetting the area records over the restored ones. */
    bool reload_ecl_and_pictures;

    /* gbl.player_not_found, byte_1EE97. Set when a script asked for a character
     * who is not in the party; the next read of the "is this one in combat" cell
     * answers 0 for it and clears this again. */
    bool player_not_found;

    /* gbl.spriteChanged, byte_1EE8C, and gbl.displayPlayerSprite, byte_1EE8F.
     * Whether the encounter art on screen has been replaced, and whether the
     * dungeon view has a monster sprite standing in it. */
    bool sprite_changed;
    bool display_player_sprite;

    /* gbl.redrawPartySummary1 and 2, byte_1EE7C and byte_1EE7D. A script wrote
     * something the party summary shows, so it needs drawing again. */
    bool redraw_party_summary1;
    bool redraw_party_summary2;

    /* gbl.byte_1DA70, byte_1EE8D, byte_1EE91, byte_1EE94, byte_1EE95 and
     * byte_1EE96. Flags the interpreter keeps for code that is not translated
     * yet, so they are set and cleared where the original did and read nowhere:
     * 1EE8D goes with the portrait, 1EE91 and 1EE94 mark the view and the area
     * record dirty, 1EE95 suppresses the encounter picture, and 1EE96 remembers
     * which head was drawn last. */
    bool byte_1DA70;
    bool byte_1EE8D;
    bool byte_1EE91;
    bool byte_1EE94;
    bool byte_1EE95;
    u8   byte_1EE96;

    /* gbl.word_1D914, word_1D916 and word_1D918. Three cells of the machine's
     * memory that live in the data segment rather than in the area record; the
     * scripts use them as scratch across a Gosub. */
    i16  word_1D914;
    i16  word_1D916;
    i16  word_1D918;

    /* gbl.word_1EE76, word_1EE78 and word_1EE7A. Three more, written by a script
     * and read by overlays that are not translated yet. */
    u16  word_1EE76;
    u16  word_1EE78;
    u16  word_1EE7A;

    /* --- a fight in progress (ovr033) --- */
    /* gbl.player_array. Everyone a fight can involve, indexed from 1: the
     * party, then the monsters. Index 0 is "nobody", which is why so much of the
     * combat code treats a player index of 0 as a miss. The Players themselves
     * are owned by the combat setup code. */
    Player *player_array[GBL_PLAYER_ARRAY];
    int  combatant_count;             /* gbl.CombatantCount */
    /* gbl.CombatMap, stru_1C9CD. Where each combatant stands; entry 0 is unused
     * and has size 0, so "no combatant" reads as off the map. */
    CombatantMap combat_map[GBL_MAX_COMBATANT_COUNT + 1];

    /* gbl.mapToBackGroundTile, stru_1D1BC. NULL outside a fight - ovr009 clears
     * it and ovr011 allocates it - and the combat code tests for that. */
    GroundTileMap *map_to_background_tile;

    /* gbl.downedPlayers, unk_1D183. A List<> in the C#; the array is sized for
     * the worst case of every combatant falling. */
    DownedPlayerTile downed_players[GBL_MAX_COMBATANT_COUNT + 1];
    int  downed_player_count;

    bool focus_combat_area_on_player; /* gbl.focusCombatAreaOnPlayer, byte_1D910 */

    /* gbl.byte_1D90E. Set while the monster AI has a target it can actually
     * reach and hit this round, which is what says the attack should be animated
     * and the window scrolled to it. Only engine/ovr010.cs reads or writes it. */
    bool byte_1D90E;

    /* --- setting a fight up (ovr011) --- */

    CombatType combat_type;           /* gbl.combat_type */
    /* gbl.AutoPCsCastMagic, byte_1D904 magicOn. Whether the party's own
     * spellcasters are left to the AI. Cleared as a fight begins. */
    bool auto_pcs_cast_magic;

    /* gbl.team_start_x and gbl.team_start_y, byte_1AD2C and byte_1AD2E. Where
     * each side's block of combatants is anchored, as an offset from the party's
     * own square: our side at 0,0 and the other side an encounter's distance
     * away in the direction the party is facing. */
    int  team_start_x[2];
    int  team_start_y[2];
    /* gbl.half_team_count, unk_1AD30. Half of each side's strength, rounded up,
     * which is how wide the first row a side is placed in may grow. */
    int  half_team_count[2];
    /* gbl.team_direction, byte_1AD32. The side's facing in quarter turns, 0 N to
     * 3 W: the two sides face each other. */
    int  team_direction[2];
    /* gbl.currentTeam, field_197. Which side is being placed just now. */
    int  current_team;

    /* gbl.byte_1AD34 and gbl.byte_1AD35. Which dungeon square the combat floor
     * is being built out of, as an offset from the party's own square - thirteen
     * across by five deep. set_background_tile reaches for these rather than
     * taking them, so the whole tile-building family is driven from here. */
    int  byte_1AD34;
    int  byte_1AD35;

    /* gbl.dir_0_flags, dir_6_flags, dir_2_flags and dir_4_flags, byte_1AD36 to
     * byte_1AD39. What stands in each of the four compass directions of the
     * square being built: 0 nothing, 1 a wall, 3 a door. */
    int  dir_0_flags;
    int  dir_6_flags;
    int  dir_2_flags;
    int  dir_4_flags;

    /* gbl.byte_1AD3D. The square's furnished bit, get_wall_x2 & 0x40, which is
     * what lets a table and its chairs be dropped into it. */
    u8   byte_1AD3D;

    /* gbl.current_city. Which city's terrain mix a wilderness floor is drawn
     * from; ovr011 copies it out of the area record before building one. */
    u8   current_city;

    /* --- the blow being struck or the spell going off (ovr024) ---
     *
     * All of these are handed between the attack code and the affect handlers
     * rather than passed as arguments, exactly as the DOS build's data segment
     * did. */
    int  attack_roll;                 /* gbl.attack_roll, byte_1D2C9; 100 on a 20 */
    int  saving_throw_roll;           /* gbl.savingThrowRoll */
    bool saving_throw_made;           /* gbl.savingThrowMade */
    int  save_verse_type;             /* gbl.saveVerseType, byte_1D2D1 */
    int  dice_count;                  /* gbl.dice_count, byte_1D2C2 */
    int  damage;                      /* gbl.damage, byte_1D2BE */
    int  damage_flags;                /* gbl.damage_flags, byte_1D2BF; DAMAGE_* */
    int  current_affect;              /* gbl.current_affect, byte_1D2BD, Affects */
    int  spell_id;                    /* gbl.spell_id, byte_1D2C1; 0 is no spell */

    /* gbl.spell_target. Who the blow or the spell being resolved is aimed at.
     * Several affect handlers set it from the attacker's action before they read
     * it, which is the original's way of passing the target on to whatever runs
     * next. A borrowed pointer, like everything else that names a combatant. */
    Player *spell_target;

    /* gbl.targetInvisible, byte_1D2C5. Set by the affects that hide their owner,
     * and read by the attack code to decide whether the swing may be made at
     * all. */
    bool target_invisible;

    /* gbl.cureSpell, byte_1D2C6. Set while a cure is being applied: the affect
     * handlers that would normally renew themselves - poison damage, disease,
     * regeneration - see it and let themselves expire instead. */
    bool cure_spell;

    /* gbl.byte_1D2C7. Cleared everywhere but one place in the mirror image
     * handler, which is the only reader: it stops an image being lost twice to
     * the same blow. What sets it is engine/ovr014.cs's business. */
    bool byte_1D2C7;
    /* gbl.byte_1D2C8. Set when a readied item of the wrong alignment has just
     * burnt its wearer (engine/ovr020.cs's calc_items_effects). */
    bool byte_1D2C8;

    /* gbl.bytes_1D2C9, byte_1D2CA and byte_1D2CB. How many times attack 1 and
     * attack 2 have landed this round; entry 0 is unused, being gbl.attack_roll's
     * own byte in the DOS data segment. engine/ovr014.cs counts them and the
     * engulf and dodge handlers read them. */
    u8   attack_hit_count[3];

    /* gbl.bytes_1D900, byte_1D901 and byte_1D902. How many swings attack 1 and
     * attack 2 have taken this round, landed or not; entry 0 goes unused as it
     * does in attack_hit_count. engine/ovr014.cs counts them, and what is left of
     * a stack of thrown weapons is set from entry 1. */
    u8   attack_made_count[3];

    int  half_actions_left;           /* gbl.halfActionsLeft, byte_1D2C0 */
    /* gbl.resetMovesLeft, byte_1D2C4. Whether an affect that stops a combatant
     * moving also ends their turn. */
    bool reset_moves_left;

    int  monster_morale;              /* gbl.monster_morale, byte_1D2CC */
    int  combat_round;                /* gbl.combat_round, byte_1D8B7 */

    /* gbl.combat_round_no_action_limit, byte_1D8B8. The round the fight stops
     * waiting for somebody to do something and calls it a draw. Every attack
     * pushes it out again by GBL_COMBAT_ROUND_NO_ACTION_VALUE rounds. */
    int  combat_round_no_action_limit;

    /* gbl.enemyHealthPercentage, byte_1D903. How much of the other side is still
     * standing, in multiples of five, which is what the monsters' morale is
     * measured against. */
    int  enemy_health_percentage;

    /* gbl.targetPos, byte_1D883 and byte_1D884. The map square the spell being
     * aimed is aimed at. Not always where a combatant stands: a cloud or a wall
     * of fire can be dropped on empty ground. */
    Point target_pos;

    /* gbl.spellTargets, sp_target. Everyone the spell about to go off will touch,
     * which for an area spell is everyone it reaches rather than the one square
     * that was aimed at. A List<Player> in the C#; borrowed pointers, as
     * everything naming a combatant is. */
    Player *spell_targets[GBL_SPELL_TARGETS_MAX];
    int  spell_target_count;

    /* gbl.item_ptr. The item the game has in hand: what a shop is selling, what
     * is being readied, or - engine/ovr014.cs - the stack a thrown weapon was
     * split off from once the last of it has been thrown. It points into
     * ground_items or into a character's pack, so it goes stale as soon as either
     * closes up over a gap. */
    Item *item_ptr;

    /* gbl.currentScroll, dword_1D5C6. The scroll a spell is being read from or
     * scribed off, set while the spell list on it is on screen. Borrowed, and it
     * points into a character's pack, so it goes stale the moment the pack closes
     * up over a gap. */
    Item *current_scroll;

    /* gbl.tradeWith, player_ptr01. Who the last trade was with: the character
     * selection starts on them the next time, so handing several things to the
     * same person is one keypress each. Borrowed. */
    Player *trade_with;

    /* gbl.applyItemAffect, byte_1D8AC. Set for one call: it sends the affect
     * table to its item handler whatever affect it was asked for, which is how a
     * magic item's own affect is hung on its wearer. The handler clears it
     * again. */
    bool apply_item_affect;

    /* gbl.StinkingCloud, stru_1D885. The stinking clouds hanging over the combat
     * map: one entry per cast, each covering up to four squares. A List<GasCloud>
     * in the C#; every caster can leave several behind, and they last until the
     * fight ends, so the array is sized well above anything a fight reaches and
     * overflow is logged rather than dropped silently. */
    GasCloud stinking_cloud[GBL_GAS_CLOUD_MAX];
    int  stinking_cloud_count;

    /* gbl.CloudKillCloud, stru_1D889. The same for cloudkill, which covers nine
     * squares a cast rather than four. */
    GasCloud cloud_kill_cloud[GBL_GAS_CLOUD_MAX];
    int  cloud_kill_count;

    /* --- the party and the monsters it is up against (ovr025) ---
     *
     * gbl.TeamList, player_next_ptr: a linked list in the DOS build and a
     * List<Player> in the C#. The Players are owned by the party and monster
     * loading code; these are borrowed pointers, as in gbl.player_array. */
    Player *team_list[GBL_TEAM_LIST_MAX];
    int  team_count;

    Player *selected_player;          /* gbl.SelectedPlayer, player_ptr */
    Player *last_selected_player;     /* gbl.LastSelectedPlayer, player_ptr2 */

    /* gbl.lastSelectetSpellTarget, dword_1D87F. Who a spell aimed at one party
     * member outside a fight was last aimed at: the selection starts on them the
     * next time. Cleared when the magic menu opens (engine/ovr016.cs) and by
     * spellcast_setup_spells. Borrowed, like everything naming a character. */
    Player *last_selected_spell_target;

    /* --- the party's shared holdings and what is lying on the ground (ovr022) */

    /* gbl.pooled_money. The heap everybody's coin goes into when the party pools
     * it, and where an encounter's treasure and anything too heavy to carry ends
     * up. It is not a character's purse, so nothing weighs it. */
    MoneySet pooled_money;

    /* gbl.items_pointer. The items on the ground where the party is standing: an
     * encounter's treasure, a shop's stock, and after a wipe the whole party's
     * packs. A List<Item> in the C#, and these are copies rather than borrowed
     * pointers because that is what it held - engine/ovr006.cs clones a dead
     * character's items onto it, and the character is freed afterwards.
     *
     * The worst case the game can build is every fallen combatant's pack, which
     * engine/ovr003.cs caps at 63 monsters plus 8 party members of 16 items
     * each. That is far more than any real encounter drops, so the array is
     * sized for a generous fight rather than for the arithmetic limit, and
     * overflow is logged. */
    Item ground_items[GBL_GROUND_ITEMS_MAX];
    int  ground_item_count;

    int  friends_count;               /* gbl.friends_count */
    int  foe_count;                   /* gbl.foe_count */

    /* --- how the last fight went (ovr006) --- */

    /* gbl.byte_1AB14. Whether the treasure being handed out came out of a fight:
     * engine/ovr006.cs sets it for every enemy it goes through, and what is on
     * the ground with it still clear is a script's gift rather than a hoard, so
     * the screen says "The party has found Treasure!" instead of who won. */
    bool byte_1AB14;

    /* gbl.battleWon, byte_1EE86, and gbl.party_fled. Set together by
     * ovr006.CleanupPlayersStateAfterCombat, along with party_killed above:
     * somebody of ours is still standing, and somebody of ours ran. A fight can
     * be won and fled from in that order, so the win clears the flight. */
    bool battle_won;
    bool party_fled;

    /* gbl.partyAnimatedCount, byte_1EE81. How many of the party are not taking a
     * share of the experience - the fallen, and the animated - which is what the
     * fight's worth is divided by. */
    int  party_animated_count;

    /* gbl.exp_to_add. What the last fight was worth to one character, kept
     * between working it out and printing it on the results screen. */
    int  exp_to_add;

    /* gbl.display_hitpoints_ac, byte_1D90F. Set when the combat side panel is
     * out of date; CombatDisplayPlayerSummary redraws it and clears this. */
    bool display_hitpoints_ac;
    /* gbl.displayPlayerStatusLine18, byte_1D8A8. Moves the out-of-combat player
     * status line down one row, for the screens that have a menu above it. */
    bool display_player_status_line18;

    /* --- the game clock and resting (ovr021) --- */

    /* gbl.timeToRest, unk_1D890. How much longer the party means to rest, in the
     * same seven slots as the clock itself; the camp screen fills it in and the
     * resting loop counts it down. */
    RestTime time_to_rest;

    /* gbl.affects_timed_out, unk_1AE24. One flag per team member, set when that
     * character has an affect still ticking. It is what lets the clock skip a
     * party nobody has cast anything on: stepping time only walks the affect
     * lists of the characters whose flag is set, and clears the flag again once
     * the last of their affects has run out. */
    bool affects_timed_out[GBL_AFFECTS_TIMED_OUT];

    /* gbl.rest_10_seconds, word_1D8A6. Counts the five-minute steps the resting
     * loop takes; the party heals a hit point every 8 * 36 of them, which is a
     * day. */
    int  rest_10_seconds;

    /* gbl.rest_incounter_count. Five-minute steps since the last check for a
     * wandering monster, against Area2.rest_encounter_period. */
    i16  rest_encounter_count;

    /* gbl.missile_dax. Four cells of 24x24 holding the frames of the missile or
     * spell effect being animated: the sprite as loaded, mirrored, and the same
     * two for its attack pose. engine/ovr011.cs allocates it when a fight starts
     * and engine/ovr009.cs drops it, so it is NULL the rest of the time. */
    DaxBlock *missile_dax;

    /* gbl.SpellCastFunction, a spellDelegate. How a spell finds what it touches,
     * which is not the same question inside a fight as out of one:
     * engine/ovr009.cs points this at ovr014.target when a battle starts and
     * back at ovr023.NonCombatSpellCast when it ends. Only ovr023 reads it, so
     * combatloop.c is the only thing that sets it until that overlay is
     * translated. */
    bool (*spell_cast_function)(QuickFight quick_fight, int spell_id);

    bool spell_from_item;             /* gbl.spell_from_item, byte_1D88D */
    bool redraw_boarder;              /* gbl.redrawBoarder, byte_1EE7E */

    u8   head_block_id;               /* gbl.head_block_id, byte_1B2EE */
    u8   body_block_id;               /* gbl.body_block_id, byte_1B2EF */

    /* gbl.byte_1EE98. Cleared every time the dungeon view is redrawn; ovr018
     * sets it, and what it means there is not yet ported. */
    bool byte_1EE98;
} Gbl;

extern Gbl gbl;

/* Base symbol id of each 8x8 bank; Put8x8Symbol subtracts these to get an
 * index into the bank (Classes/Gbl.cs: symbol_set_fix). */
extern const i16 GBL_SYMBOL_SET_FIX[GBL_SYMBOL_SETS];

/* Classes/Gbl.cs: MapDirectionXDelta / MapDirectionYDelta, unk_189A6 and
 * unk_189AF. One step in each of the eight compass directions, plus a ninth
 * entry of 0,0 for "no direction". */
extern const i8 GBL_MAP_DIR_X_DELTA[9];
extern const i8 GBL_MAP_DIR_Y_DELTA[9];

/* One step in `direction`, as a Point (gbl.MapDirectionDelta). */
Point gbl_map_direction_delta(int direction);

/* Classes/Gbl.cs: max_class_hit_dice, byte_1A1CB. The level at which each class
 * stops rolling hit dice and stops earning the constitution bonus, indexed by
 * SkillType. */
extern const u8 GBL_MAX_CLASS_HIT_DICE[SKILL_COUNT];

extern const MenuColorSet GBL_DEFAULT_MENU_COLORS;
extern const MenuColorSet GBL_ALERT_MENU_COLORS;

/* The six palette entries a character's icon colours replace (unk_1A1D3). Each
 * of Player.icon_colours holds two nibbles: the low one recolours the entry,
 * the high one the bright version eight entries along. */
#define GBL_ICON_COLOUR_COUNT 6
extern const u8 GBL_DEFAULT_ICON_COLOURS[GBL_ICON_COLOUR_COUNT];

void gbl_init(void);
void gbl_free(void);

/* gbl.TeamList.IndexOf: -1 when the character is not on the list, which is what
 * the C# returned and what the callers that step through the list expect. */
int gbl_team_index_of(const Player *player);

/* gbl.TeamList.Add. False when the list is full; the C# list grew without
 * bound, but the game only ever puts 71 characters on it. */
bool gbl_team_add(Player *player);

/* gbl.TeamList.Insert. Everything from `at` onwards moves up one; at ==
 * team_count appends. False, with the list untouched, for an index outside the
 * list or a full list. This is how the camp screen's Order menu walks a
 * character up and down the marching order. */
bool gbl_team_insert(int at, Player *player);

/* gbl.TeamList.RemoveAt. The list closes up over the gap, so an index into it -
 * and gbl.player_array, which is built from it - is stale afterwards. An index
 * outside the list is logged and ignored, where the C# would have thrown. */
void gbl_team_remove_at(int index);

/* --- gbl.unk_1D972 ---
 *
 * The string slots the instruction being run carries. Index is the operand
 * number, so 1..GBL_ECL_STRINGS-1; an index outside that is logged and reads as
 * the empty string, which is what a handler asking for a string the instruction
 * did not carry should see. Set truncates, with a warning, rather than
 * overrunning the slot. */
const char *gbl_ecl_string(int index);
void        gbl_ecl_string_set(int index, const char *text);
void        gbl_ecl_strings_clear(void);

/* --- gbl.vmCallStack ---
 *
 * Push returns false, having logged, when the stack is full. Pop hands back
 * false when it is empty, where the C#'s Stack<>.Pop would have thrown: a script
 * that returns without a matching Gosub is broken, and stopping it is better
 * than taking the game down. */
bool gbl_vm_call_push(u16 address);
bool gbl_vm_call_pop(u16 *out_address);
void gbl_vm_call_stack_clear(void);

/* --- gbl.spellTargets ---
 *
 * Add is gbl.spellTargets.Add and returns false when the list is full; Exists is
 * the C#'s Exists(st => st == player). */
void gbl_spell_targets_clear(void);
bool gbl_spell_target_add(Player *player);
bool gbl_spell_target_exists(const Player *player);

/* --- gbl.items_pointer ---
 *
 * Add copies the item and hands back its place on the ground, or NULL when there
 * is no room left. Remove_at closes the list up over the gap, so an index or a
 * pointer into it is stale afterwards. */
void  gbl_ground_items_clear(void);
Item *gbl_ground_item_add(const Item *item);
Item *gbl_ground_item_at(int index);
void  gbl_ground_item_remove_at(int index);

#endif /* COAB_GBL_H */

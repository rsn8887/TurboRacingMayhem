// Graphics shared by the game and the asset tool (the game loads them from the assets folder)
#include "sprites.h"

Graphics gfx;

const Color CAR_COLORS[NUM_CAR_COLORS] = {
    rgb(214, 18, 28),   // player 1 red
    rgb(26, 86, 226),   // player 2 blue
    rgb(250, 200, 20),  // yellow
    rgb(24, 160, 70),   // green
    rgb(236, 236, 240), // white
    rgb(64, 66, 78),    // graphite
    rgb(255, 116, 16),  // orange
    rgb(130, 46, 196),  // purple
    rgb(20, 190, 205),  // cyan
    rgb(168, 174, 186), // silver
};

const char* const SPRITE_NAMES[SPR_COUNT] = {
    "palm", "pine", "pine_snow", "bush", "cactus", "rock", "lamp",
    "billboard1", "billboard2", "billboard3", "building1", "building2",
    "snowman", "turn_left", "turn_right", "gantry_start", "gantry_check",
    "sign_jump", "sign_narrow", "sign_tunnel", "sign_dirt",
    "holo_pylon", "scifi_tower", "ufo", "holo_sign",
    "tropical", "carnival", "favela",
    "paris_lamp", "haussmann", "cafe", "tree_round",
    "fachwerk", "autobahn_sign", "wind_turbine",
    "cypress", "villa", "olive",
    "dead_tree", "windpump", "diner", "beach_hut", "beach_hut2", "lifeguard",
    "building3", "chalet", "cabin", "motel_sign", "dome", "favela2", "confetti_cannon",
    "haussmann2", "morris", "fachwerk2", "fachwerk3", "church", "villa2", "villa3", "vines",
};

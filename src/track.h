// Track = loop of road segments with curves, hills and roadside sprites
#pragma once
#include "config.h"
#include <vector>
#include <string>

enum SpriteType {
    SPR_PALM, SPR_PINE, SPR_PINE_SNOW, SPR_BUSH, SPR_CACTUS, SPR_ROCK, SPR_LAMP,
    SPR_BILLBOARD1, SPR_BILLBOARD2, SPR_BILLBOARD3, SPR_BUILDING1, SPR_BUILDING2,
    SPR_SNOWMAN, SPR_TURN_LEFT, SPR_TURN_RIGHT, SPR_GANTRY_START, SPR_GANTRY_CHECK,
    SPR_SIGN_JUMP, SPR_SIGN_NARROW, SPR_SIGN_TUNNEL, SPR_SIGN_DIRT,
    // stages 7-11 (appended, so the original sprites keep their numbers)
    SPR_HOLO_PYLON, SPR_SCIFI_TOWER, SPR_UFO, SPR_HOLO_SIGN,          // future
    SPR_TROPICAL, SPR_CARNIVAL, SPR_FAVELA,                           // Brazil
    SPR_PARIS_LAMP, SPR_HAUSSMANN, SPR_CAFE, SPR_TREE_ROUND,          // Paris (round tree also in Germany)
    SPR_FACHWERK, SPR_AUTOBAHN_SIGN, SPR_WIND_TURBINE,                // Germany
    SPR_CYPRESS, SPR_VILLA, SPR_OLIVE,                                // Tuscany
    // 1.4: more roadside variety (appended again)
    SPR_DEAD_TREE, SPR_WINDPUMP, SPR_DINER,                           // desert
    SPR_BEACH_HUT, SPR_BEACH_HUT2, SPR_LIFEGUARD,                     // coast
    SPR_BUILDING3, SPR_CHALET, SPR_CABIN, SPR_MOTEL_SIGN, SPR_DOME,   // night city, snow, forest, sunset, future
    SPR_FAVELA2, SPR_CONFETTI_CANNON,                                 // Brazil
    SPR_HAUSSMANN2, SPR_MORRIS,                                       // Paris
    SPR_FACHWERK2, SPR_FACHWERK3, SPR_CHURCH,                         // Germany
    SPR_VILLA2, SPR_VILLA3, SPR_VINES,                                // Tuscany
    // 1.4: stages 12-13 (appended again)
    SPR_BRIDGE, SPR_BRIDGE_PILLAR,                                    // road bridges the cars drive under
    SPR_AUTUMN_TREE1, SPR_AUTUMN_TREE2, SPR_OAK,                      // autumn forest
    SPR_PUDDLE, SPR_RAIN_LAMP, SPR_RAIN_BUILDING1, SPR_RAIN_BUILDING2, // rainy night city
    SPR_COUNT
};

struct SpriteInfo {
    double worldH;       // height in world units
    double hitFrac;      // fraction of the drawn width that collides (0 = no collision)
    bool mirrorRight = false;   // drawn mirrored on the right side of the road (street lamps reaching over the road)
};
extern const SpriteInfo SPRITE_INFO[SPR_COUNT];

struct SpriteInst {
    SpriteType type;
    double offset;       // lateral position in (standard) road half-widths; the road edge is at +-Segment::w
};

enum Surface : uint8_t { SURF_ASPHALT, SURF_DIRT };
enum Kerb : uint8_t {
    KERB_RUMBLE,     // classic alternating kerb stones (the look of the original prototype)
    KERB_ZEBRA,      // short, fast alternating blocks
    KERB_SOLID,      // solid painted edge with a gravel shoulder
    KERB_NONE,       // road runs straight into the verge
    KERB_GUARDRAIL,  // metal crash barrier along both edges (you scrape along it)
};

struct Segment {
    int index = 0;
    double y1 = 0, y2 = 0;    // world height at start / end
    double curve = 0;
    double w1 = 1, w2 = 1;    // road width factor at start / end (1 = standard 4 lanes)
    bool tunnel = false;
    bool shadow = false;      // in the shadow of a bridge over the road
    Surface surface = SURF_ASPHALT;
    Kerb kerb = KERB_RUMBLE;
    std::vector<SpriteInst> sprites;
};

constexpr double TUNNEL_HEIGHT = 3600;   // ceiling height above the road
constexpr double TUNNEL_SIDE   = 1.15;   // tunnel half width relative to the road half width (just past the kerbs)
constexpr double PORTAL_HEIGHT = 9000;   // height of the wall/rock face around a tunnel entrance
constexpr double TUNNEL_WALL_H = 650;    // concrete barrier along the tunnel walls
constexpr int    TUNNEL_LAMP_EVERY = 16; // segments between the rows of tunnel lamps
constexpr double RAIL_POS      = 1.24;   // guardrail position (road half widths)
constexpr double RAIL_HEIGHT   = 420;

// how the road surface is painted
enum RoadStyle : uint8_t {
    ROAD_ASPHALT,    // grey asphalt with light / dark bands (the original stages)
    ROAD_CHECKER,    // chequerboard tiles (future)
    ROAD_RAINBOW,    // flowing rainbow colours (Brazil)
    ROAD_COBBLE,     // cobblestones (Paris)
};

struct Theme {
    std::string name;
    Color skyTop, skyBottom, horizonGround;
    Color fogColor; double fogDensity;        // 0 = no fog
    Color grassLight, grassDark;
    Color rumbleA, rumbleB;
    Color roadLight, roadDark, lane;
    int   background;                         // index of background set (see sprites.h)
    bool  stars, sun, night;
    // tunnels
    Color wallA, wallB, ceilA, ceilB, lamp, facade, facadeCap;
    bool  rockFacade;
    // road surfaces / edges
    Color dirtLight, dirtDark, shoulder, rail;
    Kerb  kerb;                               // default kerb style of the stage
    RoadStyle road = ROAD_ASPHALT;
    Color roadAlt = 0;                        // second tile colour (chequerboard) / stone joints (cobbles)
    Color grid = 0;                           // if set: glowing grid lines on the verge (future)
    Color horizonGlow = 0;                    // if set: the glow along the horizon (else white by day, pink by night)
    bool  rain = false;                       // rain, wet road with puddles
};

constexpr int NUM_STAGES = 13;

class Track {
public:
    std::vector<Segment> segs;
    double length = 0;
    int stage = 0;
    Theme theme;

    void build(int stage);
    int  count() const { return int(segs.size()); }
    const Segment& find(double z) const;
    double wrap(double z) const;
    double heightAt(double z) const;
    double curveAt(double z) const { return find(z).curve; }
    double slopeAt(double z) const { const Segment& s = find(z); return (s.y2 - s.y1) / SEG_LEN; }
    double widthAt(double z) const;
    double groundSlope(double z) const { return (heightAt(z + SEG_LEN) - heightAt(z - SEG_LEN)) / (2 * SEG_LEN); }   // smoothed
    bool   tunnelAt(double z) const { return find(z).tunnel; }
    // lateral position (road half widths) of a solid wall at z: tunnel barrier or guardrail, 0 = none
    double wallAt(double z) const;
    double checkpointZ = 0;                  // position of the mid-lap checkpoint
    int    checkpointCount(double dist) const;   // half-laps passed for a continuous distance (-1 on the grid)

private:
    double lastY() const { return segs.empty() ? 0 : segs.back().y2; }
    double lastW() const { return segs.empty() ? 1 : segs.back().w2; }
    void addSegment(double curve, double y, double w);
    void addRoad(int enter, int hold, int leave, double curve, double height, double width = -1);
    void straight(int n)                         { addRoad(n, n, n, 0, 0); }
    void curve(int n, double c, double h = 0)    { addRoad(n, n, n, c, h); }
    void hill(int n, double h)                   { addRoad(n, n, n, 0, h); }
    void sCurves(int n, double c, double h = 0);
    void bumps(double h);
    void width(int n, double w);                 // narrow/widen the road over 3n segments
    void tunnel(int n, double c = 0);            // flat tunnel of 32n segments (optionally curved)
    void jump(double kick);                      // kicker ramp ending at slope 'kick' (0.14..0.22), then the road drops away
    bool inTunnel_ = false;
    Surface surface_ = SURF_ASPHALT;
    Kerb kerb_ = KERB_RUMBLE;
    void surface(Surface s)                      { if (s == SURF_DIRT && surface_ != SURF_DIRT) dirts_.push_back(count()); surface_ = s; }
    void kerb(Kerb k)                            { kerb_ = k; }
    void kerbDefault()                           { kerb_ = theme.kerb; }
public:
    std::vector<int> dirts_;
    std::vector<int> jumps_, narrows_, tunnels_;   // segment indices of features (signs, debugging)
    std::vector<int> confetti_;                    // Brazil: confetti cannons / autumn forest: old oaks - both fire at passing players
    std::vector<int> bridges_;                     // road bridges (stages 12-13)
    void downhillToEnd(int n);
    void layout(int stage);
    void decorate(unsigned seed);
};

extern const char* STAGE_NAMES[NUM_STAGES];

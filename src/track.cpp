#include "track.h"
#include <cmath>
#include <random>

const SpriteInfo SPRITE_INFO[SPR_COUNT] = {
    {3000, 0.17},  // PALM (hit width: the trunk, the bitmap is wide for the fronds)
    {2800, 0.30},  // PINE
    {2800, 0.30},  // PINE_SNOW
    { 800, 0.70},  // BUSH
    {1700, 0.45},  // CACTUS
    { 900, 0.80},  // ROCK
    {2700, 0.12, true},  // LAMP
    {2000, 0.90},  // BILLBOARD1
    {2000, 0.90},  // BILLBOARD2
    {2000, 0.90},  // BILLBOARD3
    {6500, 0.95},  // BUILDING1
    {5200, 0.95},  // BUILDING2
    {1300, 0.60},  // SNOWMAN
    {1300, 0.80},  // TURN_LEFT
    {1300, 0.80},  // TURN_RIGHT
    {2160, 0.00},  // GANTRY_START (posts handled separately)
    {2160, 0.00},  // GANTRY_CHECK
    {1500, 0.80},  // SIGN_JUMP
    {1500, 0.80},  // SIGN_NARROW
    {1500, 0.80},  // SIGN_TUNNEL
    {1500, 0.80},  // SIGN_DIRT
    {3000, 0.12, true},  // HOLO_PYLON (street light of the future)
    {7500, 0.90},  // SCIFI_TOWER
    {3400, 0.00},  // UFO (hovers: nothing to hit)
    {2100, 0.90},  // HOLO_SIGN
    {1200, 0.75},  // TROPICAL
    {2600, 0.20},  // CARNIVAL
    {4600, 0.95},  // FAVELA
    {2700, 0.12, true},  // PARIS_LAMP
    {6200, 0.95},  // HAUSSMANN
    {2400, 0.90},  // CAFE
    {2900, 0.30},  // TREE_ROUND
    {4800, 0.95},  // FACHWERK
    {2400, 0.85},  // AUTOBAHN_SIGN
    {8000, 0.08},  // WIND_TURBINE
    {3600, 0.35},  // CYPRESS
    {4200, 0.95},  // VILLA
    {1700, 0.45},  // OLIVE
    {2600, 0.12},  // DEAD_TREE
    {4600, 0.30},  // WINDPUMP
    {3600, 0.95},  // DINER
    {1900, 0.90},  // BEACH_HUT
    {1900, 0.90},  // BEACH_HUT2
    {3000, 0.60},  // LIFEGUARD
    {7600, 0.95},  // BUILDING3
    {4000, 0.95},  // CHALET
    {2800, 0.95},  // CABIN
    {4200, 0.12},  // MOTEL_SIGN
    {3400, 0.95},  // DOME
    {4600, 0.95},  // FAVELA2
    {2600, 0.20},  // CONFETTI_CANNON
    {6400, 0.95},  // HAUSSMANN2
    {2400, 0.80},  // MORRIS
    {4400, 0.95},  // FACHWERK2
    {6200, 0.95},  // FACHWERK3
    {8500, 0.95},  // CHURCH
    {3800, 0.95},  // VILLA2
    {5000, 0.95},  // VILLA3
    {1100, 0.95},  // VINES
};

const char* STAGE_NAMES[NUM_STAGES] = {
    "COCONUT COAST", "DESERT CANYON", "NEON NIGHT", "ALPINE PASS", "MISTY FOREST", "SUNSET BOULEVARD",
    "HYPERION 2099", "RIO CARNIVAL", "PARIS", "AUTOBAHN", "FLORENCE"
};

namespace {
constexpr int SHORT = 25, MEDIUM = 50, LONG = 100;
constexpr double EASY = 2, MED = 4, HARD = 6;
constexpr double LOW = 20, MID = 40, HIGH = 60;

double easeIn(double a, double b, double t)    { return a + (b - a) * t * t; }
double easeInOut(double a, double b, double t) { return a + (b - a) * ((-std::cos(t * M_PI) / 2) + 0.5); }

Theme makeTheme(int stage) {
    Theme t{};
    t.name = STAGE_NAMES[stage];
    t.lane = rgb(240, 240, 240);
    t.rumbleA = rgb(220, 20, 20); t.rumbleB = rgb(240, 240, 240);
    switch (stage) {
    case 0: // sunny coast - the colors of the original test1.png/test2.png
        t.skyTop = rgb(40, 110, 230); t.skyBottom = rgb(170, 215, 255); t.horizonGround = rgb(0, 160, 40);
        t.grassLight = rgb(0, 255, 32); t.grassDark = rgb(0, 200, 32);
        t.roadLight = rgb(128, 128, 128); t.roadDark = rgb(120, 120, 120);
        t.background = 0; break;
    case 1: // desert
        t.skyTop = rgb(60, 120, 210); t.skyBottom = rgb(250, 200, 140); t.horizonGround = rgb(200, 150, 90);
        t.grassLight = rgb(232, 196, 120); t.grassDark = rgb(216, 176, 100);
        t.roadLight = rgb(140, 125, 115); t.roadDark = rgb(130, 115, 105);
        t.rumbleB = rgb(250, 220, 60);
        t.background = 1; t.sun = true; break;
    case 2: // night city
        t.skyTop = rgb(10, 5, 35); t.skyBottom = rgb(90, 30, 110); t.horizonGround = rgb(25, 25, 40);
        t.grassLight = rgb(45, 45, 60); t.grassDark = rgb(38, 38, 52);
        t.roadLight = rgb(70, 70, 82); t.roadDark = rgb(62, 62, 74);
        t.rumbleA = rgb(255, 40, 200); t.rumbleB = rgb(40, 220, 255); t.lane = rgb(230, 230, 120);
        t.background = 2; t.stars = true; t.night = true; break;
    case 3: // snow
        t.skyTop = rgb(120, 150, 190); t.skyBottom = rgb(215, 225, 240); t.horizonGround = rgb(230, 235, 245);
        t.fogColor = rgb(215, 225, 240); t.fogDensity = 0.000035;
        t.grassLight = rgb(250, 250, 255); t.grassDark = rgb(225, 232, 245);
        t.roadLight = rgb(118, 122, 130); t.roadDark = rgb(108, 112, 120);
        t.background = 3; break;
    case 4: // fog forest
        t.skyTop = rgb(110, 125, 120); t.skyBottom = rgb(170, 180, 175); t.horizonGround = rgb(60, 90, 60);
        t.fogColor = rgb(170, 180, 175); t.fogDensity = 0.00009;
        t.grassLight = rgb(40, 120, 40); t.grassDark = rgb(30, 100, 35);
        t.roadLight = rgb(100, 100, 100); t.roadDark = rgb(92, 92, 92);
        t.background = 4; break;
    case 5: // sunset
        t.skyTop = rgb(40, 20, 90); t.skyBottom = rgb(255, 120, 60); t.horizonGround = rgb(120, 60, 60);
        t.grassLight = rgb(210, 120, 60); t.grassDark = rgb(190, 100, 50);
        t.roadLight = rgb(96, 80, 96); t.roadDark = rgb(88, 72, 88);
        t.rumbleB = rgb(255, 220, 120);
        t.background = 5; t.sun = true; break;
    case 6: // future: a sunlit utopia - a beige chequerboard road, white towers, teal accents, a pale planet by day
        t.skyTop = rgb(96, 160, 206); t.skyBottom = rgb(252, 226, 198); t.horizonGround = rgb(216, 198, 168);
        t.fogColor = rgb(246, 226, 204); t.fogDensity = 0.000018;
        t.grassLight = rgb(230, 216, 190); t.grassDark = rgb(220, 204, 176);
        t.roadLight = rgb(238, 224, 194); t.roadDark = rgb(232, 218, 188);
        t.road = ROAD_CHECKER; t.roadAlt = rgb(198, 172, 134);
        t.grid = rgb(30, 168, 170);
        t.rumbleA = rgb(30, 168, 170); t.rumbleB = rgb(250, 248, 240); t.lane = rgb(30, 150, 160);
        t.background = 6; break;
    case 7: // Rio: a psychedelic rainbow road, Sugarloaf and the statue on the hill
        t.skyTop = rgb(255, 40, 160); t.skyBottom = rgb(255, 200, 60); t.horizonGround = rgb(0, 150, 90);
        t.grassLight = rgb(40, 230, 70); t.grassDark = rgb(250, 220, 0);
        t.roadLight = rgb(150, 60, 200); t.roadDark = rgb(140, 50, 190);
        t.road = ROAD_RAINBOW;
        t.rumbleA = rgb(0, 80, 220); t.rumbleB = rgb(255, 230, 0); t.lane = rgb(255, 255, 255);
        t.background = 7; t.sun = true; break;
    case 8: // Paris: cobblestones, pale limestone and the tower at dusk
        t.skyTop = rgb(90, 110, 170); t.skyBottom = rgb(250, 190, 170); t.horizonGround = rgb(150, 140, 130);
        t.grassLight = rgb(196, 186, 168); t.grassDark = rgb(184, 174, 156);   // sand-coloured pavements
        t.roadLight = rgb(120, 118, 122); t.roadDark = rgb(112, 110, 114);
        t.road = ROAD_COBBLE; t.roadAlt = rgb(70, 68, 72);
        t.rumbleA = rgb(30, 50, 150); t.rumbleB = rgb(240, 240, 240); t.lane = rgb(235, 235, 235);
        t.background = 8; break;
    case 9: // Autobahn: wide motorway through green hills, half-timbered villages and a fairy-tale castle
        t.skyTop = rgb(70, 130, 220); t.skyBottom = rgb(200, 225, 250); t.horizonGround = rgb(70, 140, 60);
        t.grassLight = rgb(90, 180, 60); t.grassDark = rgb(76, 160, 52);
        t.roadLight = rgb(110, 112, 118); t.roadDark = rgb(102, 104, 110);
        t.background = 9; break;
    default: // 10 Florence: Tuscan hills, cypresses, white gravel roads and the cathedral dome
        t.skyTop = rgb(60, 130, 220); t.skyBottom = rgb(250, 230, 190); t.horizonGround = rgb(170, 160, 90);
        t.grassLight = rgb(176, 184, 80); t.grassDark = rgb(160, 168, 70);
        t.roadLight = rgb(126, 116, 108); t.roadDark = rgb(118, 108, 100);
        t.rumbleA = rgb(190, 70, 40); t.rumbleB = rgb(245, 235, 210);
        t.background = 10; t.sun = true; break;
    }
    // dirt, gravel shoulders, guardrails and default kerb style
    t.dirtLight = rgb(176, 132, 84); t.dirtDark = rgb(160, 118, 74);
    t.shoulder = rgb(150, 140, 120); t.rail = rgb(190, 196, 206);
    t.kerb = KERB_RUMBLE;
    switch (stage) {
    case 1: t.dirtLight = rgb(206, 150, 92); t.dirtDark = rgb(190, 136, 80); t.shoulder = rgb(214, 178, 120); break;
    case 2: t.kerb = KERB_ZEBRA; t.shoulder = rgb(60, 60, 74); break;
    case 3: t.dirtLight = rgb(150, 140, 130); t.dirtDark = rgb(136, 126, 118); t.shoulder = rgb(200, 205, 215); t.kerb = KERB_SOLID; break;
    case 4: t.dirtLight = rgb(130, 98, 64); t.dirtDark = rgb(116, 86, 56); t.shoulder = rgb(110, 100, 80); t.kerb = KERB_SOLID; break;
    case 5: t.shoulder = rgb(200, 150, 110); break;
    case 6: t.kerb = KERB_ZEBRA; t.shoulder = rgb(206, 190, 162); t.rail = rgb(214, 226, 230); break;
    case 7: t.dirtLight = rgb(230, 120, 60); t.dirtDark = rgb(210, 100, 50); t.kerb = KERB_ZEBRA; break;
    case 8: t.kerb = KERB_SOLID; t.shoulder = rgb(206, 198, 184); break;
    case 9: t.kerb = KERB_SOLID; t.shoulder = rgb(150, 150, 140); break;
    case 10: t.dirtLight = rgb(236, 226, 204); t.dirtDark = rgb(222, 210, 186); t.shoulder = rgb(210, 196, 160); break;   // strade bianche
    default: break;
    }
    // tunnel looks: concrete by default, rock faces in the countryside
    t.wallA = rgb(176, 168, 146); t.wallB = rgb(140, 132, 114);
    t.ceilA = rgb(62, 60, 58);    t.ceilB = rgb(50, 48, 46);
    t.lamp = rgb(255, 236, 170);
    t.facade = rgb(140, 120, 100); t.facadeCap = rgb(40, 140, 50); t.rockFacade = true;
    switch (stage) {
    case 1: t.wallA = rgb(200, 140, 92); t.wallB = rgb(168, 112, 72); t.facade = rgb(196, 110, 66); t.facadeCap = rgb(220, 160, 100); break;
    case 2: t.wallA = rgb(80, 80, 130); t.wallB = rgb(58, 58, 100); t.ceilA = rgb(36, 36, 56); t.ceilB = rgb(28, 28, 44);
            t.lamp = rgb(120, 240, 255); t.facade = rgb(70, 70, 96); t.facadeCap = rgb(255, 60, 200); t.rockFacade = false; break;
    case 3: t.facade = rgb(130, 136, 150); t.facadeCap = rgb(250, 252, 255); break;
    case 4: t.facade = rgb(110, 110, 100); t.facadeCap = rgb(40, 100, 45); break;
    case 5: t.wallA = rgb(176, 126, 136); t.wallB = rgb(146, 100, 112); t.facade = rgb(150, 90, 90); t.facadeCap = rgb(230, 130, 70); t.rockFacade = false; break;
    case 6: t.wallA = rgb(226, 222, 210); t.wallB = rgb(196, 192, 180); t.lamp = rgb(150, 250, 240);
            t.facade = rgb(232, 228, 216); t.facadeCap = rgb(30, 168, 170); t.rockFacade = false; break;
    case 7: t.facade = rgb(90, 120, 80); t.facadeCap = rgb(40, 200, 80); break;
    case 8: t.facade = rgb(200, 190, 170); t.facadeCap = rgb(110, 120, 140); t.rockFacade = false; break;
    case 9: t.facade = rgb(150, 150, 146); t.facadeCap = rgb(60, 150, 60); t.rockFacade = false; break;
    case 10: t.facade = rgb(200, 170, 120); t.facadeCap = rgb(190, 90, 50); break;
    default: break;
    }
    return t;
}
} // namespace

void Track::addSegment(double c, double y, double w) {
    Segment s;
    s.index = int(segs.size());
    s.y1 = lastY();
    s.y2 = y;
    s.w1 = lastW();
    s.w2 = w;
    s.curve = c;
    s.tunnel = inTunnel_;
    s.surface = inTunnel_ ? SURF_ASPHALT : surface_;
    s.kerb = inTunnel_ ? KERB_RUMBLE : (surface_ == SURF_DIRT ? KERB_NONE : kerb_);
    segs.push_back(s);
}

void Track::addRoad(int enter, int hold, int leave, double c, double height, double width) {
    double startY = lastY(), startW = lastW();
    double endY = startY + height * SEG_LEN;
    double endW = width > 0 ? width : startW;
    int total = enter + hold + leave;
    auto W = [&](int k) { return easeInOut(startW, endW, double(k + 1) / total); };
    for (int n = 0; n < enter; ++n) addSegment(easeIn(0, c, double(n) / enter), easeInOut(startY, endY, double(n) / total), W(n));
    for (int n = 0; n < hold; ++n)  addSegment(c, easeInOut(startY, endY, double(enter + n) / total), W(enter + n));
    for (int n = 0; n < leave; ++n) addSegment(easeInOut(c, 0, double(n) / leave), easeInOut(startY, endY, double(enter + hold + n) / total), W(enter + hold + n));
}

void Track::width(int n, double w) {
    if (w < lastW() - 0.1) narrows_.push_back(count());
    addRoad(n, n, n, 0, 0, w);
}

void Track::tunnel(int n, double c) {
    // long: a SHORT tunnel is ~800 segments (about 8 s at top speed). Inside, straights alternate with bends
    // that change direction; 'c' sets the main bend. Two patterns take turns, so no two tunnels in a row
    // feel the same; a tunnel given no bend becomes a gentle S.
    const int k = int(tunnels_.size());
    tunnels_.push_back(count());
    inTunnel_ = true;
    double main = c != 0 ? c : (k % 2 ? 2.0 : -2.0);
    addRoad(n, n, n, 0, 0, 1.0);                       // straight in
    if (k % 2 == 0) {
        addRoad(n, n * 5, n, main, 0, 1.0);                      // the main bend
        addRoad(n / 2, n * 2, n / 2, 0, 0, 1.0);                 // short straight
        addRoad(n, n * 4, n, -main * 0.7, 0, 1.0);               // back the other way
        addRoad(n, n * 3, n, 0, 0, 1.0);                         // long straight
        addRoad(n, n * 3, n, main * 1.3, 0, 1.0);                // a tighter one
    } else {
        addRoad(n, n * 3, n, -main * 0.6, 0, 1.0);               // a gentle lead-in the other way
        addRoad(n / 2, n, n / 2, 0, 0, 1.0);
        addRoad(n, n * 6, n, main, 0, 1.0);                      // the long main bend
        addRoad(n, n * 4, n, 0, 0, 1.0);                         // flat-out straight
        addRoad(n, n * 2, n, -main * 1.2, 0, 1.0);               // a sharp flick back
        addRoad(n, n * 2, n, main * 0.5, 0, 1.0);
    }
    addRoad(n, n, n, 0, 0, 1.0);                                 // straight out
    inTunnel_ = false;
}

void Track::jump(double kick) {
    // kicker: the slope grows steadily to 'kick' (a quadratic ramp), then the road drops straight back
    // to the original level. Fast cars leave the lip with an upward speed of v * kick and fly a free-fall
    // parabola (about 1 s and 100 segments at top speed), landing on the flat road beyond.
    const int n1 = 32, n2 = 14;
    double y0 = lastY(), w = lastW();
    double rampLen = n1 * SEG_LEN, top = y0 + kick * rampLen / 2;
    for (int k = 1; k <= n1; ++k) {
        double z = k * SEG_LEN;
        addSegment(0, y0 + kick * z * z / (2 * rampLen), w);
    }
    jumps_.push_back(count());
    for (int k = 1; k <= n2; ++k) {
        double t = double(k) / n2;
        addSegment(0, top + (y0 - top) * (0.5 - 0.5 * std::cos(t * M_PI)), w);
    }
    straight(25);                      // landing zone (75 segments)
}

void Track::sCurves(int n, double c, double h) {
    addRoad(n, n, n, -c, h * 0.5);
    addRoad(n, n, n,  c, -h * 0.5);
    addRoad(n, n, n,  c * 1.5, 0);
    addRoad(n, n, n, -c, h * 0.3);
    addRoad(n, n, n, -c * 0.7, -h * 0.3);
}

void Track::bumps(double h) {
    // rolling bumps: gentle enough to stay on the road at moderate speed, small hops when flat out
    addRoad(15, 5, 15, 0, h * 0.10);
    addRoad(15, 5, 15, 0, -h * 0.14);
    addRoad(15, 5, 15, 0, h * 0.12);
    addRoad(15, 5, 15, 0, -h * 0.08);
}

void Track::downhillToEnd(int n) {
    if (std::fabs(lastW() - 1) > 0.01) addRoad(n, n, n, 0, 0, 1.0);
    // long enough that the descent back to the start height stays gentle (max slope ~0.12)
    int m = std::max(n, int(std::ceil(std::fabs(lastY()) / (SEG_LEN * 0.12) / 3)));
    addRoad(m, m, m, 0, -lastY() / SEG_LEN);
}

void Track::layout(int st) {
    switch (st) {
    case 0:
        straight(SHORT);
        curve(MEDIUM, EASY);
        hill(MEDIUM, LOW);
        curve(MEDIUM, -MED, -LOW);
        straight(MEDIUM);
        tunnel(SHORT, EASY);                 // short curved tunnel through the headland
        straight(SHORT);
        surface(SURF_DIRT);                  // sandy beach track
        sCurves(SHORT, EASY);
        surface(SURF_ASPHALT);
        curve(LONG, MED, MID);
        bumps(LOW);
        kerb(KERB_GUARDRAIL);
        width(SHORT, 0.65);                  // narrow coastal road along the cliffs
        curve(MEDIUM, -EASY, -MID);
        sCurves(SHORT, EASY);
        kerb(KERB_SOLID);
        width(SHORT, 1.4);                   // opens up into a wide straight
        straight(MEDIUM);
        kerbDefault();
        jump(0.18);
        width(SHORT, 1.0);
        curve(MEDIUM, HARD);
        curve(MEDIUM, -MED, LOW);
        hill(MEDIUM, -LOW);
        sCurves(SHORT, MED, LOW);
        curve(LONG, -EASY);
        break;
    case 1:
        straight(MEDIUM);
        hill(LONG, HIGH);
        curve(MEDIUM, MED, -MID);
        kerb(KERB_SOLID);
        width(MEDIUM, 1.5);                  // wide desert highway
        straight(LONG);
        jump(0.18);
        jump(0.14);
        kerbDefault();
        width(SHORT, 1.0);
        bumps(MID);
        curve(LONG, -EASY, LOW);
        hill(MEDIUM, -HIGH);
        tunnel(SHORT, -MED);                 // canyon tunnel
        surface(SURF_DIRT);                  // dusty canyon track
        curve(MEDIUM, HARD);
        straight(LONG);
        surface(SURF_ASPHALT);
        kerb(KERB_ZEBRA);
        width(SHORT, 0.7);
        curve(MEDIUM, -MED, MID);
        bumps(HIGH);
        kerbDefault();
        width(SHORT, 1.0);
        curve(LONG, EASY, -LOW);
        jump(0.22);
        straight(MEDIUM);
        break;
    case 2:
        straight(SHORT);
        curve(SHORT, HARD);
        tunnel(SHORT);                       // underpass
        straight(SHORT);
        curve(SHORT, -HARD);
        kerb(KERB_SOLID);
        width(SHORT, 1.5);                   // wide boulevard
        straight(MEDIUM);
        sCurves(SHORT, MED);
        kerbDefault();
        width(SHORT, 1.0);
        curve(MEDIUM, MED, LOW);
        tunnel(MEDIUM, EASY);
        curve(SHORT, -HARD, -LOW);
        kerb(KERB_GUARDRAIL);
        width(SHORT, 0.7);                   // back alley between crash barriers
        curve(SHORT, HARD);
        straight(MEDIUM);
        kerbDefault();
        width(SHORT, 1.0);
        jump(0.14);
        straight(MEDIUM);
        sCurves(SHORT, HARD);
        curve(MEDIUM, -MED);
        straight(MEDIUM);
        break;
    case 3:
        straight(SHORT);
        curve(MEDIUM, MED, HIGH);
        tunnel(SHORT, -EASY);
        curve(MEDIUM, -MED, MID);
        kerb(KERB_GUARDRAIL);
        width(SHORT, 0.65);                  // narrow mountain pass with crash barriers
        hill(MEDIUM, -LOW);
        sCurves(SHORT, MED, MID);
        kerbDefault();
        width(SHORT, 1.0);
        curve(LONG, EASY, -HIGH);
        jump(0.18);
        surface(SURF_DIRT);                  // gravel section
        bumps(MID);
        curve(MEDIUM, -HARD, HIGH);
        surface(SURF_ASPHALT);
        tunnel(MEDIUM, MED);                 // long curved mountain tunnel
        curve(MEDIUM, HARD, -MID);
        kerb(KERB_RUMBLE);
        straight(MEDIUM);
        curve(LONG, -MED, -LOW);
        kerbDefault();
        break;
    case 4:
        straight(SHORT);
        surface(SURF_DIRT);                  // forest dirt track
        width(SHORT, 0.7);
        sCurves(SHORT, MED);
        curve(MEDIUM, HARD, LOW);
        curve(MEDIUM, -HARD, -LOW);
        surface(SURF_ASPHALT);
        width(SHORT, 1.0);
        sCurves(SHORT, HARD, LOW);
        jump(0.18);
        kerb(KERB_RUMBLE);
        hill(MEDIUM, MID);
        curve(MEDIUM, MED, -MID);
        kerbDefault();
        tunnel(SHORT, -EASY);
        bumps(LOW);
        surface(SURF_DIRT);
        width(SHORT, 0.6);
        curve(LONG, -MED);
        width(SHORT, 1.0);
        surface(SURF_ASPHALT);
        sCurves(SHORT, MED, -LOW);
        curve(MEDIUM, HARD);
        break;
    case 5:
        straight(MEDIUM);
        kerb(KERB_GUARDRAIL);
        width(SHORT, 1.5);                   // wide beach highway with crash barriers
        curve(LONG, EASY, LOW);
        straight(LONG);
        kerbDefault();
        width(SHORT, 1.0);
        curve(MEDIUM, -MED, MID);
        bumps(MID);
        tunnel(SHORT, MED);
        curve(MEDIUM, HARD, -MID);
        jump(0.18);
        straight(LONG);
        sCurves(SHORT, MED, LOW);
        kerb(KERB_ZEBRA);
        width(SHORT, 0.7);
        curve(LONG, -EASY, HIGH);
        kerbDefault();
        width(SHORT, 1.0);
        hill(LONG, -HIGH);
        jump(0.14);
        curve(MEDIUM, MED);
        curve(MEDIUM, -HARD, LOW);
        straight(MEDIUM);
        break;
    case 6:   // future: fast and flowing, with neon tunnels and big jumps
        straight(MEDIUM);
        kerb(KERB_GUARDRAIL);
        width(SHORT, 1.5);                   // wide skyway between energy barriers
        curve(LONG, EASY, MID);
        straight(MEDIUM);
        kerbDefault();
        width(SHORT, 1.0);
        tunnel(SHORT, -MED);                 // neon tube
        jump(0.20);
        sCurves(SHORT, MED, LOW);
        curve(MEDIUM, HARD, -MID);
        hill(MEDIUM, HIGH);
        jump(0.22);
        curve(LONG, -MED, -LOW);
        tunnel(SHORT, EASY);
        kerb(KERB_GUARDRAIL);
        width(SHORT, 0.7);                   // narrow maintenance lane
        sCurves(SHORT, HARD);
        kerbDefault();
        width(SHORT, 1.0);
        straight(LONG);
        jump(0.16);
        curve(MEDIUM, -HARD);
        break;
    case 7:   // Rio: up and down the hills by the bay, a sandy favela track
        straight(SHORT);
        curve(MEDIUM, MED, HIGH);
        curve(MEDIUM, -MED, -MID);
        bumps(MID);
        surface(SURF_DIRT);                  // red dirt up through the favela
        sCurves(SHORT, MED, LOW);
        curve(MEDIUM, HARD, MID);
        surface(SURF_ASPHALT);
        jump(0.18);
        kerb(KERB_RUMBLE);
        width(SHORT, 1.5);                   // wide beach avenue
        straight(LONG);
        curve(LONG, -EASY);
        kerbDefault();
        width(SHORT, 1.0);
        tunnel(SHORT, MED);                  // through the mountain
        hill(MEDIUM, -HIGH);
        sCurves(SHORT, HARD, LOW);
        jump(0.14);
        curve(MEDIUM, -MED, -LOW);
        break;
    case 8:   // Paris: boulevards, tight corners around the old town, underpasses along the river
        straight(SHORT);
        curve(SHORT, -HARD);
        width(SHORT, 1.5);                   // the grand avenue
        straight(LONG);
        curve(MEDIUM, EASY);
        width(SHORT, 1.0);
        curve(SHORT, HARD);
        curve(SHORT, -HARD);
        tunnel(SHORT, -EASY);                // underpass by the river
        straight(MEDIUM);
        kerb(KERB_GUARDRAIL);
        width(SHORT, 0.7);                   // narrow embankment road
        sCurves(SHORT, MED);
        kerbDefault();
        width(SHORT, 1.0);
        hill(MEDIUM, MID);                   // up to the hill of the artists
        curve(MEDIUM, HARD, LOW);
        jump(0.14);
        hill(MEDIUM, -MID);
        sCurves(SHORT, HARD);
        curve(MEDIUM, -MED);
        break;
    case 9:   // Autobahn: long, wide and fast, then winding through the forest past the castle
        straight(MEDIUM);
        kerb(KERB_GUARDRAIL);
        width(SHORT, 1.5);                   // six lanes between crash barriers
        straight(LONG);
        curve(LONG, EASY);
        straight(LONG);
        curve(LONG, -EASY, LOW);
        straight(LONG);
        kerbDefault();
        width(SHORT, 1.0);
        tunnel(SHORT, EASY);
        curve(MEDIUM, -MED, MID);
        sCurves(SHORT, MED, LOW);
        kerb(KERB_RUMBLE);
        curve(MEDIUM, HARD, -MID);
        jump(0.16);
        bumps(LOW);
        kerbDefault();
        curve(LONG, -MED);
        straight(MEDIUM);
        break;
    default:  // 10 Florence: rolling hills, cypress avenues and white gravel roads
        straight(SHORT);
        hill(MEDIUM, MID);
        curve(MEDIUM, MED, -LOW);
        surface(SURF_DIRT);                  // strade bianche
        width(SHORT, 0.8);
        sCurves(SHORT, MED, MID);
        curve(MEDIUM, -HARD, -MID);
        surface(SURF_ASPHALT);
        width(SHORT, 1.0);
        curve(LONG, EASY, HIGH);
        jump(0.18);
        hill(MEDIUM, -HIGH);
        kerb(KERB_GUARDRAIL);
        width(SHORT, 0.7);                   // narrow lane along the river
        curve(MEDIUM, -MED);
        kerbDefault();
        width(SHORT, 1.0);
        tunnel(SHORT, -EASY);
        bumps(MID);
        surface(SURF_DIRT);
        curve(MEDIUM, HARD, LOW);
        surface(SURF_ASPHALT);
        sCurves(SHORT, HARD, -LOW);
        curve(MEDIUM, -EASY);
        break;
    }
    surface(SURF_ASPHALT);
    kerbDefault();
    downhillToEnd(SHORT);
    // flat, straight run-in to the start line, long enough for the whole staggered grid
    straight(int(std::ceil((GRID_FRONT + (NUM_CARS - 1) * GRID_SLOT) / SEG_LEN / 3)) + 6);
}

void Track::decorate(unsigned seed) {
    std::mt19937 rng(seed);
    auto rnd = [&](double a, double b) { return std::uniform_real_distribution<double>(a, b)(rng); };
    auto chance = [&](double p) { return rnd(0, 1) < p; };
    auto side = [&]() { return chance(0.5) ? -1.0 : 1.0; };
    // 1.4 additions draw from their own random numbers, so everything placed before stays where it was
    std::mt19937 rng2(seed * 31u + 17u);
    auto rnd2 = [&](double a, double b) { return std::uniform_real_distribution<double>(a, b)(rng2); };
    auto side2 = [&]() { return rnd2(0, 1) < 0.5 ? -1.0 : 1.0; };
    // house styles by district: the district changes after every tunnel. Each district has a main style and
    // mixes in about a quarter of the next one.
    auto district = [&](int i) { int d = 0; for (int t : tunnels_) if (i >= t) ++d; return d; };
    auto houseOf = [&](int i, std::initializer_list<SpriteType> styles) {
        int k = int(styles.size()), d = district(i);
        unsigned hsh = unsigned(i) * 2654435761u;
        int pick = (hsh >> 16) % 100 < 25 ? (d + 1) % k : d % k;
        return *(styles.begin() + pick);
    };
    int n = count();
    int checkpoint = int(checkpointZ / SEG_LEN);
    auto nearGantry = [&](int i) { return i < 6 || i > n - 4 || std::abs(i - checkpoint) < 6; };
    auto isTunnel = [&](int i) { return segs[((i % n) + n) % n].tunnel; };
    // offsets are given relative to a standard road; keep the same distance from the edge when the road
    // is narrower or wider. Nothing goes inside tunnels or right in front of a portal.
    auto add = [&](int i, SpriteType t, double off) {
        int k = ((i % n) + n) % n;
        if (isTunnel(k) || isTunnel(k + 1) || isTunnel(k + 2) || isTunnel(k + 3)) return;
        double w = std::max(segs[k].w1, segs[k].w2);
        if (std::fabs(off) >= 1) off = (off < 0 ? -1 : 1) * (w + std::fabs(off) - 1);
        segs[k].sprites.push_back({t, off});
    };

    add(0, SPR_GANTRY_START, 0);
    add(checkpoint, SPR_GANTRY_CHECK, 0);

    // warning signs for jumps, narrow sections and tunnels
    for (int j : jumps_)   for (int k : {48, 30}) { add(j - k, SPR_SIGN_JUMP, -1.5); add(j - k, SPR_SIGN_JUMP, 1.5); }
    for (int j : narrows_) for (int k : {22, 8})  { add(j - k, SPR_SIGN_NARROW, -1.5); add(j - k, SPR_SIGN_NARROW, 1.5); }
    for (int j : tunnels_) for (int k : {40})     { add(j - k, SPR_SIGN_TUNNEL, 1.5); }
    for (int j : dirts_)   for (int k : {30, 12}) { add(j - k, SPR_SIGN_DIRT, -1.5); add(j - k, SPR_SIGN_DIRT, 1.5); }

    // warning signs before sharp curves (on the outside of the bend)
    for (int i = 1; i < n; ++i) {
        double c0 = segs[i - 1].curve, c1 = segs[i].curve;
        if (std::abs(c1) >= 3.5 && std::abs(c0) < 3.5 && std::signbit(c0) == std::signbit(c1)) {
            for (int k = 30; k >= 6; k -= 6) {
                int j = i - k;
                if (j <= 0 || nearGantry(j)) continue;
                add(j, c1 > 0 ? SPR_TURN_RIGHT : SPR_TURN_LEFT, c1 > 0 ? -1.45 : 1.45);
            }
        }
    }

    int billboards[3] = {SPR_BILLBOARD1, SPR_BILLBOARD2, SPR_BILLBOARD3};
    int bb = 0;
    for (int i = 0; i < n; ++i) {
        bool gantry = nearGantry(i);
        bool sign = false;
        for (auto& s : segs[i].sprites)
            sign |= (s.type == SPR_TURN_LEFT || s.type == SPR_TURN_RIGHT || (s.type >= SPR_SIGN_JUMP && s.type <= SPR_SIGN_DIRT));
        if (segs[i].kerb == KERB_GUARDRAIL) sign = true;    // keep the rail side clear of close scenery
        if (!gantry && !sign && i % 120 == 60) add(i, SpriteType(billboards[bb++ % 3]), side() * rnd(1.9, 2.4));
        switch (stage) {
        case 0:
            if (i % 10 == 0 && !gantry && !sign) { add(i, SPR_PALM, -1.6 - rnd(0, 0.4)); add(i + 5, SPR_PALM, 1.6 + rnd(0, 0.4)); }
            if (chance(0.11)) add(i, SPR_BUSH, side() * rnd(2.6, 6.0));
            if (chance(0.03)) add(i, SPR_PALM, side() * rnd(3.0, 7.0));
            if (i % 70 == 35 && !gantry) {                     // a little row of beach huts
                double sd = side2(), off = rnd2(2.7, 3.6);
                for (int k = 0; k < 3; ++k) add(i + k * 2, (k & 1) ? SPR_BEACH_HUT2 : SPR_BEACH_HUT, sd * (off + k * 0.05));
            }
            if (i % 190 == 95 && !gantry && !sign) add(i, SPR_LIFEGUARD, side2() * rnd2(2.4, 3.0));
            break;
        case 1:
            if (chance(0.07)) add(i, SPR_CACTUS, side() * rnd(1.7, 6.0));
            if (chance(0.05)) add(i, SPR_ROCK, side() * rnd(1.6, 5.0));
            if (i % 40 == 20 && !gantry && !sign) add(i, SPR_ROCK, side() * 1.6);
            if (rnd2(0, 1) < 0.015) add(i, SPR_DEAD_TREE, side2() * rnd2(1.9, 6.0));
            if (i % 110 == 55) add(i, SPR_WINDPUMP, side2() * rnd2(3.0, 6.0));
            if (i % 260 == 130 && !gantry) add(i, SPR_DINER, side2() * rnd2(3.8, 4.4));
            break;
        case 2:
            if (i % 20 == 0 && !gantry && !sign) { add(i, SPR_LAMP, -1.35); add(i + 10, SPR_LAMP, 1.35); }
            if (i % 9 == 0) {
                SpriteType b = chance(0.5) ? SPR_BUILDING1 : SPR_BUILDING2;
                if ((unsigned(i) * 2654435761u >> 16) % 100 < 22) b = SPR_BUILDING3;   // a few neon towers
                add(i, b, side() * rnd(2.8, 4.5));
            }
            if (chance(0.06)) add(i, chance(0.5) ? SPR_BUILDING1 : SPR_BUILDING2, side() * rnd(5.0, 8.0));
            break;
        case 3:
            if (chance(0.17)) add(i, SPR_PINE_SNOW, side() * rnd(1.7, 7.0));
            if (i % 16 == 0 && !gantry && !sign) add(i, SPR_PINE_SNOW, side() * rnd(1.6, 2.0));
            if (chance(0.02)) add(i, SPR_SNOWMAN, side() * rnd(1.6, 3.0));
            if (chance(0.05)) add(i, SPR_ROCK, side() * rnd(2.0, 5.0));
            if (i % 70 == 35 && !gantry) add(i, SPR_CHALET, side2() * rnd2(3.2, 5.0));
            break;
        case 4:
            if (chance(0.3)) add(i, SPR_PINE, side() * rnd(1.6, 8.0));
            if (i % 7 == 0 && !gantry && !sign) add(i, SPR_PINE, side() * rnd(1.6, 2.1));
            if (chance(0.08)) add(i, SPR_BUSH, side() * rnd(1.6, 4.0));
            if (i % 90 == 45 && !gantry) add(i, SPR_CABIN, side2() * rnd2(3.0, 4.5));
            break;
        case 5:
            if (i % 16 == 0 && !gantry && !sign) { add(i, SPR_PALM, -1.65); add(i, SPR_PALM, 1.65); }
            if (i % 24 == 8 && !gantry && !sign) { add(i, SPR_LAMP, -1.35); add(i, SPR_LAMP, 1.35); }
            if (chance(0.05)) add(i, SPR_BUSH, side() * rnd(2.5, 5.0));
            if (i % 60 == 30 && !gantry && !sign) add(i, SpriteType(billboards[bb++ % 3]), side() * rnd(2.0, 2.4));
            if (i % 150 == 75 && !gantry && !sign) add(i, SPR_MOTEL_SIGN, side2() * 2.2);
            break;
        case 6:   // future
            if (i % 20 == 0 && !gantry && !sign) { add(i, SPR_HOLO_PYLON, -1.35); add(i + 10, SPR_HOLO_PYLON, 1.35); }
            if (i % 11 == 0) add(i, SPR_SCIFI_TOWER, side() * rnd(3.0, 5.0));
            if (chance(0.05)) add(i, SPR_SCIFI_TOWER, side() * rnd(5.5, 9.0));
            if (i % 70 == 35) add(i, SPR_UFO, side() * rnd(2.2, 4.0));
            if (i % 90 == 10 && !gantry && !sign) add(i, SPR_HOLO_SIGN, side() * rnd(1.9, 2.3));
            if (i % 110 == 55) add(i, SPR_DOME, side2() * rnd2(3.4, 6.0));
            break;
        case 7:   // Rio
            if (i % 12 == 0 && !gantry && !sign) { add(i, SPR_PALM, -1.65 - rnd(0, 0.3)); add(i + 6, SPR_PALM, 1.65 + rnd(0, 0.3)); }
            if (chance(0.13)) add(i, SPR_TROPICAL, side() * rnd(2.0, 5.0));
            if (i % 30 == 15 && !gantry && !sign) add(i, SPR_CARNIVAL, side() * 1.5);
            if (i % 15 == 4) add(i, houseOf(i, {SPR_FAVELA, SPR_FAVELA2}), side() * rnd(3.2, 6.0));
            if (chance(0.03)) add(i, houseOf(i, {SPR_FAVELA, SPR_FAVELA2}), side() * rnd(6.0, 9.0));
            if (i % 160 == 80 && !gantry && !sign && !isTunnel(i) && !isTunnel(i + 4)) {   // confetti cannons
                add(i, SPR_CONFETTI_CANNON, -1.55);
                add(i, SPR_CONFETTI_CANNON, 1.55);
                confetti_.push_back(i);
            }
            break;
        case 8:   // Paris
            if (i % 20 == 0 && !gantry && !sign) { add(i, SPR_PARIS_LAMP, -1.35); add(i + 10, SPR_PARIS_LAMP, 1.35); }
            if (i % 14 == 5 && !gantry && !sign) { add(i, SPR_TREE_ROUND, -1.9); add(i + 7, SPR_TREE_ROUND, 1.9); }
            if (i % 11 == 0) add(i, houseOf(i, {SPR_HAUSSMANN, SPR_HAUSSMANN2}), side() * rnd(3.2, 4.4));
            if (chance(0.02)) add(i, houseOf(i, {SPR_HAUSSMANN, SPR_HAUSSMANN2}), side() * rnd(5.0, 8.0));
            if (i % 90 == 40 && !gantry && !sign) add(i, SPR_MORRIS, side2() * 2.1);
            if (i % 30 == 15 && !gantry && !sign) add(i, SPR_CAFE, side() * 2.4);
            break;
        case 9:   // Germany
            if (chance(0.17)) add(i, chance(0.6) ? SPR_PINE : SPR_TREE_ROUND, side() * rnd(2.2, 8.0));
            if (i % 15 == 0 && !gantry && !sign) add(i, SPR_TREE_ROUND, side() * rnd(1.8, 2.2));
            if (i % 40 == 20) add(i, houseOf(i, {SPR_FACHWERK, SPR_FACHWERK2, SPR_FACHWERK3}), side() * rnd(3.5, 6.0));
            if (i % 40 == 33 && rnd2(0, 1) < 0.3) add(i, houseOf(i, {SPR_FACHWERK, SPR_FACHWERK2, SPR_FACHWERK3}), side2() * rnd2(3.5, 5.5));
            if (i % 300 == 150) add(i, SPR_CHURCH, side2() * rnd2(4.2, 6.0));
            if (i % 120 == 100 && !gantry && !sign) add(i, SPR_AUTOBAHN_SIGN, 1.7);
            if (i % 55 == 30) add(i, SPR_WIND_TURBINE, side() * rnd(5.0, 9.0));
            break;
        default:  // 10 Florence
            if (i % 9 == 0 && !gantry && !sign) { add(i, SPR_CYPRESS, -1.7 - rnd(0, 0.2)); add(i, SPR_CYPRESS, 1.7 + rnd(0, 0.2)); }
            if (chance(0.11)) add(i, SPR_OLIVE, side() * rnd(2.4, 7.0));
            if (chance(0.04)) add(i, SPR_CYPRESS, side() * rnd(3.0, 8.0));
            if (i % 35 == 17) add(i, houseOf(i, {SPR_VILLA, SPR_VILLA2, SPR_VILLA3}), side() * rnd(3.4, 6.0));
            if (i % 45 == 8 && rnd2(0, 1) < 0.6) add(i, SPR_VINES, side2() * rnd2(2.8, 5.0));
            break;
        }
    }
}

void Track::build(int st) {
    stage = st;
    theme = makeTheme(st);
    segs.clear();
    jumps_.clear(); narrows_.clear(); tunnels_.clear(); dirts_.clear(); confetti_.clear();
    surface_ = SURF_ASPHALT;
    kerb_ = theme.kerb;
    layout(st);
    length = count() * SEG_LEN;
    // mid-lap checkpoint: half way round, moved forward out of tunnels and width changes
    int cp = count() / 2;
    // the nearest spot to half way, forwards or backwards, that is not in a tunnel, in front of a portal or on
    // a road of unusual width
    auto ok = [&](int i) {
        i = ((i % count()) + count()) % count();
        return !segs[i].tunnel && std::fabs(segs[i].w1 - 1) <= 0.02 && std::fabs(segs[i].w2 - 1) <= 0.02 &&
               !segs[(i + 6) % count()].tunnel;
    };
    for (int d = 0; d < count(); ++d) {
        if (ok(cp + d)) { cp += d; break; }
        if (ok(cp - d)) { cp -= d; break; }
    }
    cp = ((cp % count()) + count()) % count();
    checkpointZ = cp * SEG_LEN;
    decorate(1234u + unsigned(st) * 777u);
}

double Track::widthAt(double z) const {
    double w = wrap(z);
    const Segment& s = find(w);
    double t = (w - s.index * SEG_LEN) / SEG_LEN;
    return s.w1 + (s.w2 - s.w1) * t;
}

double Track::wallAt(double z) const {
    const Segment& s = find(z);
    if (s.tunnel) return TUNNEL_SIDE * widthAt(z);
    if (s.kerb == KERB_GUARDRAIL) return RAIL_POS * widthAt(z);
    return 0;
}

int Track::checkpointCount(double dist) const {
    double lap = std::floor(dist / length);
    double frac = dist - lap * length;
    return int(lap) * 2 + (frac >= checkpointZ ? 1 : 0);
}

double Track::wrap(double z) const {
    double r = std::fmod(z, length);
    return r < 0 ? r + length : r;
}

const Segment& Track::find(double z) const {
    int i = int(std::floor(wrap(z) / SEG_LEN));
    if (i >= count()) i = count() - 1;
    return segs[i];
}

double Track::heightAt(double z) const {
    double w = wrap(z);
    const Segment& s = find(w);
    double t = (w - s.index * SEG_LEN) / SEG_LEN;
    return s.y1 + (s.y2 - s.y1) * t;
}

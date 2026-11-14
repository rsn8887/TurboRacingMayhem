// Tiny real-time stereo synthesizer: engines (the players' and the nearest opponents', panned and with a
// Doppler shift), tyre screech, off-road rumble, a tunnel echo and one-shot effects
#pragma once
#include <SDL.h>

enum Sfx { SFX_BEEP_LOW, SFX_BEEP_HIGH, SFX_CRASH, SFX_BUMP, SFX_CHECKPOINT, SFX_MENU_MOVE, SFX_MENU_SELECT, SFX_TIMEOUT, SFX_LAND, SFX_NITRO, SFX_TUMBLEWEED };

class Audio {
public:
    bool init();
    void initOffline(int rate = 44100);             // no device: render with renderOffline() (tests)
    void renderOffline(float* stereo, int frames) { mix(stereo, frames); }
    void shutdown();
    void setEnabled(bool on);
    bool enabled() const { return on_; }
    // per-player continuous sounds (voice 0 or 1)
    void setEngine(int v, double rpm, double throttle, bool active);
    void setSkid(int v, double amount);
    void setOffroad(int v, double amount);
    void silenceLoops();
    void setTunnel(double amount);        // 0..1 - adds an echo while driving through a tunnel
    // opponent engines heard by the player: voice 0..OPP_VOICES-1; volume 0..1, pan -1 (left)..1 (right),
    // pitch = Doppler factor (> 1 while the car comes closer), tone 0..1 = engine character (big and low ..
    // small and high-revving, from the car type)
    static constexpr int OPP_VOICES = 5;
    void setOpponent(int v, double rpm, double volume, double pan, double pitch, double tone = 0.5);
    void play(Sfx s);

private:
    static void callback(void* user, Uint8* stream, int len);
    void mix(float* out, int n);      // interleaved stereo, n frames

    SDL_AudioDeviceID dev_ = 0;
    bool offline_ = false;
    bool ready() const { return dev_ || offline_; }
    void lock() { if (dev_) SDL_LockAudioDevice(dev_); }
    void unlock() { if (dev_) SDL_UnlockAudioDevice(dev_); }
    bool on_ = true;
    int rate_ = 44100;

    struct Loop {
        double rpm = 0, throttle = 0, skid = 0, offroad = 0; bool active = false;       // targets
        double cRpm = 0, cVol = 0, cSkid = 0, cOff = 0;                                // smoothed
        double ph1 = 0, ph2 = 0, lp = 0, nlp = 0, nlp2 = 0, olp = 0, wob = 0;
    } loop_[2];

    struct Opp {
        double rpm = 0, vol = 0, pan = 0, pitch = 1, tone = 0.5;    // targets
        double cRpm = 0, cVol = 0, cPan = 0, cPitch = 1;            // smoothed
        double ph1 = 0, ph2 = 0, ph3 = 0, lp = 0, lp2 = 0;
    } opp_[OPP_VOICES];

    struct Shot { int kind = -1; double t = 0, lp = 0, lp2 = 0; } shots_[12];
    unsigned noise_ = 22222;
    double tunnel_ = 0, cTunnel_ = 0;
    float echo_[2][8192] = {};
    int echoPos_ = 0;
    float rnd();
};

extern Audio audio;

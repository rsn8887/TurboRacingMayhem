#include "audio.h"
#include <cmath>
#include <vector>
#include <algorithm>

Audio audio;

static const double SHOT_LEN[] = {0.18, 0.45, 1.0, 0.18, 0.36, 0.04, 0.12, 1.2, 0.3, 1.1, 0.4};

float Audio::rnd() {
    noise_ ^= noise_ << 13; noise_ ^= noise_ >> 17; noise_ ^= noise_ << 5;
    return float(noise_ & 0xFFFF) / 32768.0f - 1.0f;
}

bool Audio::init() {
    SDL_AudioSpec want{}, have{};
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
#ifdef __SWITCH__
    // all three game cores render in parallel: a longer buffer (~46 ms) rides out a slow frame without a gap
    want.samples = 2048;
#else
    want.samples = 1024;
#endif
    want.callback = &Audio::callback;
    want.userdata = this;
    dev_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (!dev_) { SDL_Log("Audio disabled: %s", SDL_GetError()); return false; }
    rate_ = have.freq;
    SDL_PauseAudioDevice(dev_, 0);
    return true;
}

void Audio::initOffline(int rate) { offline_ = true; rate_ = rate; }

void Audio::shutdown() { if (dev_) SDL_CloseAudioDevice(dev_); dev_ = 0; }

void Audio::setEnabled(bool on) {
    on_ = on;
    if (!on) silenceLoops();
}

void Audio::setEngine(int v, double rpm, double throttle, bool active) {
    if (!ready()) return;
    lock();
    loop_[v].rpm = rpm; loop_[v].throttle = throttle; loop_[v].active = active && on_;
    unlock();
}
void Audio::setSkid(int v, double a)    { if (!ready()) return; lock(); loop_[v].skid = on_ ? a : 0; unlock(); }
void Audio::setOffroad(int v, double a) { if (!ready()) return; lock(); loop_[v].offroad = on_ ? a : 0; unlock(); }

void Audio::setOpponent(int v, double rpm, double volume, double pan, double pitch, double tone) {
    if (!ready() || v < 0 || v >= OPP_VOICES) return;
    lock();
    Opp& o = opp_[v];
    o.rpm = rpm; o.vol = on_ ? volume : 0; o.pan = pan; o.pitch = pitch; o.tone = tone;
    unlock();
}

void Audio::silenceLoops() {
    if (!ready()) return;
    lock();
    for (auto& l : loop_) { l.active = false; l.skid = 0; l.offroad = 0; }
    for (auto& o : opp_) o.vol = 0;
    unlock();
}

void Audio::setTunnel(double a) {
    if (!ready()) return;
    lock();
    tunnel_ = on_ ? a : 0;
    unlock();
}

void Audio::play(Sfx s) {
    if (!ready() || !on_) return;
    lock();
    for (auto& sh : shots_)
        if (sh.kind < 0) { sh.kind = s; sh.t = 0; sh.lp = sh.lp2 = 0; break; }
    unlock();
}

void Audio::callback(void* user, Uint8* stream, int len) {
    Audio* a = static_cast<Audio*>(user);
    int n = len / 4;                               // stereo frames
    static std::vector<float> buf;
    buf.assign(size_t(n) * 2, 0.0f);
    a->mix(buf.data(), n);
    Sint16* out = reinterpret_cast<Sint16*>(stream);
    for (int i = 0; i < n * 2; ++i) out[i] = Sint16(std::tanh(buf[size_t(i)]) * 30000.0f);
}

void Audio::mix(float* out, int n) {
    const double dt = 1.0 / rate_;
    const double tau = 2 * M_PI;
    for (int i = 0; i < n; ++i) {
        double s = 0;                 // centred sounds (the players' own cars, effects)
        double sl = 0, sr = 0;        // panned sounds (opponents)
        for (auto& O : opp_) {
            const double k = 0.0015;
            O.cRpm += (O.rpm - O.cRpm) * k * 4;
            O.cVol += (O.vol - O.cVol) * k;
            O.cPan += (O.pan - O.cPan) * k * 2;
            O.cPitch += (O.pitch - O.cPitch) * k * 2;
            if (O.cVol < 0.0005) continue;
            // a raspier engine than the player's (pulse + saw + a detuned third partial), pitched by the car
            // type and shifted by the Doppler factor; further away it sounds duller
            double f = (40 + O.cRpm * 165) * (0.8 + 0.45 * O.tone) * O.cPitch;
            O.ph1 += f * dt;          if (O.ph1 >= 1) O.ph1 -= 1;
            O.ph2 += f * 0.5 * dt;    if (O.ph2 >= 1) O.ph2 -= 1;
            O.ph3 += f * 1.503 * dt;  if (O.ph3 >= 1) O.ph3 -= 1;
            double saw = 2 * O.ph1 - 1, sub = O.ph2 < 0.5 ? 1 : -1, pulse = O.ph1 < 0.3 ? 1 : -0.43;
            double raw = saw * 0.45 + pulse * 0.4 + sub * 0.35 + (2 * O.ph3 - 1) * 0.18 + rnd() * 0.12;
            double cut = (500 + O.cRpm * 2000) * O.cPitch * (0.45 + 0.55 * std::min(1.0, O.cVol * 1.5));
            double a1 = 1 - std::exp(-tau * cut * dt);
            O.lp += (raw - O.lp) * a1;
            O.lp2 += (O.lp - O.lp2) * a1;
            double v = O.lp2 * O.cVol * 0.42;
            double ang = (O.cPan + 1) * M_PI / 4;   // equal-power pan
            sl += v * std::cos(ang) * 1.41;
            sr += v * std::sin(ang) * 1.41;
        }
        // --- continuous loops per player
        for (auto& L : loop_) {
            double k = 0.0015;
            L.cRpm += (L.rpm - L.cRpm) * k * 4;
            double vol = L.active ? 0.16 + 0.14 * L.throttle : 0.0;
            L.cVol += (vol - L.cVol) * k;
            L.cSkid += (L.skid - L.cSkid) * k;
            L.cOff += (L.offroad - L.cOff) * k;
            if (L.cVol > 0.0005) {
                double f = 32 + L.cRpm * 150;
                L.ph1 += f * dt;       if (L.ph1 >= 1) L.ph1 -= 1;
                L.ph2 += f * 0.5 * dt; if (L.ph2 >= 1) L.ph2 -= 1;
                double saw = 2 * L.ph1 - 1, sub = L.ph2 < 0.5 ? 1 : -1;
                double raw = saw * 0.7 + sub * 0.45 + rnd() * 0.12;
                double cut = 500 + L.cRpm * 1800;
                double alpha = 1 - std::exp(-tau * cut * dt);
                L.lp += (raw - L.lp) * alpha;
                s += L.lp * L.cVol;
            }
            if (L.cSkid > 0.002) {
                double nz = rnd();
                L.nlp += (nz - L.nlp) * 0.45;          // remove highest hiss
                L.nlp2 += (L.nlp - L.nlp2) * 0.08;     // remove lows -> band pass
                L.wob += 11 * dt; if (L.wob > 1) L.wob -= 1;
                double bp = (L.nlp - L.nlp2) * (0.7 + 0.3 * std::sin(tau * L.wob));
                s += bp * L.cSkid * 0.55;
            }
            if (L.cOff > 0.002) {
                L.olp += (rnd() - L.olp) * 0.03;
                s += L.olp * L.cOff * 1.6;
            }
        }
        // --- one-shots
        for (auto& sh : shots_) {
            if (sh.kind < 0) continue;
            double t = sh.t, len = SHOT_LEN[sh.kind];
            double env = 1 - t / len;
            double v = 0;
            auto sq = [&](double f) { return std::fmod(t * f, 1.0) < 0.5 ? 1.0 : -1.0; };
            switch (sh.kind) {
            case SFX_BEEP_LOW:  v = sq(440) * 0.25 * (env > 0.1 ? 1 : env * 10); break;
            case SFX_BEEP_HIGH: v = sq(880) * 0.25 * (env > 0.1 ? 1 : env * 10); break;
            case SFX_CRASH:     v = rnd() * 0.9 * env * env + std::sin(tau * 55 * t) * 0.8 * std::exp(-t * 6); break;
            case SFX_BUMP:      v = rnd() * 0.4 * env + std::sin(tau * 90 * t) * 0.6 * env; break;
            case SFX_CHECKPOINT: {
                double f = t < 0.12 ? 784 : t < 0.24 ? 988 : 1319;
                v = sq(f) * 0.18 * (env > 0.2 ? 1 : env * 5);
                break;
            }
            case SFX_MENU_MOVE:   v = sq(1200) * 0.12; break;
            case SFX_MENU_SELECT: v = sq(700 + t * 6000) * 0.15 * env; break;
            case SFX_LAND:        v = std::sin(tau * (70 - t * 80) * t) * 0.9 * env * env + rnd() * 0.25 * std::exp(-t * 25); break;
            case SFX_TIMEOUT:     v = sq(t < 0.4 ? 330 : t < 0.8 ? 262 : 196) * 0.2 * (env > 0.1 ? 1 : env * 10); break;
            case SFX_NITRO: {     // whoosh: band-passed noise sweeping up, over a rising low roar
                double k = 0.04 + 0.30 * std::min(1.0, t / 0.35);
                sh.lp += (rnd() - sh.lp) * k;
                sh.lp2 += (sh.lp - sh.lp2) * 0.04;
                double att = std::min(1.0, t / 0.05) * env * env;
                v = (sh.lp - sh.lp2) * 1.6 * att + std::sin(tau * (60 + t * 90) * t) * 0.35 * att;
                break;
            }
            case SFX_TUMBLEWEED: {   // dry twigs snapping: a soft thump, a burst of crackles and a papery rustle
                double nz = rnd();
                sh.lp += (nz - sh.lp) * 0.35;
                double hiss = nz - sh.lp;                                     // high-passed: dry and papery
                double crackle = 0;
                if (rnd() > 0.88 - 0.1 * env) crackle = rnd() * 1.4;          // sparse snaps, thicker at first
                sh.lp2 += (crackle - sh.lp2) * 0.6;                           // each snap a short click
                double thump = std::sin(tau * (140 - t * 120) * t) * std::exp(-t * 28);
                v = hiss * 0.35 * env * env + (crackle - sh.lp2) * 0.55 * env + thump * 0.45;
                break;
            }
            }
            s += v;
            sh.t += dt;
            if (sh.t >= len) sh.kind = -1;
        }
        // tunnel echo: short feedback delay (~75 ms; a little longer on the right for some width)
        cTunnel_ += (tunnel_ - cTunnel_) * 0.0005;
        double ch[2] = {s + sl, s + sr};
        for (int c = 0; c < 2; ++c) {
            int delay = std::min(8191, int(rate_ * (c ? 0.083 : 0.075)));
            float d = echo_[c][(echoPos_ - delay + 8192) % 8192];
            echo_[c][echoPos_] = float(ch[c] * cTunnel_ + d * 0.45);
            ch[c] = ch[c] * (1 + 0.25 * cTunnel_) + d * 0.6;
            out[i * 2 + c] = float(ch[c] * 0.8);
        }
        echoPos_ = (echoPos_ + 1) % 8192;
    }
}

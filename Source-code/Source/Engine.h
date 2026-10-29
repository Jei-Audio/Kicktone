#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
#include <cstdint>
#include "ReverbHighPass.h"

namespace pt {
constexpr double pi = 3.14159265358979323846;
// Prepared before playback, shared by all voices. Linear interpolation between
// 1025 phase rows closely reproduces the original 16-tap windowed sinc kernel.
struct SincTable {
    std::array<std::array<double,16>,1025> rows{};
    SincTable() {
        for(size_t phase=0;phase<rows.size();++phase) {
            double sum=0;
            for(int k=-7;k<=8;++k) {
                const double d=double(phase)/1024.0-k, x=pi*d;
                const double w=(std::abs(x)<1e-10 ? 1.0 : std::sin(x)/x)
                    *(0.5+0.5*std::cos(pi*d/8.0));
                rows[phase][size_t(k+7)]=w; sum+=w;
            }
            for(auto& w:rows[phase]) w/=(std::max)(0.001,sum);
        }
    }
};
inline const SincTable& sincTable() { static const SincTable table; return table; }
struct Sample {
    double rate = 44100;
    std::vector<float> left, right;
    double duration() const { return left.size() / rate; }
    float at(int ch, double pos, double step = 1) const {
        const auto& a = ch == 0 ? left : right;
        if (pos < 0 || pos >= double(a.size()) || a.empty()) return 0;
        const auto exact = static_cast<size_t>(pos);
        if (step <= 1.0000001 && std::abs(pos-double(exact)) < 1e-8) return a[exact];
        if(step <= 1.0000001) {
            const double phase=(pos-double(exact))*1024.0;
            const auto row=(std::min)(size_t(1023),size_t(phase));
            const double mix=phase-double(row);
            const auto& table=sincTable();
            double sum=0;
            for(int k=-7;k<=8;++k) {
                const auto i=static_cast<int64_t>(exact)+k;
                if(i>=0 && i<static_cast<int64_t>(a.size())) {
                    const size_t tap=size_t(k+7);
                    const double w=table.rows[row][tap]+mix*(table.rows[row+1][tap]-table.rows[row][tap]);
                    sum+=a[size_t(i)]*w;
                }
            }
            return float(sum);
        }
        // Windowed-sinc interpolation; reduce cutoff when transposing upward.
        const double cutoff = (std::min)(1.0, 1.0 / (std::max)(1.0, step));
        const auto centre = static_cast<int64_t>(std::floor(pos));
        double sum = 0, weights = 0;
        for (int k = -7; k <= 8; ++k) {
            const auto i = centre + k;
            const double d = pos - double(i);
            const double x = pi * d * cutoff;
            const double w = (std::abs(x) < 1e-10 ? 1.0 : std::sin(x) / x)
                           * (0.5 + 0.5 * std::cos(pi * d / 8.0)) * cutoff;
            if (i >= 0 && i < static_cast<int64_t>(a.size())) sum += a[static_cast<size_t>(i)] * w;
            weights += w;
        }
        return static_cast<float>(sum / (std::max)(0.001, weights));
    }
};
struct Pitch { double hz = 0, midi = 36, confidence = 0; bool valid = false; };
inline Pitch detect(const Sample& s) {
    Pitch result;
    if (s.left.size() < 512 || s.rate < 1000) return result;
    const int decimation = (std::max)(1, int(s.rate / 6000));
    const double rate = s.rate / decimation;
    std::vector<float> data;
    // Analyze the louder channel, avoiding stereo phase cancellation.
    double el = 0, er = 0;
    for (size_t i = 0; i < s.left.size(); ++i) { el += s.left[i]*s.left[i]; er += s.right[i]*s.right[i]; }
    const auto& src = el >= er ? s.left : s.right;
    for (size_t i = 0; i + decimation <= src.size(); i += decimation) {
        double v = 0; for (int j = 0; j < decimation; ++j) v += src[i+j];
        data.push_back(float(v / decimation));
    }
    const int maxLag = int(rate / 18.0), minLag = (std::max)(2, int(rate / 1200.0));
    const int n = (std::min)(2048, int(data.size()) - maxLag);
    if (n < maxLag * 2) return result;
    std::vector<double> estimates, confidences;
    for (int frame = 0; frame < 5; ++frame) {
        const int start = int((data.size() - n - maxLag) * (0.12 + frame * 0.16));
        double energy = 0; for (int i = 0; i < n; ++i) energy += data[start+i]*data[start+i];
        if (energy / n < 1e-8) continue;
        std::vector<double> diff(size_t(maxLag+1), 0.0);
        double running = 0;
        for (int lag = 1; lag <= maxLag; ++lag) {
            double d = 0; for (int i = 0; i < n; ++i) { const double delta = data[start+i] - data[start+i+lag]; d += delta*delta; }
            running += d; diff[lag] = running > 1e-12 ? d * lag / running : 1;
        }
        int lag = minLag;
        for (; lag < maxLag-1; ++lag) if (diff[lag] < 0.18) {
            while (lag < maxLag-1 && diff[lag+1] < diff[lag]) ++lag;
            break;
        }
        if (lag >= maxLag-1) continue;
        const double denom = diff[lag-1] - 2*diff[lag] + diff[lag+1];
        const double delta = std::abs(denom) > 1e-12 ? std::clamp(0.5*(diff[lag-1]-diff[lag+1])/denom,-0.5,0.5) : 0;
        estimates.push_back(69 + 12*std::log2((rate/(lag+delta))/440));
        confidences.push_back(1-diff[lag]);
    }
    if (estimates.size() < 2) return result;
    auto sorted = estimates; std::sort(sorted.begin(), sorted.end());
    const double median = sorted[sorted.size()/2];
    double sum = 0, conf = 0; int count = 0;
    for (size_t i = 0; i < estimates.size(); ++i) if (std::abs(estimates[i]-median) < 0.5) { sum += estimates[i]; conf += confidences[i]; ++count; }
    if (count < 2) return result;
    result.midi = sum/count; result.hz = 440*std::pow(2,(result.midi-69)/12);
    result.confidence = conf/count * double(count)/estimates.size(); result.valid = result.confidence >= 0.6;
    return result;
}
struct Settings {
    double root = 36, trimMs = 0, punchEndOffsetMs = 0, joinMs = 0, crossfadeMs = 8;
    // Maps source punch trim to the prepared playback duration. 1 = aligned.
    double punchTimeScale = 1;
    double punchGain = 1, tailGain = 1, output = 0.7;
    double crunchMix = 0; // v0.4.53: parallel crunch layer amount (0..1), updated live
    // Worker-measured positive overlap correlation; conservative linear fallback.
    double joinCorrelation = 1;
    double reverbTrimMs = 0, reverbDelayMs = 0, reverbGain = 1;
    int reverbLowCutSlope = 0; // 0 (OFF), 12, 24 or 48 dB/oct at 200 Hz
    bool gate = true;
    // Alignment changes ONLY the pitched tail sampling phase; the punch, reverb,
    // envelope timing and prepared MIDI note bank are untouched.
    double phaseShiftMs = 0;
    bool invertTail = false;
    bool invertPunch = false;
    // Root-rendered Tail + generated Reverb composites have already been
    // trimmed and sent through one shared pitch/stretch render.
    bool preparedCompositeTail = false;
    double compositeDryDuration = 0.0;
    // v0.4.57: shorten the END of the dry Tail only. Trim is measured from the
    // end of the full source file (scaled like trimMs for prepared notes);
    // 0 = off and exactly unchanged. Fade ends at the cut point. Reverb sends
    // and the generated/recorded reverbs keep using the uncut Tail.
    double tailEndTrimMs = 0.0, tailEndFadeMs = 10.0;
    // v0.4.62 ATTACK LAYER: a short synthesized click mixed into the Punch bus.
    // A sine whose pitch falls exponentially from attackStartHz to the played
    // note's fundamental over attackLengthMs (1-50 ms since v0.5.1), with a raised-cosine
    // fade-out. OFF = bit-identical output.
    // v0.5.13 END: where the sweep stops. attackEndHz < kAttackEndKeyHz (the
    // default, "KEY") keeps the played note's fundamental as before.
    bool attackOn = false;
    double attackLengthMs = 3.0, attackStartHz = 4000.0, attackGain = 0.5;
    double attackEndHz = 0.0;
};
// Fundamental of an internal (bank) note, e.g. 36 = C2 MIDI / C3 in FL Studio = 65.4 Hz.
inline double noteHz(double note) { return 440.0*std::pow(2.0,(note-69.0)/12.0); }
// v0.5.13 ATTACK LAYER END: 19 Hz and below = KEY (the played note, the
// pre-v0.5.13 behaviour); 20-12000 Hz = a fixed end pitch for every key.
constexpr double kAttackEndKeyHz = 20.0;
inline double attackEndFor(double endSetting,double note) { return endSetting<kAttackEndKeyHz?noteHz(note):endSetting; }
// One sample of the attack layer at time t (seconds). phase is advanced in place.
inline double attackLayerSample(double t,double outputRate,double lengthMs,double startHz,double endHz,double& phase) {
    const double length=std::clamp(lengthMs,1.0,50.0)*.001;   // v0.5.1: up to 50 ms
    if(t<0 || t>=length)return 0.0;
    const double x=t/length;
    const double hz=std::clamp(startHz,20.0,20000.0)*std::pow(std::clamp(endHz,10.0,20000.0)/std::clamp(startHz,20.0,20000.0),x);
    const double value=std::sin(phase)*(0.5+0.5*std::cos(pi*x));
    phase+=2.0*pi*(std::min)(hz,outputRate*.45)/outputRate;
    if(phase>2.0*pi)phase-=2.0*pi;
    return value;
}
// Smooth (raised-cosine) fade that reaches 0 at the cut position. Identity
// when the cut is off, so legacy audio stays sample-identical.
inline double tailEndFadeMsClamped(double fadeMs) { return std::clamp(std::isfinite(fadeMs)?fadeMs:10.0, 1.0, 2000.0); }
inline double tailEndGain(double position, double cutPosition, double fadeSeconds) {
    const double x=std::clamp((cutPosition-position)/(std::max)(1.e-6,fadeSeconds),0.0,1.0);
    return 0.5-0.5*std::cos(pi*x);
}
inline bool sameTailEnd(double trimA,double fadeA,double trimB,double fadeB) {
    if(trimA<=0 && trimB<=0)return true;
    return std::abs(trimA-trimB)<.01 && std::abs(tailEndFadeMsClamped(fadeA)-tailEndFadeMsClamped(fadeB))<.01;
}
struct Voice {
    bool active = false; int note = 36, channel = 1;
    int key = -1, punchIndex = -1; // v0.4.48: played MIDI key and punch lane index (-1: use note)
    // v0.4.49 legato glide: the playing voice switches to a new note's tail at
    // the same position and reads it at a rate that slides from the old pitch
    // to the new one. A short crossfade from the old tail prevents a click.
    bool gliding = false;
    int previousNote = -1;
    double tailOffset = 0, previousTailOffset = 0;      // tail seconds added by glide rate changes
    double glideSemitones = 0, glideSeconds = 0, glideElapsed = 0;
    double glideFadeSeconds = 0, glideFadeElapsed = 0;
    // v0.5.10: the old tail keeps the rate it was heard at when the switch
    // happened (1 unless a new key arrived mid-slide) for the whole crossfade,
    // and the crossfade law follows how well the two tails were aligned.
    double previousRate = 1, glideCorrelation = 0;
    // v0.5.23 STRETCHED GLIDE (experimental, user request): the slide keeps the
    // timeline of the prepared notes instead of reading the target faster or
    // slower like tape. The voice reads the prepared note nearest the heard
    // pitch at a small rate (at most half a semitone) for the pitch in between
    // and steps to the next note - same place in the tail, phase-aligned, short
    // crossfade - once the heard pitch is more than half a semitone away. The
    // pitch curve is the classic one: linear in semitones over GLIDE TIME.
    bool stretchedGlide = false;
    int glideTarget = -1;
    double glideNominalOffset = 0;   // drift- and alignment-free timeline offset of the note being read
    double heardGlideNote() const {
        const double x=std::clamp(glideElapsed/glideSeconds,0.0,1.0);
        return glideTarget+glideSemitones*(1.0-x);
    }
    void glideToStretched(int target, double seconds) {
        // Starts on the note being heard (no switch, no crossfade); a key
        // pressed mid-slide continues from the pitch heard right now.
        const double heard = note + (gliding ? 12.0*std::log2(glideRate()) : 0.0);
        if(!stretchedGlide) glideNominalOffset = tailOffset;   // from note-on or a classic slide
        glideTarget = target; glideSemitones = heard-target; glideSeconds = (std::max)(1.e-4, seconds); glideElapsed = 0;
        gliding = true; stretchedGlide = true;
    }
    bool stretchedStepDue() const { return gliding && stretchedGlide && !glideFadeActive() && std::abs(heardGlideNote()-note) > 0.5; }
    int stretchedStepNote() const { return std::clamp(int(std::lround(heardGlideNote())), 0, 127); }
    void stepStretchedGlide(int newNote, double fadeSeconds = 0.006) {
        previousRate = glideRate(); previousNote = note; previousTailOffset = tailOffset; note = newNote;
        glideFadeSeconds = (std::max)(1.e-4, fadeSeconds); glideFadeElapsed = 0; glideCorrelation = 0;
    }
    // Timeline offset of the next note, whose prepared length is `scale` x the
    // current note's (Adaptive Stretch): the same relative place of the tail.
    // Equal lengths (every other engine) keep it unchanged, so alignment shifts
    // and read-rate drift never accumulate from step to step.
    double nextTimelineOffset(double scale) {
        if(scale!=1.0) glideNominalOffset=(tailBase()+glideNominalOffset)*scale-tailBase();
        return glideNominalOffset;
    }
    // Phase-align the new note around its timeline position, within one period
    // of the heard pitch, so the offset never wanders. Bounded loops, linear
    // interpolation, no allocation (~40k reads per step on the audio thread).
    void alignStretchedStep(const Sample* oldTail, const Sample* newTail, double centreOffset) {
        tailOffset = centreOffset;
        if(!gliding || !oldTail || !newTail || time<tailStart || oldTail->rate<=0 || newTail->rate<=0) return;
        const double base=tailBase(), step=compositeTail?1.0:ratio;
        const double oldStep=previousRate*step, newStep=glideRate()*step;
        const double period=1.0/std::clamp(noteHz(heardGlideNote()),20.0,500.0);
        constexpr int kPoints=96; const double dt=std::clamp(period,0.008,0.03)/kPoints;
        const auto read=[](const Sample& s,int ch,double pos)->double {
            const auto& a=ch==0?s.left:s.right;
            if(pos<0 || a.size()<2 || pos>=double(a.size()-1))return 0.0;
            const auto i=size_t(pos); const double f=pos-double(i);
            return a[i]+(a[i+1]-a[i])*f;
        };
        const auto score=[&](double shift)->double {
            double ab=0,aa=0,bb=0;
            for(int k=0;k<kPoints;++k) {
                const double t=k*dt;
                for(int ch=0;ch<2;++ch) {
                    const double a=read(*oldTail,ch,(base+previousTailOffset+t*oldStep)*oldTail->rate);
                    const double b=read(*newTail,ch,(base+centreOffset+shift+t*newStep)*newTail->rate);
                    ab+=a*b;aa+=a*a;bb+=b*b;
                }
            }
            return (aa>1e-12&&bb>1e-12)?ab/std::sqrt(aa*bb):-2.0;
        };
        const auto penalised=[&](double shift){return score(shift)-0.05*std::abs(shift)/period;};
        double bestScore=-3, bestShift=0;
        for(int c=-32;c<=32;++c) {                        // coarse: one period in 64 steps
            const double shift=c*period/64.0, s=penalised(shift);
            if(s>bestScore){bestScore=s;bestShift=shift;}
        }
        if(bestScore<=-1.5) return;                       // silent: stay on the timeline
        const double coarse=bestShift, fineStep=period/1024.0;
        double fineScores[33];
        for(int c=-16;c<=16;++c) {                        // fine: +-1/64 period
            const double shift=coarse+c*fineStep, s=penalised(shift);
            fineScores[c+16]=s;
            if(s>bestScore){bestScore=s;bestShift=shift;}
        }
        int bi=int(std::lround((bestShift-coarse)/fineStep))+16;
        if(bi>0 && bi<32) {
            const double l=fineScores[bi-1],m=fineScores[bi],r=fineScores[bi+1],den=l-2*m+r;
            if(den<-1e-12)bestShift+=std::clamp(0.5*(l-r)/den,-0.5,0.5)*fineStep;
        }
        glideCorrelation=std::clamp(score(bestShift),0.0,1.0);
        tailOffset=centreOffset+bestShift;
    }
    void glideTo(int newNote, double fromSemitones, double seconds, double fadeSeconds = 0.006) {
        // A key pressed while a slide is still running continues from the pitch
        // heard right now, not from the previous target: no pitch jump.
        const double heardSemitones = gliding ? 12.0*std::log2(glideRate()) : 0.0;
        stretchedGlide = false;
        previousRate = gliding ? glideRate() : 1.0;
        previousNote = note; previousTailOffset = tailOffset; note = newNote; tailOffset = previousTailOffset;
        glideSemitones = fromSemitones+heardSemitones; glideSeconds = (std::max)(1.e-4, seconds); glideElapsed = 0;
        glideFadeSeconds = (std::max)(1.e-4, fadeSeconds); glideFadeElapsed = 0; gliding = true; glideCorrelation = 0;
    }
    double tailBase() const {
        return compositeTail ? time-tailStart-settings.phaseShiftMs/1000.0
                             : tailTrim+(time-tailStart)*ratio-settings.phaseShiftMs/1000.0;
    }
    // Phase-align the new tail to the old one at the switch point so the slide
    // starts without a shortened or doubled cycle (the "off phase" glide click).
    // v0.5.10: the old 4 ms / +-12 ms / 0.25 ms search was fooled by low or
    // clipped tails (a 4 ms piece of a 40-60 Hz cycle correlates with almost
    // any same-polarity piece), so it sometimes settled up to ~120 deg off.
    // Now: 20 ms window (at least one cycle of the lowest bank note), +-20 ms
    // range in 0.25 ms steps, then a 1/96 ms fine pass and a parabolic peak.
    // Linear interpolation on the audio thread, bounded loops, no allocation.
    void alignGlide(const Sample* oldTail, const Sample* newTail) {
        if(!gliding || !oldTail || !newTail || time<tailStart || oldTail->rate<=0 || newTail->rate<=0) return;
        const double base=tailBase(), rate0=std::pow(2.0,glideSemitones/12.0)*(compositeTail?1.0:ratio);
        const double oldStep=previousRate*(compositeTail?1.0:ratio);
        const auto read=[](const Sample& s,int ch,double pos)->double {
            const auto& a=ch==0?s.left:s.right;
            if(pos<0 || a.size()<2 || pos>=double(a.size()-1))return 0.0;
            const auto i=size_t(pos); const double f=pos-double(i);
            return a[i]+(a[i+1]-a[i])*f;
        };
        constexpr int kPoints=240; constexpr double kDt=0.02/kPoints;   // 20 ms window
        const auto score=[&](double shift)->double {
            double ab=0,aa=0,bb=0;
            for(int k=0;k<kPoints;++k) {
                const double t=k*kDt;
                for(int ch=0;ch<2;++ch) {
                    const double a=read(*oldTail,ch,(base+previousTailOffset+t*oldStep)*oldTail->rate);
                    const double b=read(*newTail,ch,(base+previousTailOffset+shift+t*rate0)*newTail->rate);
                    ab+=a*b;aa+=a*a;bb+=b*b;
                }
            }
            return (aa>1e-12&&bb>1e-12)?ab/std::sqrt(aa*bb):-2.0;
        };
        const auto penalised=[&](double shift){return score(shift)-0.02*std::abs(shift)/0.012;};
        double bestScore=-3, bestShift=0;
        for(int c=-80;c<=80;++c) {                       // coarse: +-20 ms, 0.25 ms
            const double shift=c*0.00025, s=penalised(shift);
            if(s>bestScore){bestScore=s;bestShift=shift;}
        }
        if(bestScore<=-1.5) { glideCorrelation=0; return; }      // silent: keep the position
        const double coarse=bestShift;
        double fineScores[49];
        for(int c=-24;c<=24;++c) {                       // fine: +-0.25 ms, 1/96 ms
            const double shift=coarse+c/96000.0, s=penalised(shift);
            fineScores[c+24]=s;
            if(s>bestScore){bestScore=s;bestShift=shift;}
        }
        // Parabolic peak between the fine neighbours (sub-step phase).
        int bi=int(std::lround((bestShift-coarse)*96000.0))+24;
        if(bi>0 && bi<48) {
            const double l=fineScores[bi-1],m=fineScores[bi],r=fineScores[bi+1],den=l-2*m+r;
            if(den<-1e-12)bestShift+=std::clamp(0.5*(l-r)/den,-0.5,0.5)/96000.0;
        }
        glideCorrelation=std::clamp(score(bestShift),0.0,1.0);
        tailOffset=previousTailOffset+bestShift;
    }
    double glideRate() const {
        if(!gliding)return 1.0;
        if(stretchedGlide)return std::pow(2.0,(heardGlideNote()-note)/12.0);   // v0.5.23: relative to the note being read
        const double x=std::clamp(glideElapsed/glideSeconds,0.0,1.0);
        return std::pow(2.0,glideSemitones*(1.0-x)/12.0);   // linear in semitones
    }
    bool glideFadeActive() const { return gliding && glideFadeElapsed < glideFadeSeconds; }
    double time = 0, ratio = 1, punchEnd = 0, tailStart = 0, tailTrim = 0, tailEnd = 0, overlap = 0;
    double reverbStart = 0, reverbTrim = 0, reverbEnd = 0;
    double generatedTailReverbStart = 0, generatedTailReverbEnd = 0;
    double compositeTailEnd = 0;
    double dryEnd = 0;            // v0.4.57: audible end of the (possibly cut) dry Tail
    bool compositeTail = false;
    double tailCutGain(const Sample* sample, double position) const {
        if(compositeTail || !sample || settings.tailEndTrimMs<=0) return 1.0;
        return tailEndGain(position, sample->duration()-settings.tailEndTrimMs*.001, tailEndFadeMsClamped(settings.tailEndFadeMs)*.001);
    }
    double velocity = 1, release = -1, releaseLength = 0.006;
    double attackPhase = 0; // v0.4.62 attack layer oscillator
    Settings settings;
    ReverbHPFState reverbFilter, incomingReverbFilter;
    int reverbFilterTransition=0, reverbFilterTransitionLength=1;
    void setReverbFilter(const ReverbHPFDesign& design, double outputRate, bool immediate=false) {
        // A slope change on a held note crossfades over 10 ms. New notes
        // adopt the chosen slope immediately, without a delayed onset.
        if (!immediate && design.slope==filterSlope()) return;
        if (immediate) {
            reverbFilter.reset(design);
            incomingReverbFilter.reset(design);
            reverbFilterTransition=0;
        } else {
            incomingReverbFilter.reset(design);
            reverbFilterTransitionLength=(std::max)(1,int(std::round(outputRate*0.010)));
            reverbFilterTransition=reverbFilterTransitionLength;
        }
    }
    int filterSlope() const {
        return reverbFilterTransition ? incomingReverbFilter.design.slope : reverbFilter.design.slope;
    }
    void start(int key, int chan, float vel, const Sample* punch, const Sample* tail, Settings s, const Sample* reverb = nullptr,
               double generatedTailReverbDuration = 0.0, double generatedTailReverbOffsetMs = 0.0,
               bool preparedComposite = false, double compositeDryDurationSeconds = 0.0) {
        settings = s; note = key; channel = chan; time = 0; release = -1; velocity = vel; attackPhase = 0;
        gliding = false; previousNote = -1; tailOffset = previousTailOffset = 0; glideElapsed = glideFadeElapsed = 0;
        previousRate = 1; glideCorrelation = 0;
        stretchedGlide = false; glideTarget = -1; glideNominalOffset = 0;
        compositeTail = preparedComposite;
        reverbFilterTransition=0;reverbFilter.reset({});incomingReverbFilter.reset({});
        ratio = std::pow(2.0, (key - s.root)/12.0);
        // Trim the END of the original punch without moving its onset or
        // moving the source trim point. Prepared resampling scales this trim too.
        punchEnd = punch ? (std::max)(0.0, punch->duration() - (std::max)(0.0, s.punchEndOffsetMs) * s.punchTimeScale / 1000.0) : 0;
        tailTrim = tail && !compositeTail ? std::clamp(s.trimMs/1000.0, 0.0, tail->duration()) : 0;
        const double tailDuration = tail ? (compositeTail
            ? std::clamp(compositeDryDurationSeconds,0.0,tail->duration())
            : (tail->duration()-tailTrim)/ratio) : 0;
        const double fade = tailDuration > 0 ? (std::min)({s.crossfadeMs/1000.0,punchEnd,tailDuration}) : 0;
        tailStart = tailDuration > 0 ? (std::max)(0.0, punchEnd-fade+s.joinMs/1000.0) : punchEnd;
        tailEnd = tailStart+tailDuration;
        compositeTailEnd = compositeTail && tail ? tailStart+tail->duration() : tailEnd;
        overlap = tailDuration > 0 ? (std::max)(0.0,(std::min)(punchEnd,tailEnd)-tailStart) : 0;
        dryEnd = tailEnd;
        if(tail && !compositeTail && s.tailEndTrimMs > 0)
            dryEnd = std::clamp(tailStart+(tail->duration()-s.tailEndTrimMs*.001-tailTrim)/ratio, tailStart, tailEnd);
        // Independent reverb is a recorded audio file, not an effect on the pitched
        // tail. Offset skips the beginning of that file; its audible start is
        // sample-aligned after the END of the trimmed punch, plus its own
        // non-negative playback delay. Neither knob changes tail positioning.
        reverbTrim = reverb ? std::clamp(s.reverbTrimMs/1000.0, 0.0, reverb->duration()) : 0;
        reverbStart = punchEnd + std::clamp(s.reverbDelayMs, 0.0, 300.0) / 1000.0;
        reverbEnd = reverbStart + (reverb ? reverb->duration()-reverbTrim : 0.0);
        generatedTailReverbStart=tailStart+std::clamp(generatedTailReverbOffsetMs,0.0,300.0)*.001;
        generatedTailReverbEnd=generatedTailReverbStart+(std::max)(0.0,generatedTailReverbDuration);
        active = punchEnd > 0 || tailDuration > 0 || reverbEnd > reverbStart;
        active = active || generatedTailReverbEnd > generatedTailReverbStart;
        active = active || compositeTailEnd > tailEnd;
    }
    double audibleEnd() const { return (std::max)({punchEnd, tailEnd, compositeTailEnd, reverbEnd > reverbStart ? reverbEnd : 0.0, generatedTailReverbEnd > generatedTailReverbStart ? generatedTailReverbEnd : 0.0}); }
    void stop() { if (release < 0) release = 0; }
    // tailSend is the UNCUT dry Tail (v0.4.57): reverb sends/ducking use it so
    // the Tail end cut never changes the reverb. Equal to tail when no cut.
    struct Parts { std::array<float,2> punch {}, tail {}, reverb {}, tailReverb {}, generatedTailReverb {}, tailSend {}; };
    // Keep the original voice timing, crossfade, pitch and reverb filter, but
    // expose two buses so post-voice distortion cannot contaminate the punch.
    Parts nextParts(const Sample* punch, const Sample* tail, double outputRate, const Sample* reverb = nullptr,
                    const Sample* generatedTailReverb = nullptr, const Sample* previousTail = nullptr,
                    const Sample* crunchLayer = nullptr, const Sample* previousCrunchLayer = nullptr) {
        Parts out{};
        if (!active) return out;
        if (time >= audibleEnd() || release >= releaseLength) { active = false; return out; }
        double pg = 1, tg = 1;
        if (overlap > 0 && time >= tailStart) {
            const double x = std::clamp((time-tailStart)/overlap,0.0,1.0);
            pg = std::cos(x*pi/2); tg = std::sin(x*pi/2);
            // Correlated sources must not gain +3 dB at the centre. Never
            // boost cancellation: negative correlation keeps equal-power law.
            const double rho=std::clamp(settings.joinCorrelation,0.0,1.0);
            const double norm=std::sqrt(1.0+2.0*rho*pg*tg);
            pg/=norm;tg/=norm;
        } else if (overlap <= 0) {
            // Tiny edge ramps keep even a zero-overlap join from clicking.
            pg *= std::clamp((punchEnd-time)/0.0005,0.0,1.0);
            tg *= std::clamp((time-tailStart)/0.0005,0.0,1.0);
        }
        pg *= std::clamp(time/0.0003,0.0,1.0);
        if (!compositeTail) tg *= std::clamp((tailEnd-time)/0.002,0.0,1.0);
        const double env = velocity*settings.output*(release < 0 ? 1 : (std::max)(0.0,1-release/releaseLength));
        // v0.4.62: one mono attack-layer sample for both channels (same phase).
        double attack=0;
        if(settings.attackOn && time<.050)   // v0.5.1: LENGTH up to 50 ms (was 5)
            attack=attackLayerSample(time,outputRate,settings.attackLengthMs,settings.attackStartHz,attackEndFor(settings.attackEndHz,note),attackPhase)
                   *settings.attackGain*(settings.invertPunch?-1.0:1.0);
        for (int ch=0; ch<2; ++ch) {
            double punchSample = 0, tailSample = 0, tailSendSample = 0, reverbSample = 0;
            if (punch && time < punchEnd) punchSample += punch->at(ch,time*punch->rate,punch->rate/outputRate)*pg*settings.punchGain*(settings.invertPunch?-1.0:1.0);
            const double glide=glideRate();
            const bool fading=glideFadeActive() && previousTail;
            const double fadeX=fading?std::clamp(glideFadeElapsed/glideFadeSeconds,0.0,1.0):1.0;
            double newGain=fading?std::sin(fadeX*pi/2):1.0, oldGain=fading?std::cos(fadeX*pi/2):0.0;
            if(fading) {
                // v0.5.10: the two tails were phase-aligned, so an equal-power fade
                // would bulge by up to +3 dB in the middle (and dip when the
                // alignment is poorer). Same correlation-aware law as the join.
                const double norm=std::sqrt(1.0+2.0*glideCorrelation*newGain*oldGain);
                newGain/=norm;oldGain/=norm;
            }
            if(tail && compositeTail && time>=tailStart && time<compositeTailEnd) {
                const double base=time-tailStart-settings.phaseShiftMs/1000.0;
                double value=tail->at(ch,(base+tailOffset)*tail->rate,glide*tail->rate/outputRate)*newGain;
                if(crunchLayer && settings.crunchMix>0)value+=settings.crunchMix*crunchLayer->at(ch,(base+tailOffset)*crunchLayer->rate,glide*crunchLayer->rate/outputRate)*newGain;
                if(fading) {
                    value+=previousTail->at(ch,(base+previousTailOffset)*previousTail->rate,previousRate*previousTail->rate/outputRate)*oldGain;
                    // v0.5.10: the old note's crunch layer fades out with it instead of vanishing.
                    if(previousCrunchLayer && settings.crunchMix>0)value+=settings.crunchMix*previousCrunchLayer->at(ch,(base+previousTailOffset)*previousCrunchLayer->rate,previousRate*previousCrunchLayer->rate/outputRate)*oldGain;
                }
                tailSample += value*tg*settings.tailGain*(settings.invertTail?-1.0:1.0);
                tailSendSample = tailSample;   // the composite already contains the cut dry Tail
            } else if (tail && time >= tailStart && time < tailEnd) {
                const double base=tailTrim+(time-tailStart)*ratio-settings.phaseShiftMs/1000.0;
                double value=tail->at(ch,(base+tailOffset)*tail->rate,glide*ratio*tail->rate/outputRate)*newGain;
                if(crunchLayer && settings.crunchMix>0)value+=settings.crunchMix*crunchLayer->at(ch,(base+tailOffset)*crunchLayer->rate,glide*ratio*crunchLayer->rate/outputRate)*newGain;
                double previous=0;
                if(fading) {
                    previous=previousTail->at(ch,(base+previousTailOffset)*previousTail->rate,previousRate*ratio*previousTail->rate/outputRate)*oldGain;
                    if(previousCrunchLayer && settings.crunchMix>0)previous+=settings.crunchMix*previousCrunchLayer->at(ch,(base+previousTailOffset)*previousCrunchLayer->rate,previousRate*ratio*previousCrunchLayer->rate/outputRate)*oldGain;
                }
                // Same arithmetic order as v0.4.56, so the uncut Tail is bit-identical.
                tailSendSample += (value+previous)*tg*settings.tailGain*(settings.invertTail?-1.0:1.0);
                if(settings.tailEndTrimMs>0) {
                    value*=tailCutGain(tail,base+tailOffset);
                    if(fading)previous*=tailCutGain(previousTail,base+previousTailOffset);
                }
                tailSample += (value+previous)*tg*settings.tailGain*(settings.invertTail?-1.0:1.0);
            }
            if (reverb && time >= reverbStart && time < reverbEnd) {
                // Prevent source transients/clicks at the boundary without
                // starting reverb early or changing its natural pitch/duration.
                const double fadeIn = std::clamp((time-reverbStart)/0.0003,0.0,1.0);
                const double fadeOut = std::clamp((reverbEnd-time)/0.002,0.0,1.0);
                const float raw=reverb->at(ch,(reverbTrim + time-reverbStart)*reverb->rate,
                                           reverb->rate/outputRate);
                const float dryOrFiltered=reverbFilter.process(ch,raw);
                float filtered=dryOrFiltered;
                if (reverbFilterTransition>0) {
                    const float next=incomingReverbFilter.process(ch,raw);
                    const double blend=1.0-double(reverbFilterTransition)/reverbFilterTransitionLength;
                    filtered=float(dryOrFiltered*(1.0-blend)+next*blend);
                }
                reverbSample += filtered*fadeIn*fadeOut*settings.reverbGain;
            }
            double generatedWet=0;
            if(generatedTailReverb && time>=generatedTailReverbStart && time<generatedTailReverbEnd) {
                const double localTime=time-generatedTailReverbStart;
                const double fadeIn=std::clamp(localTime/.0003,0.0,1.0);
                const double fadeOut=std::clamp((generatedTailReverbEnd-time)/.002,0.0,1.0);
                generatedWet=generatedTailReverb->at(ch,(localTime-settings.phaseShiftMs*.001)*generatedTailReverb->rate,
                                                       generatedTailReverb->rate/outputRate)*fadeIn*fadeOut;
            }
            out.punch[size_t(ch)] = float((settings.attackOn?punchSample+attack:punchSample)*env);
            out.tail[size_t(ch)] = float(tailSample*env);
            out.tailSend[size_t(ch)] = float(tailSendSample*env);
            out.reverb[size_t(ch)] = float(reverbSample*env);
            out.generatedTailReverb[size_t(ch)] = float(generatedWet*env*settings.tailGain*(settings.invertTail?-1.0:1.0));
            out.tailReverb[size_t(ch)] = out.tail[size_t(ch)] + out.reverb[size_t(ch)];
        }
        if(reverbFilterTransition>0 && --reverbFilterTransition==0)
            reverbFilter=incomingReverbFilter;
        if(gliding) {
            // Reading faster/slower during the slide moves the tail position.
            // v0.5.10: the old tail keeps moving at its own rate during the
            // crossfade (it differs from 1 when a key arrived mid-slide).
            const double tailStep=compositeTail?1.0:ratio;
            if(time>=tailStart){tailOffset+=(glideRate()-1.0)*tailStep/outputRate;previousTailOffset+=(previousRate-1.0)*tailStep/outputRate;}
            glideElapsed+=1/outputRate; glideFadeElapsed+=1/outputRate;
            // v0.5.23: a stretched slide ends only once it reads the target note.
            if(glideElapsed>=glideSeconds && glideFadeElapsed>=glideFadeSeconds && (!stretchedGlide || note==glideTarget))gliding=false;
        }
        time += 1/outputRate; if (release >= 0) release += 1/outputRate;
        return out;
    }
    // Compatibility entry point for the complete-kick waveform and existing
    // regression tests. The separate render buses use the same timing.
    std::array<float,2> next(const Sample* punch, const Sample* tail, double outputRate, const Sample* reverb = nullptr) {
        const auto parts = nextParts(punch, tail, outputRate, reverb);
        return {parts.punch[0]+parts.tailReverb[0],parts.punch[1]+parts.tailReverb[1]};
    }
};
}

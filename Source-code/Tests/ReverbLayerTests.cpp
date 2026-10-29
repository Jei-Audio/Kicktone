#include "../Source/Engine.h"
#include "../Source/KickOverview.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

static void check(bool ok, const char* message) { if(!ok) throw std::runtime_error(message); }
static pt::Sample constant(double seconds, float amplitude) {
    pt::Sample s;
    s.rate=48000;
    s.left.assign(size_t(std::llround(seconds*s.rate)),amplitude);
    s.right=s.left;
    return s;
}
int main() try {
    const auto punch=constant(0.10f,0.0f);
    const auto tail=constant(0.30f,0.0f);
    const auto reverb=constant(0.50f,0.5f);
    pt::Settings s;s.output=1;s.crossfadeMs=10;s.joinMs=-8;
    s.reverbTrimMs=100;
    pt::Voice v;
    v.start(36,1,1.0f,&punch,&tail,s,&reverb);
    check(std::abs(v.punchEnd-.10)<1e-8,"Reverb unexpectedly modified punch duration");
    check(v.tailStart<.10,"Tail join should still be independent");
    check(std::abs(v.reverbStart-.10)<1e-8,"Reverb must begin exactly after trimmed punch");
    check(std::abs(v.reverbTrim-.10)<1e-8,"Reverb source-start offset incorrect");
    check(std::abs(v.reverbEnd-.50)<1e-8,"Reverb end must include selected start trim");
    float maxBefore=0, maxAfter=0, maxBeyond=0;
    for(int i=0;i<30000;++i) {
        const auto x=v.next(&punch,&tail,48000,&reverb);
        if(i<4800)maxBefore=std::max(maxBefore,std::abs(x[0]));
        else if(i<24000)maxAfter=std::max(maxAfter,std::abs(x[0]));
        else maxBeyond=std::max(maxBeyond,std::abs(x[0]));
    }
    check(maxBefore==0,"Reverb played before punch ended");
    check(maxAfter>.45f,"Reverb not heard after punch ended");
    check(maxBeyond==0,"Reverb continued after natural length");
    // Punch shortening moves the reverb join, but never pitches reverb.
    s.punchEndOffsetMs=60;
    pt::Voice shorter; shorter.start(38,1,1.0f,&punch,&tail,s,&reverb);
    check(std::abs(shorter.reverbStart-.04)<1e-8,"Reverb not following trimmed punch end");
    check(std::abs(shorter.reverbEnd-.44)<1e-8,"Unpitched reverb duration changed with MIDI note");
    // Fully trimmed reverb must be silent with the same legacy punch/tail audio.
    s.punchEndOffsetMs=0;s.reverbTrimMs=500;
    pt::Voice trimmed;trimmed.start(36,1,1.0f,&punch,&tail,s,&reverb);
    check(std::abs(trimmed.reverbEnd-trimmed.reverbStart)<1e-8,"Full reverb trim failed");
    const auto preview=pt::buildKickOverview(&punch,&tail,s,36,256,&reverb);
    check(std::abs(preview.totalMs-trimmed.tailEnd*1000.0)<1,"Fully trimmed reverb changed tail preview duration");
    s.reverbTrimMs=100;
    const auto full=pt::buildKickOverview(&punch,&tail,s,36,256,&reverb);
    check(std::abs(full.totalMs-500)<1,"Waveform preview excluded reverb layer");
    // v0.4.6: delay moves the playback start, never the reverb file offset,
    // punch duration, tail join, reverb duration or reverb pitch. A default
    // delay of 0 ms retains the v0.4.5 onset exactly.
    s.reverbDelayMs=40;
    pt::Voice delayed;delayed.start(36,1,1.0f,&punch,&tail,s,&reverb);
    check(std::abs(delayed.punchEnd-.10)<1e-8,"Delay modified punch duration");
    check(std::abs(delayed.tailStart-v.tailStart)<1e-8,"Delay changed tail join");
    check(std::abs(delayed.reverbTrim-.10)<1e-8,"Delay trimmed the reverb recording");
    check(std::abs(delayed.reverbStart-.14)<1e-8,"40 ms reverb playback delay incorrect");
    check(std::abs(delayed.reverbEnd-.54)<1e-8,"Delay changed recorded reverb duration");
    for(int i=0;i<30000;++i){
        const auto out=delayed.next(&punch,&tail,48000,&reverb);
        const auto amplitude=std::abs(out[0]);
        if(i<6720) check(amplitude==0,"Reverb sounded before delayed onset");
        if(i==6721) check(amplitude>0,"Reverb did not begin at delayed onset");
        if(i>=25920) check(amplitude==0,"Reverb played past delayed natural end");
    }
    // Even maximum delay cannot transpose or stretch the reverb, and follows
    // punch-end trimming for different notes.
    s.reverbDelayMs=300;s.punchEndOffsetMs=60;
    pt::Voice late;late.start(38,1,1.0f,&punch,&tail,s,&reverb);
    check(std::abs(late.reverbStart-.34)<1e-8,"300 ms delayed start did not follow trimmed punch");
    check(std::abs(late.reverbEnd-.74)<1e-8,"Reverb source length was changed by MIDI or delay");
    const auto lateOverview=pt::buildKickOverview(&punch,&tail,s,38,256,&reverb);
    check(std::abs(lateOverview.totalMs-740)<1.0,"Combined waveform excluded reverb delay");
    s.reverbTrimMs=500;
    pt::Voice silent;silent.start(38,1,1.0f,&punch,&tail,s,&reverb);
    check(std::abs(silent.reverbEnd-silent.reverbStart)<1e-8,"Fully trimmed reverb became audible with delay");
    const auto silentOverview=pt::buildKickOverview(&punch,&tail,s,38,256,&reverb);
    const auto noReverbOverview=pt::buildKickOverview(&punch,&tail,s,38,256,nullptr);
    check(std::abs(silentOverview.totalMs-noReverbOverview.totalMs)<1,
          "Silent reverb delay extended the kick preview");
    s.reverbTrimMs=100;s.punchEndOffsetMs=0;s.reverbDelayMs=-30;
    pt::Voice negative;negative.start(36,1,1.0f,&punch,&tail,s,&reverb);
    check(std::abs(negative.reverbStart-.10)<1e-8,"Negative delay started reverb early");
    s.reverbDelayMs=450;
    pt::Voice clamped;clamped.start(36,1,1.0f,&punch,&tail,s,&reverb);
    check(std::abs(clamped.reverbStart-.40)<1e-8,"Delay exceeded the advertised maximum");
    // v0.4.7: Reverb Level controls ONLY the recorded reverb amplitude.
    // Unity (0 dB) is bit-for-bit identical to v0.4.6. No timing/pitch changes.
    s.reverbDelayMs=40;s.reverbTrimMs=100;s.reverbGain=1;s.punchEndOffsetMs=0;
    auto atReverbBody=[&](pt::Settings level) {
        pt::Voice voice;voice.start(36,1,1,&punch,&tail,level,&reverb);
        double peak=0;
        for(int i=0;i<13000;++i){
            const auto sample=voice.next(&punch,&tail,48000,&reverb);
            if(i>=8500 && i<10000)peak=(std::max)(peak,double(std::abs(sample[0])));
        }
        return peak;
    };
    const double atUnity=atReverbBody(s);
    check(atUnity>.49 && atUnity<.501,"0 dB did not preserve original reverb level");
    s.reverbGain=0.25;
    check(std::abs(atReverbBody(s)/atUnity-0.25)<1e-6,"Reverb 25% gain did not scale amplitude only");
    s.reverbGain=2.0;
    check(std::abs(atReverbBody(s)/atUnity-2)<1e-6,"Positive gain did not scale reverb independently");
    s.reverbGain=0;
    check(atReverbBody(s)==0,"Muted reverb produced nonzero samples");
    // A loaded reverb at mute must NOT affect the punch/tail mix, even with overlap.
    auto pitchedTail=constant(0.50f,0.20f);
    s.reverbGain=0;
    pt::Voice muted, baseline;muted.start(36,1,1,&punch,&pitchedTail,s,&reverb);
    baseline.start(36,1,1,&punch,&pitchedTail,s,nullptr);
    for(int i=0;i<19000;++i){
        const auto a=muted.next(&punch,&pitchedTail,48000,&reverb);
        const auto b=baseline.next(&punch,&pitchedTail,48000,nullptr);
        check(std::abs(a[0]-b[0])<1e-7,"Muted reverb altered dry punch/tail samples");
    }
    s.reverbGain=0.5;
    pt::Voice vol;vol.start(36,1,1,&punch,&tail,s,&reverb);
    check(std::abs(vol.reverbStart-0.14)<1e-8 && std::abs(vol.reverbEnd-0.54)<1e-8,
          "Reverb gain changed onset or duration");
    const auto quieterOverview=pt::buildKickOverview(&punch,&tail,s,36,1024,&reverb);
    s.reverbGain=1;
    const auto unityOverview=pt::buildKickOverview(&punch,&tail,s,36,1024,&reverb);
    check(std::abs(quieterOverview.totalMs-unityOverview.totalMs)<1,
          "Reverb gain changed the complete waveform duration");
    std::cout<<"PASS punch boundary and delay 0/40/300 ms, independent reverb offset, gain 0/0.25/1/2, silent dry bypass, preview and tail duration\n";
    return 0;
} catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }

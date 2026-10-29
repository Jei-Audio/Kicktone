#pragma once
#include "PitchDropSettings.h"
#include "KickCharacterPitch.h"

namespace pt::drop {
// Worker-only variable-rate full-band pitch drop. At 4x resolution, repeat
// whole local cycles with a stereo-linked, correlation-refined handover to
// retain the prepared sample's duration. No body/noise split. Off is exact.
inline Sample render(const Sample& input, PitchDropSettings settings, double fundamentalHz,
                     double onsetSeconds=0, const character::Cancel& cancel={}) {
    if(!settings.active() || input.left.empty())return input;
    if(input.left.size()!=input.right.size() || !std::isfinite(input.rate) || input.rate<8000)
        throw std::invalid_argument("Invalid pitch drop source");
    character::checkpoint(cancel);
    const size_t first=(std::min)(input.left.size(),size_t(std::llround((std::max)(0.,onsetSeconds)*input.rate)));
    if(first>=input.left.size())return input;
    Sample source;source.rate=input.rate;
    source.left.assign(input.left.begin()+first,input.left.end());source.right.assign(input.right.begin()+first,input.right.end());
    // Very short punches need no cycle correction: there is no full cycle to
    // repeat safely. Boundary-safe reads and the final fade keep them finite.
    auto analysis=cycle::analyzeWithManualRoot(source,std::clamp(std::isfinite(fundamentalHz)?fundamentalHz:65.406,18.,1400.));
    auto prepared=character::prepare(source,analysis,1.,cancel);
    Sample high;high.rate=input.rate*4.;
    high.left=cycle::resample(source.left,input.rate,.25);character::checkpoint(cancel);
    high.right=cycle::resample(source.right,input.rate,.25);character::checkpoint(cancel);
    const character::detail::Stream stream{&high.left,&high.right};
    const size_t count=source.left.size()*4;
    std::vector<float> left(count+8,0),right(count+8,0);
    double q=0,partner=0;size_t fadeAt=0,fadeLength=0;
    double rho=1;
    for(size_t o=0;o<count+8;++o) {
        if((o&2047u)==0)character::checkpoint(cancel);
        const double t=double(o)/high.rate;
        const double rate=std::pow(2.,settings.semitones(t)/12.);
        const long period=(std::max)(16L,long(std::llround(character::detail::localPeriod(prepared,q*.25)*4.)));
        if(!fadeLength && q-double(o)>double(period)*.6 && q>double(period)*1.2) {
            const long lag=character::detail::refineLag(stream,long(q),period,-1);
            if(q-lag>=0) {
                partner=q-lag;fadeAt=0;
                fadeLength=size_t((std::max)(24L,long(double(period)/(5.*rate))));
                rho=std::clamp(character::detail::similarity(stream,long(q),long(partner),long(fadeLength)),0.,1.);
            }
        }
        // Source-end protection uses the same cycle handover, never a hard wrap.
        if(!fadeLength && q+period*.35>=double(high.left.size()) && q>period*1.2 && o<count) {
            partner=q-period;fadeAt=0;fadeLength=size_t((std::max)(24L,long(double(period)/(5.*rate))));
            rho=std::clamp(character::detail::similarity(stream,long(q),long(partner),long(fadeLength)),0.,1.);
        }
        double b=0,g=1;
        if(fadeLength) {
            b=.5-.5*std::cos(pi*(double(fadeAt)+.5)/double(fadeLength));
            const double a=1.-b;g=1./std::sqrt((std::max)(.5,a*a+b*b+2*a*b*rho));
        }
        // 4x source + final band-limited decimation protects upward drops.
        // The source was already bandlimited to its original Nyquist; step=1
        // avoids an unnecessary second per-sample low-pass at this resolution.
        left[o]=float(g*((1.-b)*high.at(0,q)+(b?b*high.at(0,partner):0.)));
        right[o]=float(g*((1.-b)*high.at(1,q)+(b?b*high.at(1,partner):0.)));
        q+=rate;
        if(fadeLength) {partner+=rate;if(++fadeAt>=fadeLength){q=partner;fadeLength=0;}}
    }
    Sample out=input;
    auto l=cycle::resample(left,high.rate,4.);character::checkpoint(cancel);
    auto r=cycle::resample(right,high.rate,4.);character::checkpoint(cancel);
    const size_t n=source.left.size(),fade=(std::min)(n,size_t(std::ceil(.002*input.rate)));
    for(size_t i=0;i<n;++i) {
        // Fade entirely inside the original duration. The drop never changes
        // trim, join, MIDI, ADSR or recorded-reverb event times.
        const double gain=i>=n-fade?.5-.5*std::cos(pi*double(n-1-i)/double((std::max)(size_t(1),fade))):1.;
        out.left[first+i]=float(l[i]*gain);out.right[first+i]=float(r[i]*gain);
    }
    return out;
}
}

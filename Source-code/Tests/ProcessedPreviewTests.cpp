#include "../Source/ProcessedPreview.h"
#include <iostream>
#include <stdexcept>
#include <string>
static void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
static PreviewResult render(ProcessedPreview& worker,PreviewRequest request) {
    const auto generation=worker.submit(std::move(request));
    for(int tries=0;tries<15000;++tries) {
        if(auto result=worker.take())if(result->request.generation==generation)return std::move(*result);
        juce::Thread::sleep(1);
    }
    throw std::runtime_error("Preview worker timed out");
}
static double difference(const pt::KickOverview& a,const pt::KickOverview& b) {
    check(a.envelope.bins.size()==b.envelope.bins.size(),"Preview duration/bins changed");
    double d=0;for(size_t i=0;i<a.envelope.bins.size();++i)d+=std::abs(a.envelope.bins[i].maximum-b.envelope.bins[i].maximum)+std::abs(a.envelope.bins[i].minimum-b.envelope.bins[i].minimum);
    return d;
}
int main()try {
    ProcessedPreview worker;
    PreviewRequest request;request.rate=44100;request.settings.output=1;
    // Isolate the original exact-bypass check from the new default 1 ms fade.
    request.envelope.releaseMs=0;
    auto asset=std::make_shared<pt::Sample>();(*asset).rate=44100;
    (*asset).left.resize(8820);(*asset).right.resize(8820);
    for(size_t i=0;i<(*asset).left.size();++i)(*asset).left[i]=(*asset).right[i]=float(.15*std::sin(2*pt::pi*1000*i/44100));
    request.samples[0]=asset;
    auto dry=render(worker,request);
    request.layerMuted[0]=true;auto noPunch=render(worker,request);
    check(noPunch.processedPeaks[0]==0 && noPunch.processedPeaks[1]==0,"Punch layer mute was not reflected in the processed waveform preview");
    request.layerMuted[0]=false;
    const auto legacy=pt::buildKickOverview(&(*asset),nullptr,request.settings,request.root,7200);
    check(difference(dry.kick,legacy)<1e-5,"Bypassed preview differs from original voice");
    request.settings.invertPunch=true;auto inverted=render(worker,request);
    check(inverted.kick.envelope.bins.size()==dry.kick.envelope.bins.size(),"Punch inversion changed preview length");
    for(size_t i=0;i<dry.kick.envelope.bins.size();++i){const auto a=dry.kick.envelope.bins[i],b=inverted.kick.envelope.bins[i];check(std::abs(a.minimum+b.maximum)<1.e-7f && std::abs(a.maximum+b.minimum)<1.e-7f,"Punch polarity missing from processed waveform");}
    request.settings.invertPunch=false;
    request.transient.enabled=true;request.transient.attack=0;request.transient.sustain=0;request.transient.outputDb=-6;
    auto shaped=render(worker,request);
    check(std::abs(shaped.kick.envelope.peak/dry.kick.envelope.peak-std::pow(10.0,-6.0/20.0))<.001,"Transient output trim missing from waveform");
    check(shaped.kick.totalMs==dry.kick.totalMs,"Transient changed preview duration");
    request.samples[0].reset();request.samples[2]=asset;
    auto wet=render(worker,request);request.transient.enabled=false;
    auto wetDry=render(worker,request);check(difference(wet.kick,wetDry.kick)<1e-6,"Punch shaper leaked into reverb preview");
    request.layerMuted[2]=true;auto noReverb=render(worker,request);
    check(noReverb.processedPeaks[1]==0,"Recorded reverb layer mute was not reflected in preview");request.layerMuted[2]=false;
    request.samples[2].reset();request.samples[0]=asset;
    request.eqOn[1]=true;
    request.bands[1][0].enabled=true;request.bands[1][0].type=tt::Bell;request.bands[1][0].hz=1000;request.bands[1][0].gain=6;
    auto eq=render(worker,request);check(eq.kick.envelope.peak>dry.kick.envelope.peak*1.8,"Punch EQ absent from final preview");
    check(eq.processedPeaks[0]>dry.processedPeaks[0]*1.8f && eq.processedPeaks[1]==0,"Processed peak analysis ignored EQ or leaked to tail");
    request.normalizeGains[0]=std::pow(10.0f,-1.0f/20.0f)/eq.processedPeaks[0];
    auto normalized=render(worker,request);
    check(std::abs(juce::Decibels::gainToDecibels(normalized.kick.envelope.peak)+1)<.001,"Processed normalization does not reach -1 dBFS");
    check(normalized.processedPeaks==eq.processedPeaks,"Normalization analysis must be idempotent");
    request.normalizeGains={1,1};
    request.eqOn[1]=false;request.distortion[0].enabled=true;request.distortion[0].driveDb=24;
    auto distorted=render(worker,request);check(difference(distorted.kick,dry.kick)>1,"Distortion absent from final preview");
    request.distortion[0].enabled=false;request.samples[0].reset();request.samples[2]=asset;
    auto rv=render(worker,request);request.distortion[0].enabled=true;
    auto rvPunchDist=render(worker,request);
    // The bypassed companion path still has oversampling delay, so compare peaks.
    check(std::abs(rvPunchDist.kick.envelope.peak-rv.kick.envelope.peak)<.01,"Punch distortion altered reverb level");
    for(int i=0;i<30;++i){request.settings.output=float(i)/30;worker.submit(request);}
    request.settings.output=.25;auto final=render(worker,request);
    check(final.request.settings.output==.25,"Stale preview published over latest request");
    request.samples[2].reset();request.samples[1]=asset;request.settings.output=1;
    request.generated.keepDry=false;request.generated.enabled=true;request.generated.mix=100;request.generated.spaceMs=500;request.generated.distance=0;
    auto generated=render(worker,request);check(generated.kick.totalMs>900 && generated.processedPeaks[1]>0,"Generated reverb missing from final waveform/normalization analysis");
    request.generated.levelDb=-6;auto quieter=render(worker,request);
    check(std::abs(quieter.processedPeaks[1]/generated.processedPeaks[1]-std::pow(10.f,-6.f/20.f))<.001,"Generated reverb volume missing from preview");
    request.generated.levelDb=-60;auto muted=render(worker,request);check(muted.processedPeaks[1]==0,"Generated reverb volume mute failed");
    request.generated.levelDb=0;
    request.distortion[1].enabled=true;request.distortion[1].driveDb=24;request.distortion[1].mix=1;
    request.generated.distortWet=false;auto cleanGenerated=render(worker,request);
    request.generated.distortWet=true;auto distortedGenerated=render(worker,request);
    check(difference(cleanGenerated.kick,distortedGenerated.kick)>1,"DISTORT WET did not route generated return through tail distortion");
    request.generated.distortWet=false;request.distortion[1].enabled=false;
    request.generatedEQ=true;request.generatedBands[0].enabled=true;request.generatedBands[0].type=tt::Bell;request.generatedBands[0].hz=1000;request.generatedBands[0].gain=-12;
    auto filtered=render(worker,request);check(filtered.processedPeaks[1]<generated.processedPeaks[1]*.5f,"Generated wet EQ did not filter the return");
    request.generated.enabled=false;request.generatedEQ=false;request.generated.keepDry=true;request.generated.levelDb=-60;
    request.distortion[1].enabled=true;request.distortion[1].driveDb=14;request.distortion[1].outputDb=-12;
    auto additiveDry=render(worker,request);request.generated.enabled=true;auto additiveMuted=render(worker,request);
    check(additiveDry.processedPeaks==additiveMuted.processedPeaks,"Keep Dry changed distorted peak/normalization analysis in preview");
    request.generated.enabled=true;request.generated.mix=100;request.generated.spaceMs=500;request.generated.levelDb=0;request.generated.punchSend=true;
    request.samples={};request.samples[0]=asset;auto punchVerb=render(worker,request);
    check(punchVerb.kick.totalMs>200 && punchVerb.processedPeaks[1]>0,"Optional punch reverb send did not produce its own wet return");
    // v0.5.28 instant waveform volume: remixPreview of a render's buses at other
    // normalize gains equals a new render at those gains (limiter and IIR mono bass
    // exactly; linear mono bass up to rounding: its low band is summed per bus).
    {check(punchVerb.buses && punchVerb.audio && punchVerb.buses->frames.size()==punchVerb.audio->left.size() &&
           punchVerb.buses->bins==punchVerb.kick.envelope.bins.size(),"Render did not keep its output buses");
     auto same=[](const pt::WaveformEnvelope& a,const pt::WaveformEnvelope& b,float tolerance){
         if(a.bins.size()!=b.bins.size()||std::abs(a.peak-b.peak)>tolerance)return false;
         for(size_t i=0;i<a.bins.size();++i)if(std::abs(a.bins[i].minimum-b.bins[i].minimum)>tolerance||std::abs(a.bins[i].maximum-b.bins[i].maximum)>tolerance)return false;
         return true;};
     check(same(remixPreview(*punchVerb.buses,punchVerb.request.normalizeGains),punchVerb.kick.envelope,0),"Remix at the rendered gains differs from the render");
     for(int mode=0;mode<3;++mode) {   // 0 limiter only, 1 IIR mono bass, 2 linear mono bass
         auto base=request;base.limiter=true;base.bassMono=mode>0;base.bassMonoLinear=mode==2;base.bassMonoHz=150;base.bassMonoMix=.8f;
         const auto from=render(worker,base);
         check(from.buses && from.buses->monoLow.size()==(mode==2?from.buses->frames.size():0),"Linear mono bass low bands missing from the buses");
         for(const auto& gains:{std::array<float,2>{{2.5f,1}},std::array<float,2>{{1,.3f}},std::array<float,2>{{4,3}},std::array<float,2>{{.01f,1}}}) {
             auto to=base;to.normalizeGains=gains;const auto real=render(worker,to);
             check(same(remixPreview(*from.buses,gains),real.kick.envelope,mode==2?1.e-5f:0.f),"Remixed waveform differs from a render at the new volume");
         }
         check(!same(remixPreview(*from.buses,{{4,3}}),from.kick.envelope,1.e-3f),"Remix ignored the new volume");
         const auto t0=juce::Time::getMillisecondCounterHiRes();for(int i=0;i<10;++i)remixPreview(*from.buses,{{1.f+.1f*float(i),1}});
         std::cout<<"remix ("<<(mode==0?"limiter":mode==1?"IIR mono bass":"linear mono bass")<<", "<<from.buses->frames.size()<<" frames): "
                  <<(juce::Time::getMillisecondCounterHiRes()-t0)/10<<" ms\n";
     }
    }
    // v0.5.32 Punch-only render: a request whose Punch chain changed (Punch Attack/
    // Decay gains, EQs, split, trims, Punch EQ and its position, transient, Punch
    // distortion), rendered from an earlier render's buses, equals a full render of
    // that request sample for sample - waveform, export audio, buses and peaks.
    {auto tailAsset=std::make_shared<pt::Sample>();tailAsset->rate=44100;tailAsset->left.resize(26460);tailAsset->right.resize(26460);
     for(size_t i=0;i<tailAsset->left.size();++i){const double t=double(i)/44100;tailAsset->left[i]=float(.5*std::exp(-t*3)*std::sin(2*pt::pi*55*t));tailAsset->right[i]=float(.45*std::exp(-t*3)*std::sin(2*pt::pi*55*t+.3));}
     auto punchAsset=std::make_shared<pt::Sample>();punchAsset->rate=44100;punchAsset->left.resize(6615);punchAsset->right.resize(6615);
     for(size_t i=0;i<punchAsset->left.size();++i){const double t=double(i)/44100,f=60+900*std::exp(-t*40);punchAsset->left[i]=float(.7*std::exp(-t*12)*std::sin(2*pt::pi*f*t));punchAsset->right[i]=float(.6*std::exp(-t*12)*std::sin(2*pt::pi*f*t+.2));}
     // Empty = identical, else what differs first.
     auto differs=[](const PreviewResult& a,const PreviewResult& b)->std::string{
         if(!a.audio||!b.audio||a.audio->left.size()!=b.audio->left.size())return "audio length";
         for(size_t i=0;i<a.audio->left.size();++i)if(a.audio->left[i]!=b.audio->left[i]||a.audio->right[i]!=b.audio->right[i])
             return "audio at frame "+std::to_string(i)+" ("+std::to_string(a.audio->left[i])+" vs "+std::to_string(b.audio->left[i])+")";
         if(a.processedPeaks!=b.processedPeaks)return "peaks";
         if(a.kick.totalMs!=b.kick.totalMs||a.kick.punchEndMs!=b.kick.punchEndMs||a.kick.tailStartMs!=b.kick.tailStartMs)return "timing";
         if(a.kick.envelope.peak!=b.kick.envelope.peak||a.kick.envelope.bins.size()!=b.kick.envelope.bins.size())return "waveform peak";
         for(size_t i=0;i<a.kick.envelope.bins.size();++i)if(a.kick.envelope.bins[i].minimum!=b.kick.envelope.bins[i].minimum||a.kick.envelope.bins[i].maximum!=b.kick.envelope.bins[i].maximum)return "waveform column "+std::to_string(i);
         if(!a.buses||!b.buses||a.buses->frames!=b.buses->frames)return "buses";
         if(a.buses->monoLow!=b.buses->monoLow)return "mono low bands";
         return {};};
     auto identical=[&](const PreviewResult& a,const PreviewResult& b){return differs(a,b).empty();};
     const auto variants=[&](PreviewRequest r,int v){
         if(v>=1){r.punchRegion.enabled=true;r.punchRegion.attackGainDb=6;r.punchRegion.decayGainDb=-9;r.punchRegion.splitPercent=30;r.punchRegion.transitionMs=3;}
         if(v>=2){r.attackEQ=true;r.attackBands[0].enabled=true;r.attackBands[0].type=tt::Bell;r.attackBands[0].hz=3000;r.attackBands[0].gain=8;
                  r.decayEQ=true;r.decayBands[0].enabled=true;r.decayBands[0].type=tt::LowShelf;r.decayBands[0].hz=120;r.decayBands[0].gain=-6;
                  r.splitNormalizeGains={{1.6f,.4f}};r.eqOn[1]=true;r.bands[1][1].enabled=true;r.bands[1][1].type=tt::Bell;r.bands[1][1].hz=400;r.bands[1][1].gain=-5;}
         if(v>=3){r.transient.enabled=true;r.transient.attack=40;r.transient.outputDb=-2;r.distortion[0].driveDb=18;r.distortion[0].type=2;r.distortion[0].outputDb=-1;r.distortion[0].toneHz=9000;}
         if(v>=4)r.eqPost[0]=true;
         if(v==5){r.punchRegion.attackGainDb=-12;r.punchRegion.decayGainDb=12;r.punchRegion.splitPercent=55;}
         return r;};
     // Punch distortion ON/OFF is NOT Punch-only: either distortion being on runs both
     // lanes through the 4x oversampling (matched delay), so it changes the Tail bus too.
     // The editor sends it as a full render; here it is part of each mode's base.
     for(int mode=0;mode<4;++mode) {   // 0 limiter, 1 IIR mono bass (no distortion), 2 linear mono bass, 3 Tail distortion + distorted wet, no limiter
         auto base=request;base.samples={};base.samples[0]=punchAsset;base.samples[1]=tailAsset;base.distortion={};
         base.limiter=mode!=3;base.bassMono=mode==1||mode==2;base.bassMonoLinear=mode==2;base.bassMonoHz=140;base.bassMonoMix=.9f;
         base.distortion[0].enabled=mode!=1;
         if(mode==3){base.distortion[1].enabled=true;base.distortion[1].driveDb=20;base.generated.distortWet=true;}
         const auto from=render(worker,base);
         check(from.buses&&from.buses->punchSource&&from.buses->punchSource->punch.size()==from.buses->frames.size(),"Full render did not keep the Punch source");
         auto previous=from;double fullMs=0,punchMs=0;
         for(int v=0;v<=5;++v) {
             const auto to=variants(base,v);
             auto t0=juce::Time::getMillisecondCounterHiRes();const auto full=render(worker,to);fullMs+=juce::Time::getMillisecondCounterHiRes()-t0;
             auto fast=to;fast.punchBase=previous.buses;
             t0=juce::Time::getMillisecondCounterHiRes();const auto quick=render(worker,fast);punchMs+=juce::Time::getMillisecondCounterHiRes()-t0;
             if(const auto why=differs(full,quick);!why.empty())
                 throw std::runtime_error("Punch-only render differs from a full render (mode "+std::to_string(mode)+", variant "+std::to_string(v)+"): "+why);
             check(!quick.request.punchBase && quick.buses->punchSource==from.buses->punchSource,"Punch-only render must reuse the Punch source and drop its base");
             previous=quick;   // chained: the next variant starts from this Punch-only render
         }
         check(!identical(from,previous),"Punch chain changes had no effect");
         std::cout<<"punch-only render (mode "<<mode<<", "<<from.buses->frames.size()<<" frames): "<<punchMs/6<<" ms, full "<<fullMs/6<<" ms\n";
     }
    }
    request.samples={};auto empty=render(worker,request);check(empty.kick.envelope.bins.empty(),"Unloaded waveform not cleared");
    std::cout<<"PASS background preview: bypass, transient, EQ, distortion, routing, duration, coalescing, unload, volume remix and Punch-only render\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}

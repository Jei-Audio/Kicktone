#include "../Source/PhaseAlignment.h"
#include <iostream>
#include <stdexcept>
#include <algorithm>
static void ck(bool b,const char* m){if(!b)throw std::runtime_error(m);}
static pt::Sample sine(double hz,double phase,double secs=.120,int rate=44100) {
    pt::Sample s;s.rate=rate;
    const int n=int(secs*rate);s.left.resize(size_t(n));s.right.resize(size_t(n));
    for(int i=0;i<n;++i) {
        // A soft envelope avoids artificial clicks in the synthetic fixture.
        const double env=std::min(1.,double(i)/100)*std::min(1.,double(n-i)/100);
        const float x=float(env*0.6*std::sin(2*pt::pi*hz*i/rate+phase));
        s.left[size_t(i)]=x;s.right[size_t(i)]=x;
    }
    return s;
}
int main()try {
    const auto punch=sine(160,0,.100);
    const auto tail=sine(160,pt::pi*0.48,.100);
    pt::Settings s;s.root=36;s.output=1;s.crossfadeMs=8;s.joinMs=0;
    const auto before=pt::phase::analyse(&punch,&tail,s,36,false);
    ck(before.valid && before.offsetMs==0 && std::abs(before.before-before.after)<1e-10,"OFF must measure without shifting audio");
    const auto result=pt::phase::analyse(&punch,&tail,s,36,true);
    ck(result.valid,"Matching audio should provide a valid phase estimate");
    ck(result.after>=result.before+0.059,"Automatic alignment must improve estimated correlation");
    ck(std::abs(result.offsetMs)<=3.01,"Auto phase offset must stay bounded");
    pt::Voice legacy,autoAligned;
    legacy.start(36,1,1,&punch,&tail,s);
    s.phaseShiftMs=result.offsetMs;
    autoAligned.start(36,1,1,&punch,&tail,s);
    ck(std::abs(legacy.tailStart-autoAligned.tailStart)<1e-12 &&
       std::abs(legacy.tailEnd-autoAligned.tailEnd)<1e-12 &&
       std::abs(legacy.punchEnd-autoAligned.punchEnd)<1e-12,
       "Phase adjustment must never move kick envelope, punch or note length");
    bool changed=false;
    for(int i=0;i<4500;++i) {
        const auto dry=legacy.next(&punch,&tail,44100);
        const auto wet=autoAligned.next(&punch,&tail,44100);
        if(std::abs(dry[0]-wet[0])>0.0002)changed=true;
    }
    ck(changed,"Phase shift must be audible during overlapping layers");
    s.crossfadeMs=0;
    const auto noOverlap=pt::phase::analyse(&punch,&tail,s,36,true);
    ck(!noOverlap.valid && noOverlap.offsetMs==0,"Zero crossfade must not invent a phase correction");
    s.crossfadeMs=8;s.phaseShiftMs=0;s.invertTail=true;
    pt::Voice normal,inverted;
    s.invertTail=false;normal.start(36,1,1,&punch,&tail,s);
    s.invertTail=true;inverted.start(36,1,1,&punch,&tail,s);
    for(int i=0;i<3500;++i) {
        const auto a=normal.nextParts(&punch,&tail,44100);
        const auto b=inverted.nextParts(&punch,&tail,44100);
        ck(std::abs(a.punch[0]-b.punch[0])<1e-8f,"Invert must not change punch polarity");
        ck(std::abs(a.tail[0]+b.tail[0])<1e-8f,"Invert must reverse only tail polarity");
    }
    s.invertTail=false;s.invertPunch=false;
    const auto normalCorrelation=pt::phase::analyse(&punch,&tail,s,36,false);
    s.invertPunch=true;
    const auto punchCorrelation=pt::phase::analyse(&punch,&tail,s,36,false);
    ck(std::abs(normalCorrelation.before+punchCorrelation.before)<1e-10,"Punch polarity missing from phase analysis");
    pt::Voice punchNormal,punchInverted;
    const auto reverb=sine(230,0,.2);
    s.invertPunch=false;punchNormal.start(36,1,1,&punch,&tail,s,&reverb);
    s.invertPunch=true;punchInverted.start(36,1,1,&punch,&tail,s,&reverb);
    ck(punchNormal.punchEnd==punchInverted.punchEnd && punchNormal.tailStart==punchInverted.tailStart && punchNormal.tailEnd==punchInverted.tailEnd,"Punch inversion changed timing");
    for(int i=0;i<10000;++i){const auto a=punchNormal.nextParts(&punch,&tail,44100,&reverb),b=punchInverted.nextParts(&punch,&tail,44100,&reverb);
      for(int ch=0;ch<2;++ch){ck(a.punch[size_t(ch)]==-b.punch[size_t(ch)],"Punch inversion did not flip both channels");ck(a.tail[size_t(ch)]==b.tail[size_t(ch)] && a.reverb[size_t(ch)]==b.reverb[size_t(ch)],"Punch inversion changed tail or recorded reverb");}}
    auto silence=tail;
    std::fill(silence.left.begin(),silence.left.end(),0.0f);std::fill(silence.right.begin(),silence.right.end(),0.0f);
    const auto silent=pt::phase::analyse(&punch,&silence,s,36,true);
    ck(!silent.valid,"Silence must not produce a confident auto alignment");
    const auto alt=sine(120,pt::pi*0.48,.100);
    const auto altered=pt::phase::analyse(&punch,&alt,s,39,true);
    ck(altered.valid,"Each prepared note should be independently analysable");
    std::cout<<"PASS per-note phase: estimate, bounded shift, preserved duration, polarity, silence, zero crossfade\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

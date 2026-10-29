#include "../Source/Engine.h"
#include "../Source/ParametricEQ.h"
#include <iostream>
#include <stdexcept>
#include <cmath>
static void check(bool x,const char* message){if(!x)throw std::runtime_error(message);}
int main(){
    constexpr double sr=48000.0;
    pt::Sample p,t,r; p.rate=t.rate=r.rate=sr;
    for(auto* s:{&p,&t,&r}) {s->left.assign(48000,0.0f);s->right.assign(48000,0.0f);}
    for(int i=0;i<48000;++i){
       p.left[i]=p.right[i]=float(0.13*std::sin(2*tt::pi*240*i/sr));
       t.left[i]=t.right[i]=float(0.11*std::sin(2*tt::pi*80*i/sr));
       r.left[i]=r.right[i]=float(0.09*std::sin(2*tt::pi*1600*i/sr));
    }
    pt::Voice v;pt::Settings settings;settings.crossfadeMs=0;settings.punchEndOffsetMs=900;
    v.start(36,1,1,&p,&t,settings,&r);
    tt::ParametricEQ kick,punch,reverb;
    std::array<tt::Band,tt::kBands> kb{},pb{},rb{};
    kb[0]={true,tt::Bell,0,80.0,12.0,1.0};
    pb[0]={true,tt::Bell,0,240.0,-12.0,1.0};
    rb[0]={true,tt::Bell,0,1600.0,-12.0,1.0};
    kick.update(kb,true,sr);punch.update(pb,true,sr);reverb.update(rb,true,sr);
    double dryP=0,dryT=0,dryR=0,wetP=0,wetT=0,wetR=0;
    for(int i=0;i<24000;++i){
        auto parts=v.nextParts(&p,&t,sr,&r);
        check(std::abs(parts.tail[0]+parts.reverb[0]-parts.tailReverb[0])<1e-7f,
             "Legacy tail+reverb output not preserved by 3-part routing");
        float pl=parts.punch[0],pr=parts.punch[1];
        float tl=parts.tail[0],tr=parts.tail[1];
        float rl=parts.reverb[0],rr=parts.reverb[1];
        punch.process(pl,pr);kick.process(tl,tr);reverb.process(rl,rr);
        check(std::isfinite(pl+pr+tl+tr+rl+rr),"Non-finite three-bus processing");
        if(i>1000){
            dryP+=std::abs(parts.punch[0]);dryT+=std::abs(parts.tail[0]);dryR+=std::abs(parts.reverb[0]);
            wetP+=std::abs(pl);wetT+=std::abs(tl);wetR+=std::abs(rl);
        }
    }
    check(dryP>0.01 && dryT>0.01 && dryR>0.01,"Fixture did not reach all buses");
    check(wetP<dryP*0.8,"Punch EQ failed to attenuate punch");
    check(wetT>dryT*1.2,"Kick EQ failed to boost tail");
    check(wetR<dryR*0.8,"Reverb EQ failed to attenuate recorded reverb");
    std::cout<<"Three independent EQ buses and legacy sum tested\n";
}

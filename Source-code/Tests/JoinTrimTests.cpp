#include "../Source/SilenceTrim.h"
#include "../Source/PhaseAlignment.h"
#include <stdexcept>
#include <iostream>
static void ck(bool b,const char* m){if(!b)throw std::runtime_error(m);}
int main(){
 pt::Sample p,t;p.rate=t.rate=48000;p.left.assign(4800,.5f);p.right=p.left;t=p;
 pt::Settings s;s.root=36;s.output=1;s.crossfadeMs=20;s.joinCorrelation=1;
 auto solution=pt::phase::analyse(&p,&t,s,36,false);
 ck(solution.valid && std::abs(solution.before-1)<1e-9,"Identical overlap correlation");
 pt::Voice v;v.start(36,1,1,&p,&t,s);
 float peak=0;for(int i=0;i<8640;++i)peak=(std::max)(peak,std::abs(v.next(&p,&t,48000)[0]));
 ck(peak<=.500001f,"Correlated join boosted amplitude");
 // Negative correlation cannot cause compensation to boost either branch.
 for(float& x:t.left)x=-x;for(float& x:t.right)x=-x;
 s.joinCorrelation=-1;v.start(36,1,1,&p,&t,s);peak=0;
 for(int i=0;i<8640;++i)peak=(std::max)(peak,std::abs(v.next(&p,&t,48000)[0]));
 ck(peak<=.500001f,"Anti-phase join unexpectedly boosted");
 pt::Sample a;a.rate=48000;a.left.assign(4800,0);a.right=a.left;
 a.right[1000]=.001f;a.left[3000]=.0001f;a.left[2500]=.00005f;
 auto b=pt::silenceBounds(a);ck(b.valid && b.begin==976 && b.end==3025,"Stereo boundaries and safety padding");
 auto before=a;pt::cropSample(a,b.begin,b.end);
 ck(a.left.size()==2049,"Trim frame count");
 for(size_t i=0;i<a.left.size();++i)ck(a.left[i]==before.left[i+b.begin] && a.right[i]==before.right[i+b.begin],"Trim changed audible samples");
 b=pt::silenceBounds(a);ck(b.begin==0 && b.end==a.left.size(),"Repeated trim is not idempotent");
 a.left.assign(100,0);a.right=a.left;ck(!pt::silenceBounds(a).valid,"Silent file must remain intact");
 a.left.assign(100,.0001f);a.right=a.left;b=pt::silenceBounds(a);ck(b.begin==0 && b.end==100,"Quiet content must survive");
 std::cout<<"PASS correlated gain, cancellation, silence boundaries, stereo, quiet content and sample identity\n";
}

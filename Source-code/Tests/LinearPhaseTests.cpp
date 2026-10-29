#include "../Source/LinearPhaseEQ.h"
#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <new>
static thread_local bool countAllocations=false;
static thread_local size_t allocations=0;
void* operator new(size_t n){if(countAllocations)++allocations;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,size_t) noexcept {std::free(p);}
void operator delete[](void* p,size_t) noexcept {std::free(p);}
static void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
int main()try{
 for(double rate:{8000.,44100.,48000.,96000.,192000.}){
    std::array<tt::Band,tt::kBands> bands{};bands[0]={true,tt::Bell,0,300,6,.71};bands[1]={true,tt::HighShelf,0,4000,-4,.71};
    const auto k=tt::linear::design(bands,true,rate);
    for(size_t i=0;i<k->taps.size();++i)check(k->taps[i]==k->taps[k->taps.size()-1-i],"FIR is not exactly symmetric");
    for(double hz:{80.,300.,1000.,3000.}){if(hz>rate*.45)continue;std::complex<double> response{};
        for(size_t i=0;i<k->taps.size();++i)response+=double(k->taps[i])*std::polar(1.,-2*tt::pi*hz*(double(i)-k->radius)/rate);
        check(std::abs(response.imag())<1.e-9,"FIR has nonlinear phase");
        check(std::abs(20*std::log10(std::abs(response))-tt::responseDb(bands,hz,rate))<.3,"FIR magnitude mismatch");}
    tt::linear::Convolver fir;fir.prepare(k);
    for(int i=0;i<int(k->taps.size())+tt::linear::block;++i){const auto y=fir.process(i==0?1.f:0.f,i==0?-.5f:0.f);const int index=i-(tt::linear::block-1);const float expected=index>=0&&index<int(k->taps.size())?k->taps[size_t(index)]:0.f;check(std::abs(y[0]-expected)<3.e-6,"Partition impulse/latency mismatch");check(std::abs(y[1]+.5f*y[0])<1.e-7,"Stereo FIR mismatch");}
 }
 std::array<tt::Band,tt::kBands> bands{};
 auto neutral=tt::linear::design(bands,false,48000);tt::linear::Convolver flat;flat.prepare(neutral);
 for(int i=0;i<8000;++i){const float x=std::sin(i*.031f);const auto y=flat.process(x,x);const float expected=i>=neutral->latency()?std::sin((i-neutral->latency())*.031f):0.f;check(std::abs(y[0]-expected)<3.e-5,"Flat FIR not transparent after reported delay");}
 bands[0]={true,tt::Bell,0,160,9,1.2};auto k=tt::linear::design(bands,true,48000);tt::linear::NoteFilter notes;notes.prepare(k);
 const std::vector<int> starts{0,17,128,530,700,3500,4200};
 for(int t=0;t<9000;++t){int inputNote=-1,outputNote=-1;for(size_t n=0;n<starts.size();++n){if(t>=starts[n])inputNote=int(n);if(t>=starts[n]+k->latency())outputNote=int(n);}
    if(std::find(starts.begin(),starts.end(),t)!=starts.end())notes.startNote();
    const bool impulse=inputNote>=0 && t==starts[size_t(inputNote)]+5;
    const auto y=notes.process(impulse?.4f:0.f,impulse?-.2f:0.f);float expected=0;
    if(outputNote>=0){const int source=starts[size_t(outputNote)]+5;const bool occurred=size_t(outputNote+1)==starts.size()||source<starts[size_t(outputNote+1)];const int index=t-(tt::linear::block-1)-source;if(occurred&&index>=0&&index<int(k->taps.size()))expected=.4f*k->taps[size_t(index)];}
    check(std::abs(y[0]-expected)<3.e-6,"Retrigger failed exact boundary or destroyed pending previous audio");
 }
 // Publishing coefficients is smooth and leaves the delay line intact.
 flat.prepare(neutral);for(int i=0;i<12000;++i)flat.process(.1f,.1f);
 flat.setKernel(k);for(int i=0;i<12000;++i){auto y=flat.process(.1f,.1f);check(std::isfinite(y[0])&&std::abs(y[0]-.1f)<.001,"Coefficient transition changed settled DC or became unstable");}
 bands[0]={true,tt::LowShelf,0,1200,18,.71};auto shelf=tt::linear::design(bands,true,48000);
 flat.prepare(neutral);for(int i=0;i<12000;++i)flat.process(.05f,.05f);
 float last=.05f;allocations=0;countAllocations=true;flat.setKernel(shelf);
 for(int i=0;i<5000;++i){const auto y=flat.process(.05f,.05f);check(std::abs(y[0]-last)<.004,"EQ automation stepped rather than fading per sample");last=y[0];}
 notes.setKernel(shelf);notes.startNote();for(int i=0;i<5000;++i)notes.process(.01f,-.005f);
 countAllocations=false;auto mono=tt::linear::design(bands,true,48000,true);
 countAllocations=true;notes.setKernel(mono);notes.startNote();for(int i=0;i<7000;++i)notes.process(.02f,-.01f);
 countAllocations=false;check(allocations==0,"FIR processing/kernel adoption/note reset allocated on the audio path");
 check(std::abs(last-.05*std::pow(10.,18./20.))<.005,"EQ fade did not reach new coefficients");
 std::cout<<"PASS symmetric FIR, magnitude, stereo, rates, flat PDC, exact rapid note reset and live coefficient transition\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

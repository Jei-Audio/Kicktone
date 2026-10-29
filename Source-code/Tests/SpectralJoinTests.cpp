// v0.4.65 spectral join view: portable analysis checks (no JUCE).
#include "../Source/SpectralJoin.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
static void check(bool x,const char* m){if(!x)throw std::runtime_error(m);}

static pt::Sample tone(double rate,double seconds,double hz,double level=.5) {
    pt::Sample s;s.rate=rate;const size_t n=size_t(seconds*rate);s.left.resize(n);s.right.resize(n);
    for(size_t i=0;i<n;++i){const float v=float(level*std::sin(2*pt::pi*hz*double(i)/rate));s.left[i]=v;s.right[i]=v;}
    return s;
}

int main() {
  try {
    const double rate=48000;
    // Geometry: columns cover exactly [join-span, join+span], rows are log spaced.
    {
        auto s=tone(rate,1.0,1000.0);
        const auto a=pt::spectral::analyseJoin(s,500.0,50.0,96,1024,160);
        check(a.valid,"analysis invalid");
        check(a.rows==96 && a.columns>=140 && a.columns<=170,"unexpected grid size");
        check(std::abs(a.startMs-450.0)<1e-9 && std::abs(a.endMs-550.0)<1e-9 && a.joinMs==500.0,"span not centred on the join");
        check(std::abs(a.rowHz(0)-30.0)<1e-9 && std::abs(a.rowHz(95)-16000.0)<1e-6,"frequency axis bounds");
        check(a.rowHz(50)>a.rowHz(49),"rows not increasing");
        check(int(a.db.size())==a.rows*a.columns,"db grid size");
        // Peak row is at 1 kHz for every column; the view is normalised to 0 dB.
        float best=-1000;int bestRow=-1;
        for(int r=0;r<a.rows;++r)if(a.at(r,a.columns/2)>best){best=a.at(r,a.columns/2);bestRow=r;}
        check(std::abs(best)<1e-4f,"loudest bin is not 0 dB");
        check(std::abs(a.rowHz(bestRow)-1000.0)/1000.0<.08,"1 kHz tone not at the 1 kHz row");
        // Far from the tone the spectrum is deep (window side lobes well below).
        int lowRow=0;while(a.rowHz(lowRow)<120.0)++lowRow;
        check(a.at(lowRow,a.columns/2)<-40.f,"spectrum floor too high away from the tone");
        for(float v:a.db)check(v<=0.f && v>=-90.f && std::isfinite(v),"dB out of range");
        // A steady tone is spectrally continuous across the "join".
        check(a.typicalFlux>=1.0 && a.seamRatio()>=0 && a.seamRatio()<1.6,"steady tone reported a seam");
    }
    // A hard polarity flip at the join is a clear seam: flux several times the typical.
    {
        auto s=tone(rate,1.0,55.0,.8);
        // Flip a quarter period after 0.5 s, at the waveform peak (a flip at a
        // zero crossing would only bend the slope).
        const size_t flip=size_t(std::llround((.5+1.0/(4*55.0))*rate));
        const double joinMs=1000.0*double(flip)/rate;
        for(size_t i=flip;i<s.left.size();++i){s.left[i]=-s.left[i];s.right[i]=-s.right[i];}
        const auto a=pt::spectral::analyseJoin(s,joinMs,50.0);
        check(a.valid && a.seamRatio()>3.0,"polarity flip at the join was not detected as a seam");
        // Broadband energy appears at the join column, not far from it.
        const int c=a.columns/2;int highRow=a.rows-1;while(a.rowHz(highRow)>6000.0)--highRow;
        check(a.at(highRow,c)>a.at(highRow,2)+10.f,"click energy not localised at the join");
        // Same signal analysed 200 ms away from the flip: no seam there.
        const auto b=pt::spectral::analyseJoin(s,300.0,50.0);
        check(b.valid && b.seamRatio()<1.6,"seam reported where the signal is continuous");
    }
    // Wider spans give coarser hops; the window may extend past the audio.
    {
        auto s=tone(rate,.3,200.0);
        const auto a=pt::spectral::analyseJoin(s,250.0,200.0);
        check(a.valid && a.hopMs>pt::spectral::analyseJoin(s,250.0,25.0).hopMs,"hop does not grow with the span");
        check(a.columns>0 && std::isfinite(a.at(0,a.columns-1)),"frames past the end are not silent/finite");
    }
    // Invalid input is rejected without throwing.
    {
        pt::Sample empty;check(!pt::spectral::analyseJoin(empty,0,50).valid,"empty audio accepted");
        auto s=tone(rate,1.0,100.0);
        check(!pt::spectral::analyseJoin(s,500.0,0.0).valid,"zero span accepted");
        check(!pt::spectral::analyseJoin(s,500.0,50.0,96,1000).valid,"non power-of-two FFT accepted");
        check(!pt::spectral::analyseJoin(s,std::nan(""),50.0).valid,"NaN join accepted");
    }
    std::cout<<"SpectralJoin OK\n";return 0;
  } catch(const std::exception& e) { std::cerr<<"FAILED: "<<e.what()<<"\n";return 1; }
}

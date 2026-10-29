#pragma once
// v0.4.65 spectral join view.
//
// A short-time spectrogram of the rendered kick around the punch-to-tail join,
// plus a "seam" figure: the spectral flux (frame-to-frame change of the
// log-magnitude spectrum) at the join compared with the typical flux of the
// neighbouring frames. A clean join is spectrally continuous, so its flux is no
// higher than the frames around it; a click, a phase cancellation dip or a
// level step shows up as a broadband vertical line and a flux several times
// the typical value.
//
// Portable (no JUCE): built on the editor's message thread from the latest
// finished ProcessedPreview render - never in the audio callback, never in
// paint(). The editor turns the dB grid into an image and only draws that.
#include "Engine.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace pt::spectral {

struct JoinAnalysis {
    int columns=0, rows=0;            // time frames x log-spaced frequency rows
    std::vector<float> db;            // rows*columns, row-major, 0 dB = loudest bin, floor -90
    double startMs=0, endMs=0;        // analysed span on the kick's output timeline
    double joinMs=0;                  // join position (tail start) on that timeline
    double minHz=30, maxHz=16000;     // frequency axis (log)
    double seamFlux=0, typicalFlux=0; // flux at the join / median flux elsewhere
    double hopMs=0; size_t fftSize=0;
    bool valid=false;
    float at(int row,int column) const { return db[size_t(row)*size_t(columns)+size_t(column)]; }
    // Seam ratio: <= 1 = as continuous as the material around it (typical flux
    // is floored at 1 dB RMS). >= 3 is an audible step/click in practice.
    double seamRatio() const { return typicalFlux>0?seamFlux/typicalFlux:0.0; }
    double rowHz(int row) const {
        return rows>1?minHz*std::pow(maxHz/minHz,double(row)/double(rows-1)):minHz;
    }
    double columnMs(int column) const {
        return columns>0?startMs+(endMs-startMs)*(double(column)+.5)/double(columns):startMs;
    }
};

namespace detail {
// In-place iterative radix-2 complex FFT (n is a power of two).
inline void fft(std::vector<std::complex<double>>& a) {
    const size_t n=a.size();
    for(size_t i=1,j=0;i<n;++i) {
        size_t bit=n>>1;
        for(;j&bit;bit>>=1)j^=bit;
        j^=bit;
        if(i<j)std::swap(a[i],a[j]);
    }
    for(size_t len=2;len<=n;len<<=1) {
        const double ang=-2.0*pi/double(len);
        const std::complex<double> wl(std::cos(ang),std::sin(ang));
        for(size_t i=0;i<n;i+=len) {
            std::complex<double> w(1.0,0.0);
            for(size_t j=0;j<len/2;++j) {
                const auto u=a[i+j],v=a[i+j+len/2]*w;
                a[i+j]=u+v;a[i+j+len/2]=u-v;w*=wl;
            }
        }
    }
}
}

// audio: the rendered kick (output timeline starts at 0). joinMs: tail start.
// spanMs: half-width of the analysed window around the join. rows: frequency
// rows (log spaced). fftSize: power of two; 1024 gives ~23 ms windows at 44.1k.
inline JoinAnalysis analyseJoin(const Sample& audio,double joinMs,double spanMs,
                                int rows=96,size_t fftSize=1024,int targetColumns=160) {
    JoinAnalysis out;
    const size_t n=(std::min)(audio.left.size(),audio.right.size());
    if(n<fftSize || audio.rate<8000 || !std::isfinite(joinMs) || !std::isfinite(spanMs) || spanMs<=0 || rows<2 ||
       fftSize<64 || (fftSize&(fftSize-1))!=0 || targetColumns<8)return out;
    const double rate=audio.rate;
    out.joinMs=joinMs;out.fftSize=fftSize;out.rows=rows;
    out.startMs=joinMs-spanMs;out.endMs=joinMs+spanMs;
    out.maxHz=(std::min)(16000.0,rate*.5*.98);out.minHz=(std::min)(30.0,out.maxHz*.5);
    const double spanSamples=2.0*spanMs*.001*rate;
    const size_t hop=(std::max)(size_t(8),size_t(std::llround(spanSamples/double(targetColumns))));
    out.hopMs=1000.0*double(hop)/rate;
    const int columns=(std::max)(1,int(std::llround(spanSamples/double(hop))));
    out.columns=columns;
    // Linear-bin magnitudes per frame (mono sum, Hann window). Frames that fall
    // outside the audio read zeros there, so the view can extend past the end.
    const size_t bins=fftSize/2+1;
    std::vector<double> window(fftSize);
    for(size_t i=0;i<fftSize;++i)window[i]=.5-.5*std::cos(2.0*pi*(double(i)+.5)/double(fftSize));
    std::vector<std::vector<double>> logMag(size_t(columns),std::vector<double>(bins,0.0));
    std::vector<std::complex<double>> buffer(fftSize);
    double peak=1e-30;
    const double startSample=out.startMs*.001*rate;
    for(int c=0;c<columns;++c) {
        const double centre=startSample+(double(c)+.5)*double(hop);
        const long first=long(std::llround(centre))-long(fftSize/2);
        for(size_t i=0;i<fftSize;++i) {
            const long idx=first+long(i);
            const double v=idx>=0 && size_t(idx)<n?.5*(double(audio.left[size_t(idx)])+double(audio.right[size_t(idx)])):0.0;
            buffer[i]={v*window[i],0.0};
        }
        detail::fft(buffer);
        auto& frame=logMag[size_t(c)];
        for(size_t b=0;b<bins;++b){const double m=std::abs(buffer[b])/double(fftSize);frame[b]=m;peak=(std::max)(peak,m);}
    }
    // dB relative to the loudest bin of the view, floor -90 dB.
    const double floorDb=-90.0;
    for(auto& frame:logMag)for(auto& m:frame)m=(std::max)(floorDb,20.0*std::log10((std::max)(m,1e-30)/peak));
    // Log-frequency rows: max over the bins a row covers, interpolated where a
    // row is narrower than one bin (low frequencies).
    out.db.assign(size_t(rows)*size_t(columns),float(floorDb));
    const double binHz=rate/double(fftSize);
    for(int r=0;r<rows;++r) {
        const double hzLo=r==0?out.rowHz(0):std::sqrt(out.rowHz(r-1)*out.rowHz(r));
        const double hzHi=r==rows-1?out.rowHz(rows-1):std::sqrt(out.rowHz(r)*out.rowHz(r+1));
        const double centreBin=out.rowHz(r)/binHz;
        const size_t bLo=size_t(std::clamp(std::floor(hzLo/binHz),0.0,double(bins-1)));
        const size_t bHi=size_t(std::clamp(std::ceil(hzHi/binHz),double(bLo+1),double(bins)));
        for(int c=0;c<columns;++c) {
            const auto& frame=logMag[size_t(c)];
            double v;
            if(bHi-bLo>=2) { v=floorDb; for(size_t b=bLo;b<bHi;++b)v=(std::max)(v,frame[b]); }
            else {
                const size_t b0=size_t(std::clamp(std::floor(centreBin),0.0,double(bins-2)));
                const double t=std::clamp(centreBin-double(b0),0.0,1.0);
                v=frame[b0]*(1.0-t)+frame[b0+1]*t;
            }
            out.db[size_t(r)*size_t(columns)+size_t(c)]=float(v);
        }
    }
    // Spectral flux per frame transition (positive log-magnitude changes, 40 Hz
    // up to maxHz, so DC and the sub-30 Hz rumble do not dominate).
    if(columns>=3) {
        std::vector<double> flux(size_t(columns)-1,0.0);
        const size_t fLo=size_t(std::clamp(std::ceil(40.0/binHz),1.0,double(bins-1)));
        const size_t fHi=size_t(std::clamp(std::floor(out.maxHz/binHz),double(fLo+1),double(bins)));
        for(int c=1;c<columns;++c) {
            double sum=0;
            for(size_t b=fLo;b<fHi;++b){const double d=logMag[size_t(c)][b]-logMag[size_t(c)-1][b];sum+=d*d;}
            flux[size_t(c)-1]=std::sqrt(sum/double(fHi-fLo));
        }
        // The seam: transitions whose frames overlap the join (half a window
        // either side). Everything else is "typical".
        const double joinSample=joinMs*.001*rate;
        const double reach=double(fftSize)*.5;
        std::vector<double> others;double seam=0;int seamCount=0;
        for(int c=1;c<columns;++c) {
            const double a=startSample+(double(c)-.5)*double(hop),b=startSample+(double(c)+.5)*double(hop);
            const bool touches=b+reach>=joinSample && a-reach<=joinSample;
            if(touches){seam=(std::max)(seam,flux[size_t(c)-1]);++seamCount;}
            else others.push_back(flux[size_t(c)-1]);
        }
        if(seamCount>0 && others.size()>=3) {
            std::nth_element(others.begin(),others.begin()+ptrdiff_t(others.size()/2),others.end());
            // Typical flux never below 1 dB RMS: a perfectly steady signal has
            // only numerical flux, and its ratio must read "no seam", not noise.
            out.seamFlux=seam;out.typicalFlux=(std::max)(1.0,others[others.size()/2]);
        }
    }
    out.valid=true;
    return out;
}
} // namespace pt::spectral

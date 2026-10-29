#pragma once
#include "../Source/Engine.h"
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>
static uint16_t u16(const unsigned char* p){return uint16_t(p[0]|(uint16_t(p[1])<<8));}
static uint32_t u32(const unsigned char* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
static pt::Sample loadFloatWave(const char* file){
    std::ifstream f(file,std::ios::binary);if(!f)throw std::runtime_error("Missing real tail fixture");
    std::vector<unsigned char> b((std::istreambuf_iterator<char>(f)),{});
    if(b.size()<44 || std::memcmp(b.data(),"RIFF",4) || std::memcmp(b.data()+8,"WAVE",4))
        throw std::runtime_error("Fixture isn't RIFF/WAVE");
    int channels=0, format=0, bits=0; uint32_t sr=0;const unsigned char* samples=nullptr; size_t frames=0;
    for(size_t at=12;at+8<=b.size();){
        const auto size=u32(b.data()+at+4); const auto data=at+8;
        if(data+size>b.size())throw std::runtime_error("Bad RIFF chunk");
        if(!std::memcmp(b.data()+at,"fmt ",4) && size>=16){
            format=u16(b.data()+data);channels=u16(b.data()+data+2);
            sr=u32(b.data()+data+4);bits=u16(b.data()+data+14);
        }
        if(!std::memcmp(b.data()+at,"data",4)){
            samples=b.data()+data;frames=size/8;
        }
        at=data+size+(size%2);
    }
    if(format!=3 || channels!=2 || bits!=32 || sr!=44100 || samples==nullptr)
        throw std::runtime_error("Fixture must be stereo 32-bit float WAV 44.1kHz");
    pt::Sample x;x.rate=sr;x.left.resize(frames);x.right.resize(frames);
    for(size_t i=0;i<frames;++i){
        float l,r;std::memcpy(&l,samples+8*i,4);std::memcpy(&r,samples+8*i+4,4);
        x.left[i]=l;x.right[i]=r;
    }
    return x;
}

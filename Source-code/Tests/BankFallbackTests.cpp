#include "../Source/PitchBankPolicy.h"
#include <iostream>
#include <stdexcept>
int main() {
    pt::Sample src; src.rate=44100;
    src.left.assign(4410,0.4f); src.right.assign(4410,-0.4f);
    std::array<pt::Sample,128> notes;
    pt::fillDryPitchBank(notes,src);
    for(auto& n:notes) if(!pt::usablePitchResult(src,n) || n.left!=src.left)
        throw std::runtime_error("Fallback bank is not audible and intact");
    if(pt::kLowestPitchNote != 15 || pt::kHighestPitchNote != 51 || pt::kPitchNoteCount != 37)
        throw std::runtime_error("FL Studio pitch range MIDI mapping incorrect");
    for(int note=0; note<128; ++note) {
        const bool should = note>=15 && note<=51;
        if(pt::shouldRenderPitch(note,36) != should ||
           pt::shouldRenderPitch(note,15) != should ||
           pt::shouldRenderPitch(note,50) != should)
            throw std::runtime_error("D#1-D#3 note window incorrect");
    }
    pt::Sample damaged=src;std::fill(damaged.left.begin(),damaged.left.end(),0.0f);
    std::fill(damaged.right.begin(),damaged.right.end(),0.0f);
    if(pt::usablePitchResult(src,damaged))throw std::runtime_error("Silent pitches accepted");
    pt::Settings settings; settings.root=36;
    pt::Voice voice; voice.start(36,1,1.f,nullptr,&notes[36],settings);
    double level=0.0;
    for(int i=0;i<512;++i){auto v=voice.next(nullptr,&notes[36],44100);level+=std::abs(v[0]);}
    if(level<1.0)throw std::runtime_error("No audible tail when pitch prep fails");
    // Also verify a note outside the render range remains audibly dry.
    pt::Voice outside; settings.root=80;
    outside.start(80,1,1.f,nullptr,&notes[80],settings);
    double outsideLevel=0.;
    for(int i=0;i<512;++i)outsideLevel+=std::abs(outside.next(nullptr,&notes[80],44100)[0]);
    if(outsideLevel<1.0)throw std::runtime_error("Outside-range dry tail was muted");
    std::cout<<"PASS: only MIDI 15..51 are pitched; dry fallback remains audible\n";
    return 0;
}

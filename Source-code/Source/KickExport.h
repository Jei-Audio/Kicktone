#pragma once
// v0.4.62 Export WAV / drag-and-drop: writes the rendered kick (PreviewResult::audio)
// to a WAV file. Runs on the message thread, never in the audio callback.
#include "Engine.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace tt::exporter {
// 16 or 24 = PCM, 32 = 32-bit float.
inline bool writeWav(const pt::Sample& audio, const juce::File& file, int bitsPerSample, const juce::String& description = {}) {
    if(audio.left.empty() || audio.left.size()!=audio.right.size() || !(audio.rate>=8000.0) ||
       (bitsPerSample!=16 && bitsPerSample!=24 && bitsPerSample!=32)) return false;
    file.getParentDirectory().createDirectory();
    juce::TemporaryFile temp(file);   // write fully, then swap in: never leaves a half-written WAV
    std::unique_ptr<juce::OutputStream> stream=temp.getFile().createOutputStream();
    if(!stream) return false;
    juce::StringPairArray metadata;
    if(description.isNotEmpty())
        metadata=juce::WavAudioFormat::createBWAVMetadata(description,"Jei Audio Kicktone","",juce::Time::getCurrentTime(),0,"");
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream.get(),audio.rate,2,bitsPerSample,metadata,0));
    if(!writer) return false;
    stream.release();   // the writer owns the stream now
    const int frames=int(audio.left.size());
    const float* channels[2]={audio.left.data(),audio.right.data()};
    constexpr int block=8192;
    for(int start=0;start<frames;start+=block) {
        const int count=(std::min)(block,frames-start);
        const float* chunk[2]={channels[0]+start,channels[1]+start};
        if(!writer->writeFromFloatArrays(chunk,2,count)) return false;
    }
    writer.reset();
    return temp.overwriteTargetFileWithTemporary();
}
// A friendly file name such as "Kicktone Kick C3.wav" (FL Studio note names).
inline juce::String defaultFileName(int rootNote) {
    return "Kicktone Kick "+juce::MidiMessage::getMidiNoteName(rootNote,true,true,5)+".wav";
}
} // namespace tt::exporter

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_data_structures/juce_data_structures.h>
#include <iostream>
int main(int argc,char**argv){
 juce::AudioFormatManager f;f.registerBasicFormats();
 for(int i=1;i<argc;i++){
 juce::File file(juce::String::fromUTF8(argv[i])); juce::MemoryBlock bytes;if(!file.loadFileAsData(bytes))return 1;
 juce::ValueTree state("PunchTailState"),node("Tail");node.setProperty("audio",juce::var(bytes),nullptr);state.addChild(node,-1,nullptr);
 juce::MemoryBlock serial;juce::MemoryOutputStream stream(serial,false);state.writeToStream(stream);stream.flush();
 auto restored=juce::ValueTree::readFromData(serial.getData(),serial.getSize());auto value=restored.getChild(0).getProperty("audio");auto* blob=value.getBinaryData();if(!blob || *blob!=bytes)return 2;
 std::unique_ptr<juce::AudioFormatReader> r(f.createReaderFor(std::make_unique<juce::MemoryInputStream>(*blob,false)));if(!r)return 3;
 juce::AudioBuffer<float>b(2,int(r->lengthInSamples));if(!r->read(&b,0,b.getNumSamples(),0,true,true))return 4;
 std::cout<<"PASS decoded + embedded/restored "<<file.getFileName()<<" "<<r->lengthInSamples<<" frames "<<r->sampleRate<<" Hz\n";
 }return 0;
}

// v0.5 PERMANENT IDENTITY. Every release from v0.5 on is the same plugin to the
// host: same name, VST3 class ID (manufacturer + plugin code), bundle ID and
// state format, so a new version replaces the installed one and opens projects
// saved by earlier ones. The codes are v0.4.69's, so its projects open too.
// v0.5.29: the product was renamed from Tailtone to Kicktone at the user's
// request. Only the name changed; the codes, bundle ID and state stay.
// Never change these expectations to make a release pass.
#include "../Source/PluginProcessor.h"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <cstring>
static void check(bool x,const char* m){if(!x)throw std::runtime_error(m);}
#define TT_STR2(x) #x
#define TT_STR(x) TT_STR2(x)
static void set(PunchTailProcessor& p,const char* id,float v){auto* param=p.params.getParameter(id);param->setValueNotifyingHost(param->convertTo0to1(v));}
static float value(PunchTailProcessor& p,const char* id){return p.params.getRawParameterValue(id)->load();}
static juce::MemoryBlock save(PunchTailProcessor& p){juce::MemoryBlock b;p.getStateInformation(b);return b;}
static juce::MemoryBlock write(const juce::ValueTree& t){juce::MemoryBlock b;juce::MemoryOutputStream out(b,false);t.writeToStream(out);return b;}

static const std::pair<const char*,float> kSettings[]={{"punch2Pitch",3.5f},{"punchLayer",1.f},{"eq2On",0.f},{"punchFollowTail",1.f},{"lockSplit",1.f}};
static void checkSettings(PunchTailProcessor& p,const char* what){
    for(const auto& s:kSettings)if(std::abs(value(p,s.first)-s.second)>1e-4f)throw std::runtime_error(std::string(what)+": "+s.first+" not restored");
}

int main()try{
    juce::ScopedJuceInitialiser_GUI init;
    // Host-visible identity.
    check(std::strcmp(JucePlugin_Name,"Kicktone")==0,"PRODUCT_NAME must stay \"Kicktone\" (renamed from Tailtone in v0.5.29)");
    check(JucePlugin_ManufacturerCode==0x4a656941,"Manufacturer code must stay JeiA");
    check(JucePlugin_PluginCode==0x54614132,"Plugin code must stay TaA2 (the VST3 class ID is built from it)");
    check(std::strcmp(TT_STR(JucePlugin_CFBundleIdentifier),"audio.jei.tailtone")==0,"Bundle ID must stay audio.jei.tailtone");
    {const auto v=juce::StringArray::fromTokens(JucePlugin_VersionString,".","");
     check(v.size()==3&&(v[0].getIntValue()>0||v[1].getIntValue()>=5),"Version must be 0.5.0 or later");}
    auto aOwner=std::make_unique<PunchTailProcessor>();auto& a=*aOwner;
    check(a.getName()=="Kicktone","getName() must stay \"Kicktone\" (no version in the name)");
    check(PunchTailProcessor::versionString()==JucePlugin_VersionString,"Version string differs from the build version");

    // A project saved by this version opens in the next one.
    for(const auto& s:kSettings)set(a,s.first,s.second);
    const auto saved=save(a);
    const auto tree=juce::ValueTree::readFromData(saved.getData(),saved.getSize());
    check(tree.hasType("PunchTailState"),"State root type changed (older projects would not load)");
    check(tree.getProperty("savedWithVersion").toString()==JucePlugin_VersionString,"State does not record the version that saved it");
    {auto bOwner=std::make_unique<PunchTailProcessor>();auto& b=*bOwner;
     b.setStateInformation(saved.getData(),int(saved.getSize()));checkSettings(b,"Same-version project");}

    // A project saved before v0.5 (no version property, e.g. v0.4.69) opens.
    {auto old=tree.createCopy();old.removeProperty("savedWithVersion",nullptr);const auto bytes=write(old);
     auto cOwner=std::make_unique<PunchTailProcessor>();auto& c=*cOwner;
     c.setStateInformation(bytes.getData(),int(bytes.getSize()));checkSettings(c,"Pre-v0.5 project");}

    // A project from a NEWER version (unknown parameter and node) still opens; known settings survive.
    {auto future=tree.createCopy();future.setProperty("savedWithVersion","9.9.9",nullptr);
     juce::ValueTree param("PARAM");param.setProperty("id","someFutureParameter",nullptr);param.setProperty("value",0.75,nullptr);future.addChild(param,-1,nullptr);
     future.addChild(juce::ValueTree("FutureNode"),-1,nullptr);const auto bytes=write(future);
     auto dOwner=std::make_unique<PunchTailProcessor>();auto& d=*dOwner;
     d.setStateInformation(bytes.getData(),int(bytes.getSize()));checkSettings(d,"Newer-version project");
     check(d.params.getParameter("someFutureParameter")==nullptr,"Unknown parameter must be ignored");}

    // Garbage never changes the current settings.
    {const char junk[]="not a Tailtone state";a.setStateInformation(junk,int(sizeof junk));checkSettings(a,"Invalid state");}
    std::cout<<"PASS permanent identity: name Kicktone, JeiA/TaA2, audio.jei.tailtone, version "<<JucePlugin_VersionString
             <<"; projects from this, older (pre-v0.5) and newer versions load\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}

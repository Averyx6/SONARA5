#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"
#include <iostream>

int main(int argc,char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    SonaraAudioProcessor processor;processor.prepareToPlay(44100,512);
    if(!processor.generateTrackWithSeed("progressive house 128 BPM F minor 64 bars",0x460046))return 1;
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());if(!editor)return 2;
    const auto output=argc>1?juce::File(argv[1]):juce::File{};
    if(output!=juce::File{})output.createDirectory();
    for(const auto size:{juce::Point<int>(1180,720),juce::Point<int>(1320,820),juce::Point<int>(1900,1200)})
    {
        editor->setSize(size.x,size.y);
        for(const auto tab:{"SONG","MIDI","EXPORT","FX & MIX","INSTRUMENT","DRUMS","REFERENCE"})
        {
            for(int i=0;i<editor->getNumChildComponents();++i)
                if(auto* button=dynamic_cast<juce::TextButton*>(editor->getChildComponent(i));button&&button->getButtonText()==tab)
                    button->onClick();
            std::vector<juce::Component*> controls;
            for(int i=0;i<editor->getNumChildComponents();++i)
            {
                auto* c=editor->getChildComponent(i);if(!c->isVisible())continue;
                if(dynamic_cast<juce::Button*>(c)||dynamic_cast<juce::Slider*>(c)||dynamic_cast<juce::TextEditor*>(c))
                {
                    if(c->getWidth()<=0||c->getHeight()<=0||!editor->getLocalBounds().contains(c->getBounds()))
                    {std::cerr<<tab<<" control is off-screen: "<<c->getName()<<" "<<c->getBounds().toString()<<"\n";return 3;}
                    controls.push_back(c);
                }
            }
            for(size_t i=0;i<controls.size();++i)for(size_t j=i+1;j<controls.size();++j)
                if(controls[i]->getBounds().intersects(controls[j]->getBounds()))
                {std::cerr<<tab<<" overlapping controls: "<<controls[i]->getName()<<" / "<<controls[j]->getName()<<'\n';return 4;}
            if(juce::String(tab)=="SONG")
            {
                for(int i=0;i<editor->getNumChildComponents();++i)
                    if(auto* timeline=editor->getChildComponent(i);timeline->getName()=="Song timeline")
                    {
                        const auto song=processor.arrangementSnapshot();
                        for(const auto& section:song->getSections())if(section.name=="DROP")
                        {
                            const juce::Point<float> point(112.f+(timeline->getWidth()-112.f)*(section.startBar+section.bars*.5f)/(float)song->getBars(),12.f);
                            const auto now=juce::Time::getCurrentTime();
                            const juce::MouseEvent event(juce::Desktop::getInstance().getMainMouseSource(),point,juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),1.f,0.f,0.f,0.f,0.f,timeline,timeline,now,point,now,1,false);
                            timeline->mouseDown(event);
                            if(!processor.isSongPlaying()||processor.currentSongBar()!=section.startBar)return 7;
                            processor.pauseSongPreview();break;
                        }
                    }
            }
            const auto snapshot=editor->createComponentSnapshot(editor->getLocalBounds());
            if(snapshot.isNull())return 5;
            if(output!=juce::File{}&&juce::String(tab)=="SONG")
            {const auto file=output.getChildFile("SONARA-"+juce::String(size.x)+"x"+juce::String(size.y)+".png");file.deleteFile();juce::FileOutputStream stream(file);juce::PNGImageFormat png;if(!png.writeImageToStream(snapshot,stream))return 6;}
        }
    }
    // A worker must expose cancellation and safely join when the editor closes.
    bool started=false;
    for(int i=0;i<editor->getNumChildComponents();++i)
        if(auto* button=dynamic_cast<juce::TextButton*>(editor->getChildComponent(i));button&&button->getButtonText()=="GENERATE TRACK")
        {button->onClick();started=true;if(button->isEnabled())return 8;break;}
    if(!started)return 9;
    bool cancelVisible=false;
    for(int i=0;i<editor->getNumChildComponents();++i)
        if(auto* button=dynamic_cast<juce::TextButton*>(editor->getChildComponent(i));button&&button->getButtonText()=="CANCEL")
        {cancelVisible=button->isVisible()&&button->isEnabled();button->onClick();break;}
    if(!cancelVisible)return 10;
    editor.reset();
    std::cout<<"All seven tabs render; interactive controls fit without overlap at three sizes\n";
    return 0;
}

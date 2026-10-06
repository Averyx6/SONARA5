#include "PluginEditor.h"
#include <cmath>
#include <algorithm>

namespace {
const juce::Colour bg0(0xff05070d), bg1(0xff08111d), panel(0xff0d1422), panel2(0xff111a2b);
const juce::Colour purple(0xff7b4dff), cyan(0xff27dcff), pink(0xffff4dc4), text(0xffeaf5ff), dim(0xff75869d);
juce::Rectangle<float> inset(juce::Rectangle<int> r,float x=1.f){return r.toFloat().reduced(x);}

juce::Colour laneColour(size_t i){
    static const std::array<juce::Colour,11> colours {
        juce::Colour(0xffff9d3d),juce::Colour(0xffff6f91),juce::Colour(0xfff5d547),juce::Colour(0xffffae6a),
        juce::Colour(0xff27dcff),juce::Colour(0xff7b4dff),juce::Colour(0xff50e3a4),juce::Colour(0xff468cff),
        juce::Colour(0xffff4dc4),juce::Colour(0xffb96cff),juce::Colour(0xff91a1b8) };
    return colours[std::min(i,colours.size()-1)];
}
}

SonaraAudioProcessorEditor::SonaraLookAndFeel::SonaraLookAndFeel(){
    setColour(juce::TextButton::textColourOffId,text); setColour(juce::TextButton::textColourOnId,juce::Colours::white);
    setColour(juce::Slider::textBoxTextColourId,text); setColour(juce::Slider::textBoxBackgroundColourId,juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxOutlineColourId,juce::Colours::transparentBlack);
    setColour(juce::TextEditor::backgroundColourId,juce::Colour(0xff0a1220)); setColour(juce::TextEditor::textColourId,text); setColour(juce::TextEditor::outlineColourId,juce::Colour(0xff26344b));
}

void SonaraAudioProcessorEditor::SonaraLookAndFeel::drawButtonBackground(juce::Graphics& g,juce::Button& b,const juce::Colour& c,bool over,bool down){
    auto r=b.getLocalBounds().toFloat().reduced(.5f); auto base=c;
    if(over)base=base.brighter(.12f); if(down)base=base.darker(.18f);
    g.setColour(base.withAlpha(b.isEnabled()?1.f:.28f));g.fillRoundedRectangle(r,7.f);
    g.setColour((over?cyan:juce::Colour(0xff2a3850)).withAlpha(b.isEnabled()?.85f:.25f));g.drawRoundedRectangle(r,7.f,1.f);
}

void SonaraAudioProcessorEditor::SonaraLookAndFeel::drawRotarySlider(juce::Graphics& g,int x,int y,int w,int h,float pos,float start,float end,juce::Slider& s){
    const float size=(float)juce::jmin(w,h)-12.f; const auto c=juce::Point<float>(x+w*.5f,y+h*.45f); const auto radius=size*.5f;
    g.setColour(juce::Colour(0xff172239));g.fillEllipse(c.x-radius,c.y-radius,size,size);g.setColour(juce::Colour(0xff253553));g.drawEllipse(c.x-radius,c.y-radius,size,size,2.f);
    juce::Path track;track.addCentredArc(c.x,c.y,radius-4,radius-4,0.f,start,end,true);g.setColour(juce::Colour(0xff26344a));g.strokePath(track,juce::PathStrokeType(4.f));
    juce::Path value;value.addCentredArc(c.x,c.y,radius-4,radius-4,0.f,start,start+pos*(end-start),true);g.setColour(cyan);g.strokePath(value,juce::PathStrokeType(4.f,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));
    const float a=start+pos*(end-start);const auto p=c+juce::Point<float>(std::sin(a),-std::cos(a))*(radius-9.f);g.setColour(juce::Colours::white);g.fillEllipse(p.x-2,p.y-2,4,4);
    g.setColour(text.withAlpha(.65f));g.setFont(10.f);g.drawText(s.getName(),x,y+h-18,w,14,juce::Justification::centred);
}

void SonaraAudioProcessorEditor::ExternalDragButton::mouseDrag(const juce::MouseEvent& e){if(!gestureStarted&&e.getDistanceFromDragStart()>6){gestureStarted=true;owner.beginExternalDrag(kind);}}

void SonaraAudioProcessorEditor::SoundDNAView::paint(juce::Graphics& g){
    auto r=getLocalBounds().toFloat().reduced(12.f);const auto& d=processor.currentPatch();
    g.setColour(panel2);g.fillRoundedRectangle(r,13.f);g.setColour(purple.withAlpha(.5f));g.drawRoundedRectangle(r,13.f,1.f);
    auto graph=r.reduced(18.f,24.f);graph.removeFromBottom(34.f);
    juce::ColourGradient glow(purple.withAlpha(.28f),graph.getCentreX(),graph.getCentreY(),cyan.withAlpha(.03f),graph.getRight(),graph.getBottom(),true);g.setGradientFill(glow);g.fillRoundedRectangle(graph,10.f);
    const int bars=88; for(int i=0;i<bars;++i){const float x=graph.getX()+graph.getWidth()*i/(bars-1.f);const float seed=std::sin((float)i*.31f+(float)(d.seed%997)*.011f+animation);const float shape=.25f+.75f*std::abs(seed*std::sin((float)i*.071f+d.macroMovement*4.f));const float amp=(12.f+shape*graph.getHeight()*.34f)*(0.45f+d.macroImpact*.55f);g.setColour(juce::Colour::fromHSV(.68f-(float)i/bars*.22f,.72f,.98f,.28f+.55f*shape));g.drawLine(x,graph.getCentreY()-amp*.5f,x,graph.getCentreY()+amp*.5f,1.25f);}
    g.setColour(text);g.setFont(juce::FontOptions(20.f).withStyle("Bold"));g.drawText(d.name,r.getX()+18,r.getY()+10,r.getWidth()-36,24,juce::Justification::left);
    g.setColour(dim);g.setFont(11.f);g.drawText("SOUND DNA • seed "+juce::String(d.seed)+" • unison "+juce::String(d.unison)+" • cutoff "+juce::String((int)d.cutoff)+" Hz",r.getX()+18,r.getBottom()-31,r.getWidth()-36,18,juce::Justification::left);
}

void SonaraAudioProcessorEditor::TimelineView::paint(juce::Graphics& g)
{
    auto r=getLocalBounds().toFloat();
    g.setColour(panel2);g.fillRoundedRectangle(r,10.f);
    auto a=processor.arrangementSnapshot();
    if(!a)
    {
        g.setColour(dim);g.setFont(14.f);
        g.drawText("Generate a track to build the arrangement timeline",getLocalBounds(),juce::Justification::centred);
        return;
    }

    const auto& lanes=a->getLanes();
    const auto& sections=a->getSections();
    const int bars=a->getBars();
    const double totalBeats=a->getTotalBeats();
    const float labelW=112.f,headerH=44.f;
    const float rowH=(r.getHeight()-headerH)/(float)juce::jmax(1,(int)lanes.size());
    const auto header=juce::Rectangle<float>(r.getX()+labelW,r.getY(),r.getWidth()-labelW,headerH);
    const auto grid=r.withTrimmedLeft(labelW).withTrimmedTop(headerH);
    const int currentBar=processor.currentSongBar();

    g.setColour(juce::Colour(0xff08111e));
    g.fillRoundedRectangle(r.getX()+4.f,r.getY()+4.f,labelW-8.f,headerH-8.f,6.f);
    g.setColour(cyan.withAlpha(.82f));g.setFont(8.5f);
    g.drawFittedText("SECTIONS\nCLICK TO PLAY",(int)r.getX()+10,(int)r.getY()+8,(int)labelW-20,(int)headerH-14,
                     juce::Justification::centredLeft,2);

    for(const auto& s:sections)
    {
        const float x=header.getX()+header.getWidth()*s.startBar/(float)bars;
        const float w=header.getWidth()*s.bars/(float)bars;
        const bool active=currentBar>=s.startBar&&currentBar<s.startBar+s.bars;
        const bool impact=s.name.contains("DROP")||s.name.contains("HOOK");
        const auto base=impact?purple:cyan;

        g.setColour(base.withAlpha(active?.42f:(.10f+.12f*s.energy)));
        g.fillRoundedRectangle(x+1.f,header.getY()+4.f,juce::jmax(2.f,w-2.f),headerH-8.f,5.f);
        g.setColour(active?juce::Colours::white:text.withAlpha(.65f));
        g.setFont(active?9.5f:8.3f);
        g.drawFittedText(s.name,(int)x+5,(int)header.getY()+7,juce::jmax(8,(int)w-10),16,
                         juce::Justification::centredLeft,1);
        g.setColour(active?cyan.withAlpha(.9f):dim.withAlpha(.8f));
        g.setFont(7.5f);
        g.drawFittedText("BAR "+juce::String(s.startBar+1),(int)x+5,(int)header.getY()+23,
                         juce::jmax(8,(int)w-10),11,juce::Justification::centredLeft,1);
    }

    for(const auto& s:sections)
    {
        const float x=grid.getX()+grid.getWidth()*s.startBar/(float)bars;
        const float w=grid.getWidth()*s.bars/(float)bars;
        const bool active=currentBar>=s.startBar&&currentBar<s.startBar+s.bars;
        const bool impact=s.name.contains("DROP")||s.name.contains("HOOK");
        g.setColour((impact?purple:cyan).withAlpha(active?.10f:.045f+.055f*s.energy));
        g.fillRect(x,grid.getY(),w,grid.getHeight());
        g.setColour(juce::Colour(0xff26344c).withAlpha(.8f));
        g.drawVerticalLine((int)x,grid.getY(),grid.getBottom());
    }

    for(size_t li=0;li<lanes.size();++li)
    {
        const auto& lane=lanes[li];
        const float y=grid.getY()+li*rowH;
        g.setColour(li==(size_t)processor.getSelectedLane()?juce::Colour(0xff15263b):juce::Colour(0xff0c1523));
        g.fillRect(r.getX(),y,r.getWidth(),juce::jmax(1.f,rowH-1.f));
        g.setColour(laneColour(li));g.fillRoundedRectangle(r.getX()+5.f,y+4.f,3.f,juce::jmax(2.f,rowH-8.f),1.5f);
        g.setColour(text.withAlpha(li==(size_t)processor.getSelectedLane()?.95f:.72f));g.setFont(9.5f);
        g.drawFittedText(lane.name,(int)r.getX()+14,(int)y,(int)labelW-20,(int)rowH,
                         juce::Justification::centredLeft,1);

        for(const auto& n:lane.notes)
        {
            const float x=grid.getX()+grid.getWidth()*(float)(n.beat/totalBeats);
            const float w=juce::jmax(1.2f,grid.getWidth()*(float)(n.length/totalBeats));
            const float alpha=.28f+.44f*(n.velocity/127.f);
            g.setColour(laneColour(li).withAlpha(alpha));
            g.fillRoundedRectangle(x,y+rowH*.27f,w,juce::jmax(2.f,rowH*.46f),1.5f);
        }
    }

    const float px=grid.getX()+grid.getWidth()*(float)processor.songPosition01();
    g.setColour(juce::Colours::white.withAlpha(processor.isSongPlaying()?.98f:.55f));
    g.drawLine(px,header.getY()+2.f,px,r.getBottom(),processor.isSongPlaying()?1.8f:1.1f);
    g.setColour(cyan);
    juce::Path marker;
    marker.startNewSubPath(px-5.f,header.getY()+1.f);
    marker.lineTo(px+5.f,header.getY()+1.f);
    marker.lineTo(px,header.getY()+8.f);
    marker.closeSubPath();
    g.fillPath(marker);

    const auto sectionName=processor.currentSectionName();
    g.setColour(juce::Colour(0xff06101c).withAlpha(.92f));
    g.fillRoundedRectangle(r.getRight()-190.f,r.getBottom()-22.f,182.f,16.f,5.f);
    g.setColour(cyan.withAlpha(.9f));g.setFont(8.5f);
    g.drawFittedText("BAR "+juce::String(currentBar+1)+"/"+juce::String(bars)+" • "+sectionName,
                     (int)r.getRight()-184,(int)r.getBottom()-20,170,12,
                     juce::Justification::centredRight,1);
}

void SonaraAudioProcessorEditor::TimelineView::mouseDown(const juce::MouseEvent& e)
{
    auto a=processor.arrangementSnapshot();
    if(!a)return;

    constexpr float headerH=44.f;
    constexpr float labelW=112.f;

    if(e.position.y<headerH&&e.position.x>=labelW)
    {
        const float normalized=juce::jlimit(0.f,.9999f,(e.position.x-labelW)/juce::jmax(1.f,(float)getWidth()-labelW));
        const int clickedBar=juce::jlimit(0,a->getBars()-1,(int)std::floor(normalized*a->getBars()));
        for(const auto& s:a->getSections())
            if(clickedBar>=s.startBar&&clickedBar<s.startBar+s.bars)
            {
                processor.startSongPreviewAtBar(s.startBar);
                repaint();
                return;
            }
    }

    if(e.position.y>=headerH)
    {
        const auto& lanes=a->getLanes();
        if(lanes.empty())return;
        const float rowH=(getHeight()-headerH)/(float)lanes.size();
        const int lane=juce::jlimit(0,(int)lanes.size()-1,(int)((e.position.y-headerH)/rowH));
        processor.setSelectedLane(lane);
        repaint();
    }
}

void SonaraAudioProcessorEditor::PianoRollView::paint(juce::Graphics& g)
{
    auto r=getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff08111d));g.fillRoundedRectangle(r,9.f);

    auto a=processor.arrangementSnapshot();
    if(!a||!juce::isPositiveAndBelow(processor.getSelectedLane(),(int)a->getLanes().size()))
    {
        g.setColour(dim);g.setFont(12.f);
        g.drawText("PIANO ROLL • select a lane",getLocalBounds(),juce::Justification::centred);
        return;
    }

    const auto& lane=a->getLanes()[(size_t)processor.getSelectedLane()];
    constexpr float keyW=62.f,headerH=24.f;
    auto header=juce::Rectangle<float>(r.getX(),r.getY(),r.getWidth(),headerH);
    auto keys=juce::Rectangle<float>(r.getX(),r.getY()+headerH,keyW,r.getHeight()-headerH);
    auto grid=juce::Rectangle<float>(r.getX()+keyW,r.getY()+headerH,r.getWidth()-keyW,r.getHeight()-headerH);

    const int viewBars=8;
    const int bars=a->getBars();
    const int currentBar=(int)std::floor(processor.songPosition01()*bars);
    const int firstBar=juce::jlimit(0,juce::jmax(0,bars-viewBars),(currentBar/viewBars)*viewBars);
    const double beat0=firstBar*4.0,beat1=(firstBar+viewBars)*4.0;

    int minVisible=127,maxVisible=0;
    for(const auto& n:lane.notes)
        if(n.beat+n.length>=beat0&&n.beat<=beat1)
        {
            minVisible=juce::jmin(minVisible,n.note);
            maxVisible=juce::jmax(maxVisible,n.note);
        }
    if(maxVisible<=minVisible){minVisible=48;maxVisible=84;}
    minVisible=juce::jmax(24,minVisible-5);
    maxVisible=juce::jmin(108,maxVisible+5);
    if(maxVisible-minVisible<24)
    {
        const int centre=(minVisible+maxVisible)/2;
        minVisible=juce::jmax(24,centre-12);
        maxVisible=juce::jmin(108,minVisible+24);
        minVisible=juce::jmax(24,maxVisible-24);
    }
    const int pitchRows=maxVisible-minVisible+1;
    const float rowH=grid.getHeight()/(float)juce::jmax(1,pitchRows);

    g.setColour(juce::Colour(0xff0a1524));g.fillRect(header);
    g.setColour(text.withAlpha(.92f));g.setFont(juce::FontOptions(10.f).withStyle("Bold"));
    g.drawText(lane.name+"  •  BARS "+juce::String(firstBar+1)+"–"+juce::String(juce::jmin(bars,firstBar+viewBars)),
               (int)header.getX()+8,(int)header.getY()+3,(int)header.getWidth()-16,17,juce::Justification::left);

    auto isBlack=[](int note)
    {
        const int pc=((note%12)+12)%12;
        return pc==1||pc==3||pc==6||pc==8||pc==10;
    };

    for(int note=minVisible;note<=maxVisible;++note)
    {
        const int row=maxVisible-note;
        const float y=grid.getY()+row*rowH;
        const bool black=isBlack(note);

        g.setColour(black?juce::Colour(0xff0a1320):juce::Colour(0xff0d1827));
        g.fillRect(grid.getX(),y,grid.getWidth(),rowH);
        g.setColour(black?juce::Colour(0xff101a28):juce::Colour(0xffc8d1db).withAlpha(.14f));
        g.fillRect(keys.getX(),y,keys.getWidth(),rowH);

        g.setColour(juce::Colour(0xff1b2a3e).withAlpha(.72f));
        g.drawHorizontalLine((int)(y+rowH),grid.getX(),grid.getRight());

        if(note%12==0&&rowH>=5.f)
        {
            g.setColour(text.withAlpha(.58f));g.setFont(juce::jmax(7.f,juce::jmin(9.f,rowH*.78f)));
            g.drawFittedText(juce::MidiMessage::getMidiNoteName(note,true,true,4),
                             (int)keys.getX()+5,(int)y,(int)keys.getWidth()-8,(int)juce::jmax(6.f,rowH),
                             juce::Justification::centredLeft,1);
        }
    }

    const int beatLines=viewBars*4;
    for(int i=0;i<=beatLines;++i)
    {
        const float x=grid.getX()+grid.getWidth()*i/(float)beatLines;
        const bool barLine=i%4==0;
        g.setColour(barLine?juce::Colour(0xff52627a).withAlpha(.72f):juce::Colour(0xff26364b).withAlpha(.48f));
        g.drawVerticalLine((int)x,grid.getY(),grid.getBottom());
        if(barLine&&i<beatLines)
        {
            g.setColour(dim.withAlpha(.70f));g.setFont(7.5f);
            g.drawText(juce::String(firstBar+i/4+1),(int)x+3,(int)grid.getY()+2,28,11,juce::Justification::left);
        }
    }

    for(const auto& n:lane.notes)
    {
        if(n.beat+n.length<beat0||n.beat>beat1)continue;
        const float x=grid.getX()+grid.getWidth()*(float)((n.beat-beat0)/(beat1-beat0));
        const float w=juce::jmax(3.f,grid.getWidth()*(float)(n.length/(beat1-beat0)));
        const int clamped=juce::jlimit(minVisible,maxVisible,n.note);
        const float y=grid.getY()+(maxVisible-clamped)*rowH;
        const float noteH=juce::jmax(3.f,rowH-1.2f);
        const auto colour=laneColour((size_t)processor.getSelectedLane());

        g.setColour(colour.withAlpha(.16f));g.fillRoundedRectangle(x-1.f,y-.4f,w+2.f,noteH+1.f,2.f);
        g.setColour(colour.withAlpha(.92f));g.fillRoundedRectangle(x,y,w,noteH,1.8f);
        g.setColour(juce::Colours::white.withAlpha(.20f));g.drawHorizontalLine((int)(y+1.f),x+1.f,x+w-1.f);

        if(w>28.f&&noteH>8.f)
        {
            g.setColour(juce::Colours::white.withAlpha(.84f));g.setFont(7.5f);
            g.drawFittedText(juce::MidiMessage::getMidiNoteName(n.note,true,true,4),
                             (int)x+3,(int)y,(int)w-5,(int)noteH,juce::Justification::centredLeft,1);
        }
    }

    const double currentBeat=processor.songPosition01()*a->getTotalBeats();
    if(currentBeat>=beat0&&currentBeat<=beat1)
    {
        const float px=grid.getX()+grid.getWidth()*(float)((currentBeat-beat0)/(beat1-beat0));
        g.setColour(juce::Colour(0xffff9a42).withAlpha(.95f));
        g.drawVerticalLine((int)px,grid.getY(),grid.getBottom());
    }

    g.setColour(juce::Colour(0xff2a3950));
    g.drawRoundedRectangle(r.reduced(.5f),9.f,1.f);
}

SonaraAudioProcessorEditor::SonaraAudioProcessorEditor(SonaraAudioProcessor& x):AudioProcessorEditor(&x),p(x){
    setLookAndFeel(&look);setSize(1320,820);setResizable(true,true);setResizeLimits(1180,720,1900,1200);setOpaque(true);
    const auto restoredSoundPrompt=p.currentPatch().sourcePrompt.trim();
    const auto restoredSongPrompt=p.getCurrentSongPrompt().trim();
    soundPrompt.setText(restoredSoundPrompt.isNotEmpty()?restoredSoundPrompt:
        juce::String("Future rave lead, aggressive bright festival, wide fast attack"));
    songPrompt.setText(restoredSongPrompt.isNotEmpty()?restoredSongPrompt:
        juce::String("Emotional progressive house, 128 BPM, F minor, strong hook clear main melody, simple melody clean melody no random notes, short intro, big chorus, drum build snare roll before drop, powerful long drop, warm chords, deep bass, more space"));
    for(auto* editor:{&soundPrompt,&songPrompt,&laneSoundPrompt})
    {
        editor->setMultiLine(false);
        editor->setFont(14.f);
        editor->setColour(juce::TextEditor::backgroundColourId,juce::Colour(0xff08111e));
        editor->setColour(juce::TextEditor::outlineColourId,juce::Colour(0xff263650));
        editor->setColour(juce::TextEditor::focusedOutlineColourId,cyan.withAlpha(.85f));
        addAndMakeVisible(*editor);
    }
    soundPrompt.setTextToShowWhenEmpty("Describe one custom sound...",dim);
    songPrompt.onTextChange=[this]{p.setSongPromptDraft(songPrompt.getText());};
    seedInput.setInputRestrictions(18,"0123456789abcdefABCDEFxX");seedInput.setFont(juce::FontOptions(11.f));seedInput.setTextToShowWhenEmpty("16-digit hexadecimal seed",dim);addAndMakeVisible(seedInput);addAndMakeVisible(reproduceSeed);styleButton(reproduceSeed);
    reproduceSeed.onClick=[this]{auto text=seedInput.getText().trim().toLowerCase().replace("0x","");if(text.isEmpty()||text.length()>16||text!=text.retainCharacters("0123456789abcdef")){showStatus("Enter a hexadecimal seed of up to 16 digits");return;}const auto seed=(uint64_t)text.getHexValue64();const auto prompt=songPrompt.getText();runWork([this,prompt,seed]{p.generateTrackWithSeed(prompt,seed);},[this]{bpm.setValue(p.getPreviewBpm(),juce::dontSendNotification);setTab(1);});};
    songPrompt.setTextToShowWhenEmpty("Describe the whole song: genre, key, BPM, energy, drop, instruments...",dim);
    laneSoundPrompt.setTextToShowWhenEmpty("Describe ONLY the selected lane sound: e.g. warm supersaw lead, clean pluck, donk bass...",dim);

    std::array<juce::Button*,46> buttons {&generateSound,&generateTrack,&generateDrums,&randomizeEverythingButton,&surpriseMe,&similar,&mutate,&randomize,&undo,&redo,
        &variation1,&variation2,&variation3,&variation4,&captureA,&captureB,&recallA,&recallB,&connect,&previewSound,&playSong,&playChorus,&playDrop,&stop,
        &dragPreviewMidi,&dragFullMidi,&dragLaneMidi,&dragLeadMidi,&dragReferenceMidi,&dragReferenceAudio,&dragFullAudio,&dragLaneAudio,&dragLeadAudio,&dragStems,
        &loadReference,&importMidi,&resound,&rebuildReference,&saveSoundButton,&loadSoundButton,&saveProjectButton,&loadProjectButton,&exportMixButton,&exportStemsButton,
        &applyLaneSound,&autoLaneSound};
    for(auto* b:buttons){addAndMakeVisible(*b);styleButton(*b,b==&generateSound||b==&generateTrack||b==&generateDrums||b==&randomizeEverythingButton||b==&surpriseMe||b==&playSong||b==&playChorus||b==&playDrop||b==&resound||b==&rebuildReference||b==&exportMixButton||b==&exportStemsButton);}
    for(auto* button:{&exportFullMidiButton,&exportLaneMidiButton}){addAndMakeVisible(*button);styleButton(*button);}
    exportFullMidiButton.onClick=[this]{chooseExportMidi(false);};exportLaneMidiButton.onClick=[this]{chooseExportMidi(true);};
    dragLaneMidi.setTooltip("Selected lane notes for FL Piano Roll. Drop onto a SONARA channel to use that lane's SoundDNA; other instruments will sound different.");
    dragLeadMidi.setTooltip("Always exports only the generated LEAD notes for FL Piano Roll. No BASS, SUB, CHORDS, PAD or drum notes are included.");
    dragFullMidi.setTooltip("Drag the WHOLE generated song as multitrack MIDI: drums, bass, sub, chords, pluck, pad, lead, counter and FX stay on separate named tracks with full song timing.");
    dragPreviewMidi.setTooltip("Sound-preview notes. Drop onto the same SONARA instrument to keep its SoundDNA; use WAV for exact rendered audio.");
    dragLaneAudio.setTooltip("Rendered selected lane with SONARA SoundDNA preserved. Drag to FL Playlist for the exact lane sound.");
    dragLeadAudio.setTooltip("Always renders the generated LEAD with its SONARA SoundDNA. Drag to FL Playlist when you want the lead sound preserved.");
    dragReferenceAudio.setTooltip("Rendered extracted reference melody using the current RESOUND SoundDNA. Drag to FL Playlist for the actual resounded audio.");
    dragFullAudio.setTooltip("Rendered full mix using the same SONARA song mixer as preview. Drag to FL Playlist for preview-matched sound.");

    dragFullAudio.onClick=[this]{const auto kind=ExternalDragButton::Kind::fullMixAudio;const auto it=dragCache.find((int)kind);if(it!=dragCache.end()&&it->second.key==dragCacheKey(kind))showStatus("Audio ready • drag this button to the FL Playlist");else beginExternalDrag(kind);};
    dragLaneAudio.onClick=[this]{const auto kind=ExternalDragButton::Kind::laneAudio;const auto it=dragCache.find((int)kind);if(it!=dragCache.end()&&it->second.key==dragCacheKey(kind))showStatus("Audio ready • drag this button to the FL Playlist");else beginExternalDrag(kind);};
    dragLeadAudio.onClick=[this]{const auto kind=ExternalDragButton::Kind::leadAudio;const auto it=dragCache.find((int)kind);if(it!=dragCache.end()&&it->second.key==dragCacheKey(kind))showStatus("Audio ready • drag this button to the FL Playlist");else beginExternalDrag(kind);};
    dragStems.onClick=[this]{const auto kind=ExternalDragButton::Kind::stemsAudio;const auto it=dragCache.find((int)kind);if(it!=dragCache.end()&&it->second.key==dragCacheKey(kind))showStatus("Audio ready • drag this button to the FL Playlist");else beginExternalDrag(kind);};
    dragReferenceAudio.onClick=[this]{const auto kind=ExternalDragButton::Kind::referenceAudio;const auto it=dragCache.find((int)kind);if(it!=dragCache.end()&&it->second.key==dragCacheKey(kind))showStatus("Audio ready • drag this button to the FL Playlist");else beginExternalDrag(kind);};
    juce::TextButton* tabs[]={&tabInstrument,&tabSong,&tabDrums,&tabFx,&tabReference,&tabMidi,&tabExport};
    for(auto* t:tabs){addAndMakeVisible(*t);styleButton(*t);}
    tabInstrument.onClick=[this]{setTab(0);};tabSong.onClick=[this]{setTab(1);};tabDrums.onClick=[this]{setTab(2);p.setSelectedLane(0);};
    tabFx.onClick=[this]{setTab(3);};tabReference.onClick=[this]{setTab(4);};tabMidi.onClick=[this]{setTab(5);};tabExport.onClick=[this]{setTab(6);};

    for(auto* b:{&lockOsc,&lockUnison,&lockEnv,&lockFilter,&lockMod,&lockSources,&lockTone,&lockFx})addAndMakeVisible(*b);
    configureMacro(macroBrightness,"BRIGHT");configureMacro(macroMovement,"MOVE");configureMacro(macroSpace,"SPACE");configureMacro(macroImpact,"IMPACT");configureMixSlider(mixLevel,0.0,1.5," LEVEL");configureMixSlider(mixPan,-1.0,1.0," PAN");configureMixSlider(mixWidth,0.0,1.5," WIDTH");configureMixSlider(mixFx,0.0,1.5," FX");
    bpm.setTooltip("Existing song tempo is shown here. Set a new song tempo explicitly in the prompt; this control sets sound-preview and fallback generation tempo.");
    bpm.setRange(60,200,1);bpm.setValue(p.getPreviewBpm(),juce::dontSendNotification);bpm.setSliderStyle(juce::Slider::LinearHorizontal);bpm.setTextBoxStyle(juce::Slider::TextBoxRight,false,64,20);bpm.setTextValueSuffix(" BPM");addAndMakeVisible(bpm);
    patchName.setColour(juce::Label::textColourId,text);patchName.setFont(juce::FontOptions(15.f).withStyle("Bold"));addAndMakeVisible(patchName);
    statusLine.setColour(juce::Label::textColourId,dim);statusLine.setFont(11.f);addAndMakeVisible(statusLine);
    selectedLaneLabel.setColour(juce::Label::textColourId,text.withAlpha(.75f));selectedLaneLabel.setFont(11.f);addAndMakeVisible(selectedLaneLabel);
    referenceSummary.setColour(juce::Label::textColourId,text.withAlpha(.82f));referenceSummary.setFont(juce::FontOptions(13.f));referenceSummary.setJustificationType(juce::Justification::topLeft);addAndMakeVisible(referenceSummary);
    timeline.setName("Song timeline");pianoRoll.setName("Selected lane piano roll");
    addAndMakeVisible(soundView);addAndMakeVisible(timeline);addAndMakeVisible(pianoRoll);addAndMakeVisible(playbackBar);

    const std::array<juce::String,8> soundPresetPrompts={
        "Emotional progressive house lead, wide warm powerful",
        "Future rave lead, aggressive bright festival",
        "Warm short pluck, organic attack, wide delay",
        "Deep reese bass, mono sub, moving texture",
        "Dreamy ambient pad, evolving wide soft",
        "Tech house bass, tight punchy dark",
        "Cinematic bell, metallic organic spacious",
        "Experimental morphing synth, animated texture"
    };
    const std::array<juce::String,8> songPresetPrompts={
        "Emotional progressive house, 128 BPM, F minor, strong hook clear main melody, simple melody, short intro, big chorus, drum build, powerful long drop, warm chords, deep bass, more space",
        "Future rave, 128 BPM, F minor, strong hook, clean melody no random notes, short intro, festival drum build, big chorus, massive powerful drop, dark bass, sharp synths",
        "Melodic EDM pop, 124 BPM, radio structure, catchy strong hook, simple melody, clean bass, warm chords, punchy drums, big chorus, uplifting final drop",
        "Tech house, 126 BPM, dark tight groove, rolling bass, sparse clean hook, less busy, short intro, club arrangement, short breakdown",
        "Drum and bass, 174 BPM, energetic minor key, clear hook, moving bass, fast drums, strong drum build, atmospheric breakdown, powerful final drop",
        "Electro pop, 122 BPM, radio structure, catchy clean main melody, simple melody, glossy synths, big chorus, punchy drums, more space",
        "Cinematic EDM, 128 BPM, emotional minor key, clear main melody, wide pads, dramatic build, big chorus, powerful melodic drop, long breakdown",
        "Experimental EDM, 130 BPM, unusual groove, evolving harmony, animated synths, but keep a strong hook and clear song structure"
    };
    for(size_t i=0;i<presets.size();++i)
    {
        addAndMakeVisible(presets[i]);styleButton(presets[i]);
        presets[i].onClick=[this,i,soundPresetPrompts,songPresetPrompts]
        {
            if(activeTab==0) soundPrompt.setText(soundPresetPrompts[i]);
            else songPrompt.setText(songPresetPrompts[i]);
        };
    }

    const std::array<juce::String,10> suggestionText={
        "strong hook clear main melody",
        "simple melody clean melody no random notes",
        "short intro",
        "big chorus",
        "long drop",
        "drum build snare roll before drop",
        "radio structure song structure",
        "powerful drop",
        "more space less busy",
        "no counter melody main melody only"
    };
    for(size_t i=0;i<promptSuggestions.size();++i)
    {
        addAndMakeVisible(promptSuggestions[i]);
        styleButton(promptSuggestions[i],i==0||i==1||i==7);
        promptSuggestions[i].setTooltip("Add ""+suggestionText[i]+"" to the song prompt");
        promptSuggestions[i].onClick=[this,i,suggestionText]
        {
            auto current=songPrompt.getText().trim();
            const auto token=suggestionText[i];
            if(!current.containsIgnoreCase(token))
            {
                if(current.isNotEmpty()&&!current.endsWithChar(','))current+=",";
                if(current.isNotEmpty())current+=" ";
                current+=token;
                songPrompt.setText(current);
            }
        };
    }

    generateSound.onClick=[this]{p.generatePatch(soundPrompt.getText());};
    applyLaneSound.onClick=[this]{
        if(!p.setSelectedLaneSound(laneSoundPrompt.getText(),false))
            showStatus("Select a lane and describe its sound first");
    };
    autoLaneSound.onClick=[this]{
        if(!p.setSelectedLaneSound({},true))
            showStatus("Select a lane first");
    };
    generateTrack.onClick=[this]{const auto prompt=songPrompt.getText();runWork([this,prompt]{p.generateTrack(prompt);},[this]{bpm.setValue(p.getPreviewBpm(),juce::dontSendNotification);setTab(1);});};
    generateDrums.onClick=[this]{p.regenerateDrums(songPrompt.getText());p.setSelectedLane(0);setTab(2);};
    randomizeEverythingButton.onClick=[this]{const auto prompt=songPrompt.getText().trim().isEmpty()?juce::String("fresh modern EDM"):songPrompt.getText();runWork([this,prompt]{p.randomizeEverything(prompt);},[this]{bpm.setValue(p.getPreviewBpm(),juce::dontSendNotification);setTab(1);});};
    surpriseMe.onClick=[this]{const auto q=p.makeSurprisePrompt();songPrompt.setText(q);runWork([this,q]{p.randomizeEverything(q);},[this]{bpm.setValue(p.getPreviewBpm(),juce::dontSendNotification);setTab(1);});};
    similar.onClick=[this]{p.generateSimilarPatch();};mutate.onClick=[this]{p.mutatePatch();};randomize.onClick=[this]{p.randomizePatch();};undo.onClick=[this]{p.undoPatch();};redo.onClick=[this]{p.redoPatch();};
    variation1.onClick=[this]{p.generateVariation(1);};variation2.onClick=[this]{p.generateVariation(2);};variation3.onClick=[this]{p.generateVariation(3);};variation4.onClick=[this]{p.generateVariation(4);};
    captureA.onClick=[this]{p.captureA();};captureB.onClick=[this]{p.captureB();};recallA.onClick=[this]{p.recallA();};recallB.onClick=[this]{p.recallB();};
    previewSound.onClick=[this]{p.startPreview();};
    playSong.onClick=[this]{
        if(p.isSongPlaying())p.pauseSongPreview();
        else if(p.songPosition01()>0.0&&p.songPosition01()<.9999)p.resumeSongPreview();
        else p.startSongPreview();
    };
    playChorus.onClick=[this]{p.startChorusPreview();};
    playDrop.onClick=[this]{p.startDropPreview();};
    stop.onClick=[this]{if(busy){p.backgroundCancel.store(true);showStatus("Cancelling operation...");}else{p.stopPreview();p.stopSongPreview();}};bpm.onValueChange=[this]{p.setPreviewBpm(bpm.getValue());};
    connect.onClick=[this]{const auto packet=p.exportProjectForCyanoryx();if(packet.isNotEmpty())juce::SystemClipboard::copyTextToClipboard(packet);showStatus(packet.isNotEmpty()?"Cyanoryx protocol bundle copied to clipboard":"Cyanoryx bundle unavailable");};
    loadReference.onClick=[this]{chooseReferenceAudio();};importMidi.onClick=[this]{chooseMidiImport();};
    resound.onClick=[this]{if(!p.resoundReference(songPrompt.getText()))showStatus(p.generationStatus);};
    rebuildReference.onClick=[this]{
        if(p.rebuildInstrumentalFromReference(songPrompt.getText()))
        {
            bpm.setValue(p.getPreviewBpm(),juce::dontSendNotification);
            setTab(1);
            p.startSongPreview();
        }
        else showStatus(p.generationStatus);
    };
    saveSoundButton.onClick=[this]{chooseSaveSound();};loadSoundButton.onClick=[this]{chooseLoadSound();};saveProjectButton.onClick=[this]{chooseSaveProject();};loadProjectButton.onClick=[this]{chooseLoadProject();};exportMixButton.onClick=[this]{chooseExportMix();};exportStemsButton.onClick=[this]{chooseExportStems();};
    macroBrightness.onValueChange=[this]{p.setMacro(SonaraAudioProcessor::Macro::brightness,(float)macroBrightness.getValue());};macroMovement.onValueChange=[this]{p.setMacro(SonaraAudioProcessor::Macro::movement,(float)macroMovement.getValue());};macroSpace.onValueChange=[this]{p.setMacro(SonaraAudioProcessor::Macro::space,(float)macroSpace.getValue());};macroImpact.onValueChange=[this]{p.setMacro(SonaraAudioProcessor::Macro::impact,(float)macroImpact.getValue());};mixLevel.onValueChange=[this]{p.setLaneMix(p.getSelectedLane(),SonaraAudioProcessor::LaneMixParameter::level,(float)mixLevel.getValue());};
    mixPan.onValueChange=[this]{p.setLaneMix(p.getSelectedLane(),SonaraAudioProcessor::LaneMixParameter::pan,(float)mixPan.getValue());};
    mixWidth.onValueChange=[this]{p.setLaneMix(p.getSelectedLane(),SonaraAudioProcessor::LaneMixParameter::width,(float)mixWidth.getValue());};
    mixFx.onValueChange=[this]{p.setLaneMix(p.getSelectedLane(),SonaraAudioProcessor::LaneMixParameter::fxSend,(float)mixFx.getValue());};
    lockOsc.onClick=[this]{p.mutationLocks().oscillators=lockOsc.getToggleState();};lockUnison.onClick=[this]{p.mutationLocks().unison=lockUnison.getToggleState();};lockEnv.onClick=[this]{p.mutationLocks().ampEnvelope=lockEnv.getToggleState();};lockFilter.onClick=[this]{p.mutationLocks().filter=lockFilter.getToggleState();};lockMod.onClick=[this]{p.mutationLocks().modulation=lockMod.getToggleState();};lockSources.onClick=[this]{p.mutationLocks().sources=lockSources.getToggleState();};lockTone.onClick=[this]{p.mutationLocks().tone=lockTone.getToggleState();};lockFx.onClick=[this]{p.mutationLocks().spatialFx=lockFx.getToggleState();};

    const auto& d=p.currentPatch();macroBrightness.setValue(d.macroBrightness,juce::dontSendNotification);macroMovement.setValue(d.macroMovement,juce::dontSendNotification);macroSpace.setValue(d.macroSpace,juce::dontSendNotification);macroImpact.setValue(d.macroImpact,juce::dontSendNotification);syncLockButtons();setTab(1);startTimerHz(15);timerCallback();
}

SonaraAudioProcessorEditor::~SonaraAudioProcessorEditor(){stopTimer();if(busy)p.backgroundCancel.store(true);if(worker.joinable())worker.join();fileChooser.reset();setLookAndFeel(nullptr);}
void SonaraAudioProcessorEditor::styleButton(juce::Button& b,bool accent){b.setColour(juce::TextButton::buttonColourId,accent?juce::Colour(0xff39256f):juce::Colour(0xff101a2c));b.setColour(juce::TextButton::buttonOnColourId,purple);}
void SonaraAudioProcessorEditor::configureMacro(juce::Slider& s,const juce::String& name){s.setRange(0,1,.001);s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);s.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);s.setName(name);addAndMakeVisible(s);}
void SonaraAudioProcessorEditor::configureMixSlider(juce::Slider& s,double min,double max,const juce::String& suffix)
{
    s.setRange(min,max,.001);s.setSliderStyle(juce::Slider::LinearHorizontal);
    s.setTextBoxStyle(juce::Slider::TextBoxRight,false,72,22);s.setTextValueSuffix(suffix);
    s.setColour(juce::Slider::trackColourId,cyan.withAlpha(.72f));
    s.setColour(juce::Slider::backgroundColourId,juce::Colour(0xff0b1524));
    addAndMakeVisible(s);
}

void SonaraAudioProcessorEditor::syncMixControls()
{
    const int lane=p.getSelectedLane();
    const auto state=p.getLaneMix(lane);
    const bool enabled=lane>=0;
    mixLevel.setEnabled(enabled);mixPan.setEnabled(enabled&&lane!=5);
    mixWidth.setEnabled(lane>=4&&lane!=5);mixFx.setEnabled(enabled&&lane!=0&&lane!=4&&lane!=5);
    mixLevel.setValue(state.level,juce::dontSendNotification);
    mixPan.setValue(state.pan,juce::dontSendNotification);
    mixWidth.setValue(state.width,juce::dontSendNotification);
    mixFx.setValue(state.fxSend,juce::dontSendNotification);
}

void SonaraAudioProcessorEditor::syncLockButtons(){const auto& l=p.mutationLocks();lockOsc.setToggleState(l.oscillators,juce::dontSendNotification);lockUnison.setToggleState(l.unison,juce::dontSendNotification);lockEnv.setToggleState(l.ampEnvelope,juce::dontSendNotification);lockFilter.setToggleState(l.filter,juce::dontSendNotification);lockMod.setToggleState(l.modulation,juce::dontSendNotification);lockSources.setToggleState(l.sources,juce::dontSendNotification);lockTone.setToggleState(l.tone,juce::dontSendNotification);lockFx.setToggleState(l.spatialFx,juce::dontSendNotification);}
void SonaraAudioProcessorEditor::showStatus(const juce::String& s){p.generationStatus=s;statusLine.setText(s,juce::dontSendNotification);}

void SonaraAudioProcessorEditor::runWork(std::function<void()> work,std::function<void()> finished)
{
    if(busy||dragActive)return;
    if(worker.joinable())worker.join();
    busy=true;p.backgroundCancel.store(false);p.generationProgress.store(0.f);
    for(int i=0;i<getNumChildComponents();++i)getChildComponent(i)->setEnabled(getChildComponent(i)==&stop);
    stop.setButtonText("CANCEL");
    juce::Component::SafePointer<SonaraAudioProcessorEditor> self(this);
    worker=std::thread([this,self,work=std::move(work),finished=std::move(finished)]() mutable
    {
        try{work();}catch(const std::exception&){p.generationStatus="Operation failed • no output was published";p.generationProgress.store(0);}
        juce::MessageManager::callAsync([self,finished=std::move(finished)]() mutable
        {
            if(!self)return;
            self->busy=false;self->p.backgroundCancel.store(false);
            for(int i=0;i<self->getNumChildComponents();++i)self->getChildComponent(i)->setEnabled(true);
            self->stop.setButtonText("STOP");self->updateModeVisibility();
            if(finished)finished();self->timerCallback();
        });
    });
}

juce::String SonaraAudioProcessorEditor::dragCacheKey(ExternalDragButton::Kind kind) const
{
    auto song=p.arrangementSnapshot();
    juce::String key=juce::String((int)kind)+":"+juce::String::toHexString((juce::int64)(uintptr_t)song.get())
        +":"+juce::String(p.getSelectedLane())+":"+juce::String(p.currentPatch().seed)+":"+juce::String(p.getPreviewBpm(),4)+":"+juce::String(p.getPreparedSampleRate(),1);
    for(int i=0;i<12;++i){const auto m=p.getLaneMix(i);key+=":"+juce::String(m.level,6)+":"+juce::String(m.pan,6)+":"+juce::String(m.width,6)+":"+juce::String(m.fxSend,6);}
    return key;
}

void SonaraAudioProcessorEditor::beginExternalDrag(ExternalDragButton::Kind kind)
{
    if(busy||dragActive)return;
    const bool audio=kind==ExternalDragButton::Kind::referenceAudio||kind==ExternalDragButton::Kind::fullMixAudio
        ||kind==ExternalDragButton::Kind::laneAudio||kind==ExternalDragButton::Kind::leadAudio||kind==ExternalDragButton::Kind::stemsAudio;
    if(!audio){launchFileDrag(prepareDragFiles(kind));return;}
    const auto key=dragCacheKey(kind);auto it=dragCache.find((int)kind);
    if(it!=dragCache.end()&&it->second.key==key&&!it->second.files.isEmpty())
    {
        bool exists=true;for(const auto& file:it->second.files)exists=exists&&juce::File(file).existsAsFile();
        if(exists){launchFileDrag(it->second.files);return;}
    }
    auto files=std::make_shared<juce::StringArray>();
    showStatus("Preparing audio transfer • progress below • drag again when ready");
    runWork([this,kind,files]{*files=prepareDragFiles(kind);},[this,kind,key,files]
    {
        if(files->isEmpty()){showStatus("Audio transfer failed or cancelled");return;}
        dragCache[(int)kind]={key,*files};showStatus("AUDIO READY • drag the same WAV/stems button into FL Studio");
    });
}

void SonaraAudioProcessorEditor::launchFileDrag(const juce::StringArray& files)
{
    if(files.isEmpty())return;
    dragActive=true;showStatus("DRAGGING • release over FL Studio's Piano Roll or Playlist");repaint();
    juce::Component::SafePointer<SonaraAudioProcessorEditor> self(this);
    const bool started=juce::DragAndDropContainer::performExternalDragDropOfFiles(files,false,this,[self]
    {if(self){self->dragActive=false;self->showStatus("Drag ended • check the target's imported content");self->repaint();}});
    if(!started){dragActive=false;showStatus("Drag could not start • use MIDI/WAV export controls");repaint();}
}

void SonaraAudioProcessorEditor::chooseExportMidi(bool selected)
{
    fileChooser=std::make_unique<juce::FileChooser>(selected?"Export selected lane MIDI":"Export full arrangement MIDI",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(selected?"SONARA-Selected.mid":"SONARA-Full.mid"),"*.mid");
    juce::Component::SafePointer<SonaraAudioProcessorEditor> self(this);
    fileChooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::warnAboutOverwriting,[self,selected](const juce::FileChooser& chooser)
    {
        if(!self)return;auto f=chooser.getResult();if(f==juce::File{})return;
        if(f.getFileExtension().isEmpty())f=f.withFileExtension(".mid");
        const bool ok=selected?self->p.writeSelectedLaneMidiFile(f):self->p.writeArrangementMidiFile(f);
        self->showStatus(ok?"MIDI exported • notes/timing only; use WAV for SONARA audio":"MIDI export failed");
    });
}

juce::StringArray SonaraAudioProcessorEditor::prepareDragFiles(ExternalDragButton::Kind kind)
{
    const auto temp=juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("SONARA/Transfers");
    if(!temp.createDirectory())return {};
    bool ok=false;
    juce::String status;
    juce::StringArray files;

    if(kind==ExternalDragButton::Kind::previewMidi)
    {
        p.generationStatus=juce::String("Preparing SOUND MIDI...");
        dragFile=temp.getNonexistentChildFile("SONARA-Sound",".mid");
        ok=p.writePreviewMidiFile(dragFile);
        status="SOUND MIDI ready • drop on SONARA for the same SoundDNA";
    }
    else if(kind==ExternalDragButton::Kind::fullMidi)
    {
        p.generationStatus=juce::String("Preparing FULL SONG MIDI...");
        dragFile=temp.getNonexistentChildFile("SONARA-Full-Arrangement",".mid");
        ok=p.writeArrangementMidiFile(dragFile);
        status="FULL SONG MIDI ready • separate generated tracks + complete timing";
    }
    else if(kind==ExternalDragButton::Kind::laneMidi)
    {
        p.generationStatus=juce::String("Preparing SELECTED MIDI...");
        dragFile=temp.getNonexistentChildFile("SONARA-Selected-Lane",".mid");
        ok=p.writeSelectedLaneMidiFile(dragFile);
        status="SELECTED MIDI ready • current lane only";
    }
    else if(kind==ExternalDragButton::Kind::leadMidi)
    {
        p.generationStatus=juce::String("Preparing LEAD MIDI...");
        dragFile=temp.getNonexistentChildFile("SONARA-Lead",".mid");
        ok=p.writeLeadMidiFile(dragFile);
        status="LEAD MIDI ready • main melody only";
    }
    else if(kind==ExternalDragButton::Kind::referenceMidi)
    {
        p.generationStatus=juce::String("Preparing REFERENCE MIDI...");
        dragFile=temp.getNonexistentChildFile("SONARA-Reference-Melody",".mid");
        ok=p.writeReferenceMidiFile(dragFile);
        status="REFERENCE MIDI ready • extracted melody";
    }
    else if(kind==ExternalDragButton::Kind::referenceAudio)
    {
        p.generationStatus=juce::String("Rendering RESOUND WAV...");
        dragFile=temp.getNonexistentChildFile("SONARA-Resound",".wav");
        ok=p.exportReferenceAudio(dragFile);
        status="RESOUND WAV ready • extracted melody + current SoundDNA";
    }
    else if(kind==ExternalDragButton::Kind::fullMixAudio)
    {
        p.generationStatus=juce::String("Rendering FULL MIX WAV...");
        dragFile=temp.getNonexistentChildFile("SONARA-Full-Mix",".wav");
        ok=p.exportFullMix(dragFile);
        status="FULL MIX WAV ready • preview-matched 24-bit audio";
    }
    else if(kind==ExternalDragButton::Kind::laneAudio)
    {
        p.generationStatus=juce::String("Rendering SELECTED WAV...");
        dragFile=temp.getNonexistentChildFile("SONARA-Selected-Lane",".wav");
        ok=p.exportSelectedLaneAudio(dragFile);
        status="SELECTED WAV ready • current lane SoundDNA preserved";
    }
    else if(kind==ExternalDragButton::Kind::leadAudio)
    {
        p.generationStatus=juce::String("Rendering LEAD WAV...");
        dragFile=temp.getNonexistentChildFile("SONARA-Lead",".wav");
        ok=p.exportLeadAudio(dragFile);
        status="LEAD WAV ready • LEAD SoundDNA preserved";
    }
    else
    {
        p.generationStatus=juce::String("Rendering STEMS...");
        dragFile=temp.getNonexistentChildFile("SONARA-Stems","");
        ok=dragFile.createDirectory()&&p.exportAllStems(dragFile);
        status="STEMS ready • rendered 24-bit lanes";
        if(ok)
        {
            juce::Array<juce::File> wavs;
            dragFile.findChildFiles(wavs,juce::File::findFiles,false,"*.wav");
            for(const auto& f:wavs)files.add(f.getFullPathName());
            ok=!files.isEmpty();
        }
    }

    if(ok&&kind!=ExternalDragButton::Kind::stemsAudio)
        files.add(dragFile.getFullPathName());

    if(!ok||files.isEmpty())
    {
        p.generationStatus=juce::String("Export unavailable • required generated/reference content is missing");
        return {};
    }
    return files;
}

void SonaraAudioProcessorEditor::chooseReferenceAudio()
{
    fileChooser=std::make_unique<juce::FileChooser>("Load reference audio",juce::File{},"*.wav;*.aiff;*.aif;*.mp3");
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[this](const juce::FileChooser& c)
    {
        const auto f=c.getResult();
        if(f.existsAsFile())
        {
            if(p.analyseReferenceFile(f))
            {
                bpm.setValue(p.getPreviewBpm(),juce::dontSendNotification);
                referenceSummary.setText(p.getReferenceSummary(),juce::dontSendNotification);
            }
            else
            {
                referenceSummary.setText(p.generationStatus,juce::dontSendNotification);
                showStatus(p.generationStatus);
            }
        }
        fileChooser.reset();
    });
}
void SonaraAudioProcessorEditor::chooseMidiImport()
{
    fileChooser=std::make_unique<juce::FileChooser>("Import MIDI",juce::File{},"*.mid;*.midi");
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[this](const juce::FileChooser& c)
    {
        const auto f=c.getResult();
        if(f.existsAsFile())
        {
            if(p.importMidiFile(f))
            {
                bpm.setValue(p.getPreviewBpm(),juce::dontSendNotification);
                referenceSummary.setText(p.getReferenceSummary(),juce::dontSendNotification);
            }
            else
            {
                referenceSummary.setText(p.generationStatus,juce::dontSendNotification);
                showStatus(p.generationStatus);
            }
        }
        fileChooser.reset();
    });
}
void SonaraAudioProcessorEditor::chooseSaveSound(){fileChooser=std::make_unique<juce::FileChooser>("Save SoundDNA",juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("SONARA-Sound.sonara"),"*.sonara");fileChooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::warnAboutOverwriting,[this](const juce::FileChooser& c){auto f=c.getResult();if(f!=juce::File{}){if(f.getFileExtension().isEmpty())f=f.withFileExtension(".sonara");showStatus(p.saveSound(f)?"SoundDNA saved":"SoundDNA save failed");}fileChooser.reset();});}
void SonaraAudioProcessorEditor::chooseLoadSound(){fileChooser=std::make_unique<juce::FileChooser>("Load SoundDNA",juce::File{},"*.sonara");fileChooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[this](const juce::FileChooser& c){const auto f=c.getResult();if(f.existsAsFile()){const bool ok=p.loadSound(f);if(ok&&p.currentPatch().sourcePrompt.isNotEmpty())soundPrompt.setText(p.currentPatch().sourcePrompt);showStatus(ok?"SoundDNA loaded":"SoundDNA load failed");}fileChooser.reset();});}
void SonaraAudioProcessorEditor::chooseSaveProject(){fileChooser=std::make_unique<juce::FileChooser>("Save SONARA project",juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("SONARA-Project.sonaraproject"),"*.sonaraproject");fileChooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::warnAboutOverwriting,[this](const juce::FileChooser& c){auto f=c.getResult();if(f!=juce::File{}){if(f.getFileExtension().isEmpty())f=f.withFileExtension(".sonaraproject");showStatus(p.saveProject(f)?"Project saved":"Project save failed");}fileChooser.reset();});}
void SonaraAudioProcessorEditor::chooseLoadProject(){fileChooser=std::make_unique<juce::FileChooser>("Load SONARA project",juce::File{},"*.sonaraproject");fileChooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[this](const juce::FileChooser& c){const auto f=c.getResult();if(f.existsAsFile()&&p.loadProject(f)){bpm.setValue(p.getPreviewBpm(),juce::dontSendNotification);const auto song=p.getCurrentSongPrompt();if(song.isNotEmpty())songPrompt.setText(song);const auto sound=p.currentPatch().sourcePrompt;if(sound.isNotEmpty())soundPrompt.setText(sound);syncLockButtons();setTab(p.arrangementSnapshot()?1:0);}else if(f.existsAsFile())showStatus("Project load failed");fileChooser.reset();});}
void SonaraAudioProcessorEditor::chooseExportMix(){fileChooser=std::make_unique<juce::FileChooser>("Export 24-bit full mix",juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("SONARA-Full-Mix.wav"),"*.wav");fileChooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::warnAboutOverwriting,[this](const juce::FileChooser& c){auto f=c.getResult();if(f!=juce::File{}){if(f.getFileExtension().isEmpty())f=f.withFileExtension(".wav");runWork([this,f]{p.exportFullMix(f);});}fileChooser.reset();});}
void SonaraAudioProcessorEditor::chooseExportStems(){fileChooser=std::make_unique<juce::FileChooser>("Choose folder for SONARA stems",juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),"*");fileChooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectDirectories,[this](const juce::FileChooser& c){const auto d=c.getResult();if(d.isDirectory())runWork([this,d]{p.exportAllStems(d);});fileChooser.reset();});}

void SonaraAudioProcessorEditor::setTab(int index)
{
    activeTab=juce::jlimit(0,6,index);
    juce::TextButton* tabs[]={&tabInstrument,&tabSong,&tabDrums,&tabFx,&tabReference,&tabMidi,&tabExport};
    for(int i=0;i<7;++i)
    {
        tabs[i]->setToggleState(i==activeTab,juce::dontSendNotification);
        tabs[i]->setColour(juce::TextButton::buttonColourId,i==activeTab?juce::Colour(0xff2d205a):juce::Colour(0xff0e1728));
    }

    static const char* soundNames[]={"Progressive Lead","Future Rave","Warm Pluck","Deep Reese","Dream Pad","Tech House Bass","Cinematic Bell","Experimental"};
    static const char* songNames[]={"Progressive House","Future Rave","Melodic EDM","Tech House","Drum & Bass","Electro Pop","Cinematic EDM","Experimental"};
    for(int i=0;i<8;++i) presets[(size_t)i].setButtonText(activeTab==0?soundNames[i]:songNames[i]);

    updateModeVisibility();

    // Tab-specific controls have different button rows. Re-run the responsive layout
    // whenever the active tab changes so newly-visible controls receive real bounds
    // instead of remaining at their default 0x0 position.
    resized();
    repaint();
}
void SonaraAudioProcessorEditor::updateModeVisibility(){
    const bool instrumentTab=activeTab==0;
    const bool songTab=activeTab==1;
    const bool drumsTab=activeTab==2;
    const bool mixTab=activeTab==3;
    const bool referenceTab=activeTab==4;
    const bool midiTab=activeTab==5;
    const bool exportTab=activeTab==6;
    const bool arrangementTab=songTab||drumsTab||mixTab||midiTab||exportTab;

    seedInput.setVisible(songTab);reproduceSeed.setVisible(songTab);
    timeline.setVisible(arrangementTab);
    pianoRoll.setVisible(songTab||drumsTab||midiTab);
    soundView.setVisible(instrumentTab);
    referenceSummary.setVisible(referenceTab);
    soundPrompt.setVisible(instrumentTab);
    songPrompt.setVisible(!instrumentTab&&!mixTab);
    laneSoundPrompt.setVisible(mixTab);
    for(auto& b:promptSuggestions)b.setVisible(songTab);

    exportFullMidiButton.setVisible(midiTab||exportTab);exportLaneMidiButton.setVisible(midiTab||exportTab);
    generateSound.setVisible(instrumentTab);
    similar.setVisible(instrumentTab);
    mutate.setVisible(instrumentTab);
    randomize.setVisible(instrumentTab);
    undo.setVisible(instrumentTab);
    redo.setVisible(instrumentTab);
    generateTrack.setVisible(songTab);
    randomizeEverythingButton.setVisible(songTab);
    surpriseMe.setVisible(songTab);
    generateDrums.setVisible(drumsTab);
    applyLaneSound.setVisible(mixTab);
    autoLaneSound.setVisible(mixTab);

    for(auto* b:std::array<juce::Component*,4>{&loadReference,&importMidi,&resound,&rebuildReference})b->setVisible(referenceTab);
    for(auto* b:std::array<juce::Component*,6>{&saveSoundButton,&loadSoundButton,&saveProjectButton,&loadProjectButton,&exportMixButton,&exportStemsButton})b->setVisible(exportTab);

    for(auto* b:std::array<juce::Component*,8>{&variation1,&variation2,&variation3,&variation4,&captureA,&captureB,&recallA,&recallB})b->setVisible(instrumentTab);
    for(auto* b:std::array<juce::Component*,8>{&lockOsc,&lockUnison,&lockEnv,&lockFilter,&lockMod,&lockSources,&lockTone,&lockFx})b->setVisible(instrumentTab);
    for(auto* s:std::array<juce::Component*,4>{&macroBrightness,&macroMovement,&macroSpace,&macroImpact})s->setVisible(instrumentTab);
    for(auto* s:std::array<juce::Component*,4>{&mixLevel,&mixPan,&mixWidth,&mixFx})s->setVisible(mixTab);
    if(mixTab)syncMixControls();
    patchName.setVisible(instrumentTab);
    selectedLaneLabel.setVisible(arrangementTab);

    dragPreviewMidi.setVisible(instrumentTab);
    dragFullMidi.setVisible(songTab||midiTab||exportTab);
    dragLaneMidi.setVisible(songTab||drumsTab||midiTab||exportTab);
    dragLeadMidi.setVisible(midiTab||exportTab);
    dragReferenceMidi.setVisible(referenceTab||midiTab||exportTab);
    dragReferenceAudio.setVisible(referenceTab||exportTab);
    dragFullAudio.setVisible(songTab||exportTab);
    dragLaneAudio.setVisible(songTab||drumsTab||exportTab);
    dragLeadAudio.setVisible(exportTab);
    dragStems.setVisible(exportTab);

    previewSound.setVisible(instrumentTab||referenceTab);
    playSong.setVisible(songTab||drumsTab||mixTab||referenceTab||midiTab||exportTab);
    playChorus.setVisible(songTab||drumsTab||mixTab||midiTab||exportTab);
    playDrop.setVisible(songTab||drumsTab||mixTab||midiTab||exportTab);
    stop.setVisible(true);

    if(referenceTab)referenceSummary.setText(p.hasReference()?p.getReferenceSummary():"LOAD AUDIO or IMPORT MIDI\n\nSONARA analyzes tempo, key and a dominant instrumental melody into editable note data. RESOUND plays that melody with new generated SoundDNA. REBUILD creates new drums, bass, chords, synths and arrangement around the extracted melody without copying the reference audio.",juce::dontSendNotification);
}

void SonaraAudioProcessorEditor::timerCallback()
{
    if(busy){statusLine.setText(juce::String(p.generationStatus),juce::dontSendNotification);playbackProgress=p.generationProgress.load();playbackBar.repaint();repaint();return;}
    playbackProgress=p.isSongPlaying()?p.songPosition01():p.previewPosition01();
    pulse+=.045f;if(pulse>juce::MathConstants<float>::twoPi)pulse=0.f;
    soundView.animation=pulse;

    patchName.setText(p.currentPatch().name,juce::dontSendNotification);
    statusLine.setText(p.generationStatus,juce::dontSendNotification);
    connect.setButtonText("COPY CYANORYX BUNDLE");
    const bool referenceReady=p.hasReference();
    resound.setEnabled(referenceReady);
    rebuildReference.setEnabled(referenceReady);
    playSong.setButtonText(p.isSongPlaying()?"PAUSE":(p.songPosition01()>0.0&&p.songPosition01()<.9999?"RESUME":"PLAY SONG"));
    previewSound.setButtonText(p.isPreviewPlaying()?"PREVIEWING":"PREVIEW SOUND");

    auto a=p.arrangementSnapshot();
    const bool hasArrangement=a&&!a->isEmpty();
    bpm.setEnabled(activeTab==0||!hasArrangement);if(activeTab!=0&&hasArrangement)bpm.setValue(a->getBpm(),juce::dontSendNotification);
    const auto seed=p.getSongGenerationSeed();if((!seedInitialised||seed!=displayedSeed)&&!seedInput.hasKeyboardFocus(true)){seedInitialised=true;displayedSeed=seed;seedInput.setText(hasArrangement?juce::String::toHexString((juce::int64)seed).paddedLeft('0',16):juce::String(),false);}
    const int selected=p.getSelectedLane();
    const bool selectedReady=hasArrangement&&juce::isPositiveAndBelow(selected,(int)a->getLanes().size())
                          &&!a->getLanes()[(size_t)selected].notes.empty();
    bool leadReady=false;
    if(hasArrangement)
        for(const auto& lane:a->getLanes())
            if(lane.name=="LEAD"&&!lane.notes.empty()){leadReady=true;break;}

    dragFullMidi.setEnabled(hasArrangement);
    dragFullAudio.setEnabled(hasArrangement);
    dragStems.setEnabled(hasArrangement);
    exportFullMidiButton.setEnabled(hasArrangement);exportLaneMidiButton.setEnabled(selectedReady);
    exportMixButton.setEnabled(hasArrangement);
    exportStemsButton.setEnabled(hasArrangement);
    dragLaneMidi.setEnabled(selectedReady);
    dragLaneAudio.setEnabled(selectedReady);
    dragLeadMidi.setEnabled(leadReady);
    dragLeadAudio.setEnabled(leadReady);
    playSong.setEnabled(hasArrangement);
    playChorus.setEnabled(hasArrangement);
    playDrop.setEnabled(hasArrangement);
    dragReferenceMidi.setEnabled(referenceReady);
    dragReferenceAudio.setEnabled(referenceReady);

    selectedLaneLabel.setText(selectedReady
        ?"SELECTED • "+a->getLanes()[(size_t)selected].name
        :(hasArrangement?"SELECTED • lane has no events":"SELECTED • generate a song first"),
        juce::dontSendNotification);

    if(activeTab==3)syncMixControls();
    if(activeTab==4&&p.hasReference())referenceSummary.setText(p.getReferenceSummary(),juce::dontSendNotification);

    if(activeTab==0)soundView.repaint();
    else if(activeTab==1||activeTab==2||activeTab==5||activeTab==6)timeline.repaint();
    if(activeTab==1||activeTab==2||activeTab==5)pianoRoll.repaint();
    repaint();
}

void SonaraAudioProcessorEditor::paint(juce::Graphics& g)
{
    auto r=getLocalBounds().toFloat();
    juce::ColourGradient b(bg0,0,0,bg1,r.getWidth(),r.getHeight(),false);
    g.setGradientFill(b);g.fillAll();
    if(dragActive){g.setColour(cyan);g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(3),14.f,4.f);}

    g.setColour(purple.withAlpha(.07f+.025f*std::sin(pulse)));
    g.fillEllipse(r.getCentreX()-360,r.getCentreY()-330,720,660);
    g.setColour(cyan.withAlpha(.035f));
    g.fillEllipse(r.getRight()-430,40,380,380);

    g.setColour(text);g.setFont(juce::FontOptions(32.f).withStyle("Bold"));
    g.drawText("SONARA",30,18,220,38,juce::Justification::left);
    g.setColour(cyan);g.setFont(11.f);
    g.drawText("AI MUSIC PRODUCER | SOUND + SONG + DRUMS",32,52,650,20,juce::Justification::left);

    const auto left=juce::Rectangle<float>(22,118,210,(float)getHeight()-195);
    const auto right=juce::Rectangle<float>((float)getWidth()-244,118,222,(float)getHeight()-195);
    g.setColour(panel.withAlpha(.96f));g.fillRoundedRectangle(left,12);g.fillRoundedRectangle(right,12);
    g.setColour(juce::Colour(0xff24324a));g.drawRoundedRectangle(left,12,1);g.drawRoundedRectangle(right,12,1);

    const bool instrumentTab=activeTab==0;
    g.setColour(dim);g.setFont(10.f);
    g.drawText(instrumentTab?"SOUND PRESETS":"SONG / STYLE PRESETS",38,134,165,18,juce::Justification::left);

    juce::String rightTitle="SONG INSPECTOR";
    if(activeTab==0) rightTitle="PERFORMANCE / SOUND DNA";
    else if(activeTab==2) rightTitle="DRUM GENERATOR";
    else if(activeTab==3) rightTitle="FX / MIX INSPECTOR";
    else if(activeTab==4) rightTitle="REFERENCE ANALYSIS";
    else if(activeTab==5) rightTitle="MIDI INSPECTOR";
    else if(activeTab==6) rightTitle="EXPORT STATUS";
    g.drawText(rightTitle,getWidth()-226,134,190,18,juce::Justification::left);

    g.setColour(juce::Colour(0xff17243a));
    g.fillRoundedRectangle(252,118,(float)getWidth()-516,(float)getHeight()-195,12);
    g.setColour(juce::Colour(0xff263650));
    g.drawRoundedRectangle(252,118,(float)getWidth()-516,(float)getHeight()-195,12,1);

    g.setColour(cyan.withAlpha(.72f));g.setFont(9.f);
    const juce::String promptTitle=instrumentTab?"SOUND PROMPT • SOUND DESIGN ONLY":
        (activeTab==3?"SELECTED LANE SOUND PROMPT • OVERRIDES ONLY THIS INSTRUMENT":
                      "SONG / ARRANGEMENT PROMPT • COMPOSITION + GROOVE + STRUCTURE");
    g.drawText(promptTitle,270,120,getWidth()-570,15,juce::Justification::left);

    if(instrumentTab)
    {
        g.setColour(text.withAlpha(.75f));g.setFont(10.f);
        g.drawText("MUTATION LOCKS",35,getHeight()-265,170,16,juce::Justification::left);
    }

    if(activeTab==4)
    {
        g.setColour(cyan.withAlpha(.75f));
        g.drawText("REFERENCE / RESOUND",270,238,220,18,juce::Justification::left);
    }
    if(activeTab==1)
    {
        g.setColour(cyan.withAlpha(.70f));g.setFont(8.5f);
        g.drawText("PROMPT DIRECTIONS • CLICK TO ADD",270,225,getWidth()-570,14,juce::Justification::left);
    }
    if(activeTab==6)
    {
        g.setColour(cyan.withAlpha(.75f));
        g.drawText("24-BIT AUDIO + STEM EXPORT",270,238,260,18,juce::Justification::left);
    }

    auto a=p.arrangementSnapshot();
    const int rx=getWidth()-226;
    int ry=168;

    if(!instrumentTab && a)
    {
        static const char* noteNames[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
        const int pc=((a->getRootMidi()%12)+12)%12;
        const juce::String key=juce::String(noteNames[pc])+(a->isMinor()?" minor":" major");
        int noteCount=0;
        for(const auto& lane:a->getLanes()) noteCount+=(int)lane.notes.size();

        juce::String seedHex=juce::String::toHexString((juce::int64)p.getSongGenerationSeed()).toUpperCase();

        auto shortHex=[](uint64_t value)
        {
            auto result=juce::String::toHexString((juce::int64)value).toUpperCase();
            return result.length()>8?result.substring(result.length()-8):result;
        };

        const auto lower=songPrompt.getText().toLowerCase();
        juce::String style="EDM";
        if(lower.contains("progressive house")) style="PROGRESSIVE HOUSE";
        else if(lower.contains("future rave")) style="FUTURE RAVE";
        else if(lower.contains("tech house")) style="TECH HOUSE";
        else if(lower.contains("drum and bass")||lower.contains("dnb")) style="DRUM & BASS";
        else if(lower.contains("electro pop")||lower.contains("pop")) style="ELECTRO POP";
        else if(lower.contains("cinematic")) style="CINEMATIC EDM";
        const auto card=[&](const juce::String& label,const juce::String& value,int y)
        {
            const auto box=juce::Rectangle<float>((float)rx, (float)y, 186.f, 40.f);
            g.setColour(juce::Colour(0xff0b1524));g.fillRoundedRectangle(box,7.f);
            g.setColour(juce::Colour(0xff263650));g.drawRoundedRectangle(box,7.f,1.f);
            g.setColour(dim);g.setFont(8.5f);g.drawText(label,rx+10,y+6,166,12,juce::Justification::left);
            g.setColour(text);g.setFont(11.f);g.drawFittedText(value,rx+10,y+20,166,17,juce::Justification::left,1);
        };

        if(activeTab==1)
        {
            card("STYLE",style,ry);ry+=44;
            card("KEY / TEMPO",key+" • "+juce::String(a->getBpm(),0)+" BPM",ry);ry+=44;
            g.setColour(dim);g.setFont(9.f);g.drawText("SEED • HEXADECIMAL",rx,ry,186,16,juce::Justification::left);
            ry+=82;
            card("PRODUCER PLAN",a->getPromptIntentSummary(),ry);ry+=44;
            card("HARMONY",a->getHarmonySummary(),ry);ry+=44;
            card("MELODY",a->getMelodyArchetypeName(),ry);ry+=44;
            card("CURRENT SECTION",p.currentSectionName()+" • "+juce::String(p.currentSongBar()+1)+"/"+juce::String(a->getBars()),ry);ry+=44;
            const int selected=p.getSelectedLane();
            if(juce::isPositiveAndBelow(selected,(int)a->getLanes().size()))
            {
                const auto& lane=a->getLanes()[(size_t)selected];
                card("SOUND DNA",lane.name+" • U"+juce::String(lane.sound.unison)+" • "+juce::String((int)lane.sound.cutoff)+" Hz",ry);ry+=44;
            }
            g.setColour(dim);g.setFont(9.f);g.drawFittedText("MIDI carries notes and timing. Use WAV/stems for exact SONARA sound.",rx,ry,186,40,juce::Justification::topLeft,3);
        }
        else if(activeTab==2)
        {
            int drumNotes=0;
            for(int i=0;i<juce::jmin(4,(int)a->getLanes().size());++i) drumNotes+=(int)a->getLanes()[(size_t)i].notes.size();
            card("DRUM PROMPT STYLE",style,ry);ry+=52;
            card("CURRENT DRUM EVENTS",juce::String(drumNotes)+" hits",ry);ry+=52;
            card("MODE","REGENERATE DRUMS ONLY",ry);ry+=58;
            g.setColour(cyan);g.setFont(9.f);
            g.drawFittedText("Melody, chords and bass stay intact when GENERATE DRUMS is used.",
                             rx,ry,186,54,juce::Justification::topLeft,4);
        }
        else if(activeTab==3)
        {
            const int laneIndex=p.getSelectedLane();
            if(juce::isPositiveAndBelow(laneIndex,(int)a->getLanes().size()))
            {
                const auto& lane=a->getLanes()[(size_t)laneIndex];
                const auto mix=p.getLaneMix(laneIndex);
                card("SELECTED LANE",lane.name,ry);ry+=44;
                card("MIDI EVENTS",juce::String((int)lane.notes.size()),ry);ry+=44;
                card("LEVEL / PAN",juce::String(mix.level,2)+" / "+juce::String(mix.pan,2),ry);ry+=44;
                card("WIDTH / FX SEND",juce::String(mix.width,2)+" / "+juce::String(mix.fxSend,2),ry);ry+=44;
                card("SOUND DNA",lane.sound.name,ry);
            }
        }
        else if(activeTab==5)
        {
            card("ARRANGEMENT",juce::String((int)a->getLanes().size())+" editable MIDI lanes",ry);ry+=52;
            card("SONG LENGTH",juce::String(a->getBars())+" bars",ry);ry+=52;
            card("EXPORT","FULL MIDI + SELECTED MIDI + REF MIDI",ry);
        }
        else if(activeTab==6)
        {
            card("FULL MIX","24-BIT WAV",ry);ry+=52;
            card("STEMS","24-BIT LANE WAVS",ry);ry+=52;
            card("EDITABLE","FULL + SELECTED MIDI",ry);
        }
    }

    const float progress=juce::jlimit(0.f,1.f,p.generationProgress.load());
    const auto prog=juce::Rectangle<float>(30,(float)getHeight()-40,(float)getWidth()-60,5);
    g.setColour(juce::Colour(0xff16243a));g.fillRoundedRectangle(prog,3);
    g.setColour(cyan);g.fillRoundedRectangle(prog.withWidth(prog.getWidth()*progress),3);
}

void SonaraAudioProcessorEditor::resized()
{
    const int w=getWidth(),h=getHeight();
    const int tabY=82,gap=5,tabW=(w-60-6*gap)/7;
    juce::TextButton* tabs[]={&tabInstrument,&tabSong,&tabDrums,&tabFx,&tabReference,&tabMidi,&tabExport};
    for(int i=0;i<7;++i)tabs[i]->setBounds(30+i*(tabW+gap),tabY,tabW,30);
    connect.setBounds(w-250,24,220,34);

    int py=160;
    for(auto& b:presets){b.setBounds(35,py,184,31);py+=36;}

    const int lockY=h-240;
    juce::ToggleButton* locks1[]={&lockOsc,&lockUnison,&lockEnv,&lockFilter};
    juce::ToggleButton* locks2[]={&lockMod,&lockSources,&lockTone,&lockFx};
    for(int i=0;i<4;++i)
    {
        locks1[i]->setBounds(34+(i%2)*94,lockY+(i/2)*30,90,27);
        locks2[i]->setBounds(34+(i%2)*94,lockY+64+(i/2)*30,90,27);
    }

    const int cx=270,cw=w-550;
    soundPrompt.setBounds(cx,138,cw,36);
    songPrompt.setBounds(cx,138,cw,36);
    laneSoundPrompt.setBounds(cx,138,cw,36);

    auto layoutRow=[&](std::initializer_list<juce::Component*> items,int y,int height=38)
    {
        const int n=(int)items.size();
        if(n<=0)return;
        const int g=6,cell=(cw-g*(n-1))/n;
        int x=cx;
        for(auto* item:items){item->setBounds(x,y,cell,height);x+=cell+g;}
    };

    switch(activeTab)
    {
        case 0: layoutRow({&generateSound,&similar,&mutate,&randomize,&undo,&redo},184); break;
        case 1: layoutRow({&generateTrack,&randomizeEverythingButton,&surpriseMe},184); break;
        case 2: layoutRow({&generateDrums},184); break;
        case 3: layoutRow({&applyLaneSound,&autoLaneSound},184); break;
        case 4: layoutRow({&loadReference,&importMidi,&resound,&rebuildReference},184); break;
        case 6: layoutRow({&saveSoundButton,&loadSoundButton,&saveProjectButton,&loadProjectButton,&exportMixButton,&exportStemsButton},184); break;
        case 5: layoutRow({&exportFullMidiButton,&exportLaneMidiButton},184);break;
        default: break;
    }
    if(activeTab==6)layoutRow({&exportFullMidiButton,&exportLaneMidiButton},228,30);

    if(activeTab==1)
    {
        const int sx=cx,sg=5;
        const int cell=(cw-sg*4)/5;
        for(int i=0;i<10;++i)
        {
            const int row=i/5,col=i%5;
            promptSuggestions[(size_t)i].setBounds(sx+col*(cell+sg),242+row*28,cell,24);
        }
    }

    const int upperY=activeTab==1?300:(activeTab==6?268:232);
    const int upperH=activeTab==1?juce::jlimit(164,280,h-upperY-272):
        (activeTab==3?juce::jlimit(164,280,h-upperY-302):juce::jlimit(164,280,h-upperY-206));
    soundView.setBounds(cx,upperY,cw,upperH);
    timeline.setBounds(cx,upperY,cw,upperH);
    referenceSummary.setBounds(cx+28,upperY+42,cw-56,upperH-64);

    const int pianoY=upperY+upperH+10;
    const int bottomControlsY=h-88;
    if(activeTab==3)
    {
        const int my=pianoY+18;
        mixLevel.setBounds(cx+18,my,cw-36,38);
        mixPan.setBounds(cx+18,my+48,cw-36,38);
        mixWidth.setBounds(cx+18,my+96,cw-36,38);
        mixFx.setBounds(cx+18,my+144,cw-36,38);
    }
    const int pianoH=juce::jmax(40,h-164-pianoY);
    pianoRoll.setBounds(cx,pianoY,cw,pianoH);

    patchName.setBounds(cx+18,upperY+8,cw-36,22);
    selectedLaneLabel.setBounds(cx,activeTab==3?pianoY-8:h-(activeTab==6?194:158),cw,20);
    seedInput.setBounds(w-226,270,186,26);reproduceSeed.setBounds(w-226,301,186,30);

    switch(activeTab)
    {
        case 0: layoutRow({&dragPreviewMidi},h-130,34); break;
        case 1: layoutRow({&dragFullMidi,&dragLaneMidi,&dragFullAudio,&dragLaneAudio},h-130,34); break;
        case 2: layoutRow({&dragLaneMidi,&dragLaneAudio},h-130,34); break;
        case 4: layoutRow({&dragReferenceMidi,&dragReferenceAudio},h-130,34); break;
        case 5: layoutRow({&dragFullMidi,&dragLaneMidi,&dragLeadMidi,&dragReferenceMidi},h-130,34); break;
        case 6:
            layoutRow({&dragFullMidi,&dragLaneMidi,&dragLeadMidi,&dragReferenceMidi},h-168,32);
            layoutRow({&dragFullAudio,&dragLaneAudio,&dragLeadAudio,&dragReferenceAudio,&dragStems},h-130,32);
            break;
        default: break;
    }

    std::vector<juce::Component*> transport;
    if(activeTab==0||activeTab==4)transport.push_back(&previewSound);
    if(activeTab!=0){transport.push_back(&playSong);if(activeTab!=4){transport.push_back(&playChorus);transport.push_back(&playDrop);}}
    transport.push_back(&stop);
    const int transportW=cw-174,cell=(transportW-6*((int)transport.size()-1))/(int)transport.size();int tx=cx;
    for(auto* c:transport){c->setBounds(tx,bottomControlsY,cell,34);tx+=cell+6;}
    bpm.setBounds(cx+cw-168,bottomControlsY,168,34);
    playbackBar.setBounds(cx,h-46,cw,15);
    statusLine.setBounds(cx,h-28,cw,18);

    variation1.setBounds(w-226,164,42,28);variation2.setBounds(w-180,164,42,28);
    variation3.setBounds(w-134,164,42,28);variation4.setBounds(w-88,164,42,28);
    captureA.setBounds(w-226,200,88,30);captureB.setBounds(w-130,200,88,30);
    recallA.setBounds(w-226,236,88,30);recallB.setBounds(w-130,236,88,30);

    const int mx=w-225,my=292;
    macroBrightness.setBounds(mx,my,92,92);macroMovement.setBounds(mx+96,my,92,92);
    macroSpace.setBounds(mx,my+104,92,92);macroImpact.setBounds(mx+96,my+104,92,92);

    updateModeVisibility();
}

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

void SonaraAudioProcessorEditor::ExternalDragButton::mouseDrag(const juce::MouseEvent& e){if(e.getDistanceFromDragStart()>6)owner.beginExternalDrag(kind);}

void SonaraAudioProcessorEditor::SoundDNAView::paint(juce::Graphics& g){
    auto r=getLocalBounds().toFloat().reduced(12.f);const auto& d=processor.currentPatch();
    g.setColour(panel2);g.fillRoundedRectangle(r,13.f);g.setColour(purple.withAlpha(.5f));g.drawRoundedRectangle(r,13.f,1.f);
    auto graph=r.reduced(18.f,24.f);graph.removeFromBottom(34.f);
    juce::ColourGradient glow(purple.withAlpha(.28f),graph.getCentreX(),graph.getCentreY(),cyan.withAlpha(.03f),graph.getRight(),graph.getBottom(),true);g.setGradientFill(glow);g.fillRoundedRectangle(graph,10.f);
    const int bars=88; for(int i=0;i<bars;++i){const float x=graph.getX()+graph.getWidth()*i/(bars-1.f);const float seed=std::sin((float)i*.31f+(float)(d.seed%997)*.011f+animation);const float shape=.25f+.75f*std::abs(seed*std::sin((float)i*.071f+d.macroMovement*4.f));const float amp=(12.f+shape*graph.getHeight()*.34f)*(0.45f+d.macroImpact*.55f);g.setColour(juce::Colour::fromHSV(.68f-(float)i/bars*.22f,.72f,.98f,.28f+.55f*shape));g.drawLine(x,graph.getCentreY()-amp*.5f,x,graph.getCentreY()+amp*.5f,1.25f);}
    g.setColour(text);g.setFont(juce::FontOptions(20.f).withStyle("Bold"));g.drawText(d.name,r.getX()+18,r.getY()+10,r.getWidth()-36,24,juce::Justification::left);
    g.setColour(dim);g.setFont(11.f);g.drawText("SOUND DNA • seed "+juce::String(d.seed)+" • unison "+juce::String(d.unison)+" • cutoff "+juce::String((int)d.cutoff)+" Hz",r.getX()+18,r.getBottom()-31,r.getWidth()-36,18,juce::Justification::left);
}

void SonaraAudioProcessorEditor::TimelineView::paint(juce::Graphics& g){
    auto r=getLocalBounds().toFloat();g.setColour(panel2);g.fillRoundedRectangle(r,10.f);auto a=processor.arrangementSnapshot();
    if(!a){g.setColour(dim);g.setFont(14.f);g.drawText("Generate a track to build the arrangement timeline",getLocalBounds(),juce::Justification::centred);return;}
    const auto& lanes=a->getLanes(); const auto& sections=a->getSections(); const int bars=a->getBars(); const double totalBeats=a->getTotalBeats();
    const float labelW=108.f, headerH=24.f,rowH=(r.getHeight()-headerH)/(float)lanes.size();auto grid=r.withTrimmedLeft(labelW).withTrimmedTop(headerH);
    for(const auto& s:sections){const float x=grid.getX()+grid.getWidth()*s.startBar/(float)bars;const float w=grid.getWidth()*s.bars/(float)bars;g.setColour((s.name.contains("DROP")?purple:cyan).withAlpha(.08f+.12f*s.energy));g.fillRect(x,grid.getY(),w,grid.getHeight());g.setColour(text.withAlpha(.55f));g.setFont(9.f);g.drawText(s.name,(int)x+3,(int)r.getY(),(int)w-6,(int)headerH,juce::Justification::centredLeft);g.setColour(juce::Colour(0xff26344c));g.drawVerticalLine((int)x,grid.getY(),grid.getBottom());}
    for(size_t li=0;li<lanes.size();++li){const auto& lane=lanes[li];const float y=grid.getY()+li*rowH;g.setColour(li==(size_t)processor.getSelectedLane()?juce::Colour(0xff15263b):juce::Colour(0xff0c1523));g.fillRect(r.getX(),y,r.getWidth(),rowH-1);g.setColour(laneColour(li));g.fillRect(r.getX()+5,y+5,3,rowH-10);g.setColour(text.withAlpha(.75f));g.setFont(10.f);g.drawText(lane.name,(int)r.getX()+14,(int)y,(int)labelW-18,(int)rowH,juce::Justification::centredLeft);
        // Draw note activity as real clip density blocks.
        for(const auto& n:lane.notes){const float x=grid.getX()+grid.getWidth()*(float)(n.beat/totalBeats);const float w=juce::jmax(1.2f,grid.getWidth()*(float)(n.length/totalBeats));g.setColour(laneColour(li).withAlpha(.38f));g.fillRoundedRectangle(x,y+rowH*.28f,w,rowH*.44f,1.5f);}
    }
    const float px=grid.getX()+grid.getWidth()*(float)processor.songPosition01();g.setColour(juce::Colours::white.withAlpha(.92f));g.drawLine(px,r.getY(),px,r.getBottom(),1.3f);g.setColour(cyan);g.fillEllipse(px-3,r.getY()+2,6,6);
}

void SonaraAudioProcessorEditor::TimelineView::mouseDown(const juce::MouseEvent& e){auto a=processor.arrangementSnapshot();if(!a)return;const auto& lanes=a->getLanes();const float headerH=24.f,rowH=(getHeight()-headerH)/(float)lanes.size();const int lane=juce::jlimit(0,(int)lanes.size()-1,(int)((e.position.y-headerH)/rowH));processor.setSelectedLane(lane);repaint();}

void SonaraAudioProcessorEditor::PianoRollView::paint(juce::Graphics& g){
    auto r=getLocalBounds().toFloat();g.setColour(juce::Colour(0xff09111e));g.fillRoundedRectangle(r,10.f);auto a=processor.arrangementSnapshot();if(!a||!juce::isPositiveAndBelow(processor.getSelectedLane(),(int)a->getLanes().size())){g.setColour(dim);g.drawText("PIANO ROLL",getLocalBounds(),juce::Justification::centred);return;}
    const auto& lanes=a->getLanes();const auto& lane=lanes[(size_t)processor.getSelectedLane()];const float keyW=52.f;auto grid=r.withTrimmedLeft(keyW);const int viewBars=8;const int bars=a->getBars();int centerBar=(int)std::floor(processor.songPosition01()*bars);int firstBar=juce::jlimit(0,juce::jmax(0,bars-viewBars),(centerBar/viewBars)*viewBars);const double beat0=firstBar*4.0,beat1=(firstBar+viewBars)*4.0;
    for(int i=0;i<=viewBars*4;i++){const float x=grid.getX()+grid.getWidth()*i/(viewBars*4.f);g.setColour((i%4==0?juce::Colour(0xff30405a):juce::Colour(0xff172339)).withAlpha(.8f));g.drawVerticalLine((int)x,grid.getY(),grid.getBottom());}
    const int minN=36,maxN=96;for(int n=minN;n<=maxN;n+=12){const float y=grid.getBottom()-grid.getHeight()*(n-minN)/(float)(maxN-minN);g.setColour(juce::Colour(0xff172339));g.drawHorizontalLine((int)y,grid.getX(),grid.getRight());}
    for(const auto& n:lane.notes){if(n.beat+n.length<beat0||n.beat>beat1)continue;const float x=grid.getX()+grid.getWidth()*(float)((n.beat-beat0)/(beat1-beat0));const float w=juce::jmax(3.f,grid.getWidth()*(float)(n.length/(beat1-beat0)));const float y=grid.getBottom()-grid.getHeight()*(juce::jlimit(minN,maxN,n.note)-minN)/(float)(maxN-minN);g.setColour(laneColour((size_t)processor.getSelectedLane()).withAlpha(.88f));g.fillRoundedRectangle(x,y-3,w,6,2.f);}
    g.setColour(text.withAlpha(.72f));g.setFont(10.f);g.drawText(lane.name+" • bars "+juce::String(firstBar+1)+"–"+juce::String(firstBar+viewBars),4,4,(int)r.getWidth()-8,18,juce::Justification::left);
}

SonaraAudioProcessorEditor::SonaraAudioProcessorEditor(SonaraAudioProcessor& x):AudioProcessorEditor(&x),p(x){
    setLookAndFeel(&look);setSize(1320,820);setResizable(true,true);setResizeLimits(1180,720,1900,1200);setOpaque(true);
    prompt.setText("Emotional progressive house, 128 BPM, F minor, huge memorable lead, warm chords, powerful drop");
    prompt.setMultiLine(false);prompt.setFont(14.f);addAndMakeVisible(prompt);

    std::array<juce::Button*,36> buttons {&generateSound,&generateTrack,&similar,&mutate,&randomize,&undo,&redo,
        &variation1,&variation2,&variation3,&variation4,&captureA,&captureB,&recallA,&recallB,&connect,&previewSound,&playSong,&stop,
        &dragPreviewMidi,&dragFullMidi,&dragLaneMidi,&dragReferenceMidi,&dragFullAudio,&dragLaneAudio,&dragStems,
        &loadReference,&importMidi,&resound,&rebuildReference,&saveSoundButton,&loadSoundButton,&saveProjectButton,&loadProjectButton,&exportMixButton,&exportStemsButton};
    for(auto* b:buttons){addAndMakeVisible(*b);styleButton(*b,b==&generateSound||b==&generateTrack||b==&playSong||b==&resound||b==&rebuildReference||b==&exportMixButton||b==&exportStemsButton);}

    juce::TextButton* tabs[]={&tabInstrument,&tabSong,&tabDrums,&tabFx,&tabReference,&tabMidi,&tabExport};
    for(auto* t:tabs){addAndMakeVisible(*t);styleButton(*t);}
    tabInstrument.onClick=[this]{setTab(0);};tabSong.onClick=[this]{setTab(1);};tabDrums.onClick=[this]{setTab(2);p.setSelectedLane(0);};
    tabFx.onClick=[this]{setTab(3);};tabReference.onClick=[this]{setTab(4);};tabMidi.onClick=[this]{setTab(5);};tabExport.onClick=[this]{setTab(6);};

    for(auto* b:{&lockOsc,&lockUnison,&lockEnv,&lockFilter,&lockMod,&lockSources,&lockTone,&lockFx})addAndMakeVisible(*b);
    configureMacro(macroBrightness,"BRIGHT");configureMacro(macroMovement,"MOVE");configureMacro(macroSpace,"SPACE");configureMacro(macroImpact,"IMPACT");
    bpm.setRange(60,200,1);bpm.setValue(p.getPreviewBpm(),juce::dontSendNotification);bpm.setSliderStyle(juce::Slider::LinearHorizontal);bpm.setTextBoxStyle(juce::Slider::TextBoxRight,false,64,20);bpm.setTextValueSuffix(" BPM");addAndMakeVisible(bpm);
    patchName.setColour(juce::Label::textColourId,text);patchName.setFont(juce::FontOptions(15.f).withStyle("Bold"));addAndMakeVisible(patchName);
    statusLine.setColour(juce::Label::textColourId,dim);statusLine.setFont(11.f);addAndMakeVisible(statusLine);
    selectedLaneLabel.setColour(juce::Label::textColourId,text.withAlpha(.75f));selectedLaneLabel.setFont(11.f);addAndMakeVisible(selectedLaneLabel);
    referenceSummary.setColour(juce::Label::textColourId,text.withAlpha(.82f));referenceSummary.setFont(juce::FontOptions(13.f));referenceSummary.setJustificationType(juce::Justification::topLeft);addAndMakeVisible(referenceSummary);
    addAndMakeVisible(soundView);addAndMakeVisible(timeline);addAndMakeVisible(pianoRoll);addAndMakeVisible(playbackBar);

    const std::array<juce::String,8> presetPrompts={"Emotional progressive house lead, wide warm powerful","Future rave lead, aggressive bright festival","Warm short pluck, organic attack, wide delay","Deep reese bass, mono sub, moving texture","Dreamy ambient pad, evolving wide soft","Tech house bass, tight punchy dark","Cinematic bell, metallic organic spacious","Experimental morphing synth, animated texture"};
    for(size_t i=0;i<presets.size();++i){addAndMakeVisible(presets[i]);styleButton(presets[i]);presets[i].onClick=[this,i,presetPrompts]{prompt.setText(presetPrompts[i]);};}

    generateSound.onClick=[this]{p.generatePatch(prompt.getText());};generateTrack.onClick=[this]{p.generateTrack(prompt.getText());bpm.setValue(p.getPreviewBpm(),juce::dontSendNotification);setTab(1);};
    similar.onClick=[this]{p.generateSimilarPatch();};mutate.onClick=[this]{p.mutatePatch();};randomize.onClick=[this]{p.randomizePatch();};undo.onClick=[this]{p.undoPatch();};redo.onClick=[this]{p.redoPatch();};
    variation1.onClick=[this]{p.generateVariation(1);};variation2.onClick=[this]{p.generateVariation(2);};variation3.onClick=[this]{p.generateVariation(3);};variation4.onClick=[this]{p.generateVariation(4);};
    captureA.onClick=[this]{p.captureA();};captureB.onClick=[this]{p.captureB();};recallA.onClick=[this]{p.recallA();};recallB.onClick=[this]{p.recallB();};
    previewSound.onClick=[this]{p.startPreview();};playSong.onClick=[this]{p.startSongPreview();};stop.onClick=[this]{p.stopPreview();p.stopSongPreview();};bpm.onValueChange=[this]{p.setPreviewBpm(bpm.getValue());};
    connect.onClick=[this]{const auto packet=p.exportProjectForCyanoryx();juce::SystemClipboard::copyTextToClipboard(packet);showStatus(packet.isNotEmpty()?"Cyanoryx protocol v4 bundle copied":"Cyanoryx bundle unavailable");};
    loadReference.onClick=[this]{chooseReferenceAudio();};importMidi.onClick=[this]{chooseMidiImport();};resound.onClick=[this]{p.resoundReference(prompt.getText());};rebuildReference.onClick=[this]{p.rebuildInstrumentalFromReference(prompt.getText());setTab(1);};
    saveSoundButton.onClick=[this]{chooseSaveSound();};loadSoundButton.onClick=[this]{chooseLoadSound();};saveProjectButton.onClick=[this]{chooseSaveProject();};loadProjectButton.onClick=[this]{chooseLoadProject();};exportMixButton.onClick=[this]{chooseExportMix();};exportStemsButton.onClick=[this]{chooseExportStems();};
    macroBrightness.onValueChange=[this]{p.setMacro(SonaraAudioProcessor::Macro::brightness,(float)macroBrightness.getValue());};macroMovement.onValueChange=[this]{p.setMacro(SonaraAudioProcessor::Macro::movement,(float)macroMovement.getValue());};macroSpace.onValueChange=[this]{p.setMacro(SonaraAudioProcessor::Macro::space,(float)macroSpace.getValue());};macroImpact.onValueChange=[this]{p.setMacro(SonaraAudioProcessor::Macro::impact,(float)macroImpact.getValue());};
    lockOsc.onClick=[this]{p.mutationLocks().oscillators=lockOsc.getToggleState();};lockUnison.onClick=[this]{p.mutationLocks().unison=lockUnison.getToggleState();};lockEnv.onClick=[this]{p.mutationLocks().ampEnvelope=lockEnv.getToggleState();};lockFilter.onClick=[this]{p.mutationLocks().filter=lockFilter.getToggleState();};lockMod.onClick=[this]{p.mutationLocks().modulation=lockMod.getToggleState();};lockSources.onClick=[this]{p.mutationLocks().sources=lockSources.getToggleState();};lockTone.onClick=[this]{p.mutationLocks().tone=lockTone.getToggleState();};lockFx.onClick=[this]{p.mutationLocks().spatialFx=lockFx.getToggleState();};

    const auto& d=p.currentPatch();macroBrightness.setValue(d.macroBrightness,juce::dontSendNotification);macroMovement.setValue(d.macroMovement,juce::dontSendNotification);macroSpace.setValue(d.macroSpace,juce::dontSendNotification);macroImpact.setValue(d.macroImpact,juce::dontSendNotification);syncLockButtons();setTab(0);startTimerHz(30);
}

SonaraAudioProcessorEditor::~SonaraAudioProcessorEditor(){fileChooser.reset();setLookAndFeel(nullptr);}
void SonaraAudioProcessorEditor::styleButton(juce::Button& b,bool accent){b.setColour(juce::TextButton::buttonColourId,accent?juce::Colour(0xff39256f):juce::Colour(0xff101a2c));b.setColour(juce::TextButton::buttonOnColourId,purple);}
void SonaraAudioProcessorEditor::configureMacro(juce::Slider& s,const juce::String& name){s.setRange(0,1,.001);s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);s.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);s.setName(name);addAndMakeVisible(s);}
void SonaraAudioProcessorEditor::syncLockButtons(){const auto& l=p.mutationLocks();lockOsc.setToggleState(l.oscillators,juce::dontSendNotification);lockUnison.setToggleState(l.unison,juce::dontSendNotification);lockEnv.setToggleState(l.ampEnvelope,juce::dontSendNotification);lockFilter.setToggleState(l.filter,juce::dontSendNotification);lockMod.setToggleState(l.modulation,juce::dontSendNotification);lockSources.setToggleState(l.sources,juce::dontSendNotification);lockTone.setToggleState(l.tone,juce::dontSendNotification);lockFx.setToggleState(l.spatialFx,juce::dontSendNotification);}
void SonaraAudioProcessorEditor::showStatus(const juce::String& s){p.generationStatus=s;statusLine.setText(s,juce::dontSendNotification);}

void SonaraAudioProcessorEditor::beginExternalDrag(ExternalDragButton::Kind kind){
    const auto temp=juce::File::getSpecialLocation(juce::File::tempDirectory);bool ok=false;juce::String status;juce::StringArray files;
    if(kind==ExternalDragButton::Kind::previewMidi){dragFile=temp.getNonexistentChildFile("SONARA-Sound",".mid");ok=p.writePreviewMidiFile(dragFile);status="Dragging sound MIDI to FL Studio";files.add(dragFile.getFullPathName());}
    else if(kind==ExternalDragButton::Kind::fullMidi){dragFile=temp.getNonexistentChildFile("SONARA-Full-Arrangement",".mid");ok=p.writeArrangementMidiFile(dragFile);status="Dragging full editable arrangement MIDI";files.add(dragFile.getFullPathName());}
    else if(kind==ExternalDragButton::Kind::laneMidi){dragFile=temp.getNonexistentChildFile("SONARA-Selected-Lane",".mid");ok=p.writeSelectedLaneMidiFile(dragFile);status="Dragging selected lane MIDI";files.add(dragFile.getFullPathName());}
    else if(kind==ExternalDragButton::Kind::referenceMidi){dragFile=temp.getNonexistentChildFile("SONARA-Reference-Melody",".mid");ok=p.writeReferenceMidiFile(dragFile);status="Dragging extracted reference melody MIDI";files.add(dragFile.getFullPathName());}
    else if(kind==ExternalDragButton::Kind::fullMixAudio){dragFile=temp.getNonexistentChildFile("SONARA-Full-Mix",".wav");ok=p.exportFullMix(dragFile);status="Dragging rendered 24-bit full mix";files.add(dragFile.getFullPathName());}
    else if(kind==ExternalDragButton::Kind::laneAudio){dragFile=temp.getNonexistentChildFile("SONARA-Selected-Lane",".wav");ok=p.exportSelectedLaneAudio(dragFile);status="Dragging selected 24-bit lane";files.add(dragFile.getFullPathName());}
    else {dragFile=temp.getNonexistentChildFile("SONARA-Stems","");ok=dragFile.createDirectory()&&p.exportAllStems(dragFile);status="Dragging all rendered 24-bit stems";if(ok){juce::Array<juce::File> wavs;dragFile.findChildFiles(wavs,juce::File::findFiles,false,"*.wav");for(const auto& f:wavs)files.add(f.getFullPathName());ok=!files.isEmpty();}}
    if(!ok||files.isEmpty()){showStatus("Export unavailable • generate or load the required content first");return;}showStatus(status);
    juce::DragAndDropContainer::performExternalDragDropOfFiles(files,false,this,[this]{showStatus("External drag finished");});
}

void SonaraAudioProcessorEditor::chooseReferenceAudio(){fileChooser=std::make_unique<juce::FileChooser>("Load reference audio",juce::File{},"*.wav;*.aiff;*.aif;*.mp3");fileChooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[this](const juce::FileChooser& c){const auto f=c.getResult();if(f.existsAsFile()&&p.analyseReferenceFile(f)){bpm.setValue(p.getPreviewBpm(),juce::dontSendNotification);referenceSummary.setText(p.getReferenceSummary(),juce::dontSendNotification);}fileChooser.reset();});}
void SonaraAudioProcessorEditor::chooseMidiImport(){fileChooser=std::make_unique<juce::FileChooser>("Import MIDI",juce::File{},"*.mid;*.midi");fileChooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[this](const juce::FileChooser& c){const auto f=c.getResult();if(f.existsAsFile()&&p.importMidiFile(f)){bpm.setValue(p.getPreviewBpm(),juce::dontSendNotification);referenceSummary.setText(p.getReferenceSummary(),juce::dontSendNotification);}fileChooser.reset();});}
void SonaraAudioProcessorEditor::chooseSaveSound(){fileChooser=std::make_unique<juce::FileChooser>("Save SoundDNA",juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("SONARA-Sound.sonara"),"*.sonara");fileChooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::warnAboutOverwriting,[this](const juce::FileChooser& c){auto f=c.getResult();if(f!=juce::File{}){if(f.getFileExtension().isEmpty())f=f.withFileExtension(".sonara");showStatus(p.saveSound(f)?"SoundDNA saved":"SoundDNA save failed");}fileChooser.reset();});}
void SonaraAudioProcessorEditor::chooseLoadSound(){fileChooser=std::make_unique<juce::FileChooser>("Load SoundDNA",juce::File{},"*.sonara");fileChooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[this](const juce::FileChooser& c){const auto f=c.getResult();if(f.existsAsFile())showStatus(p.loadSound(f)?"SoundDNA loaded":"SoundDNA load failed");fileChooser.reset();});}
void SonaraAudioProcessorEditor::chooseSaveProject(){fileChooser=std::make_unique<juce::FileChooser>("Save SONARA project",juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("SONARA-Project.sonaraproject"),"*.sonaraproject");fileChooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::warnAboutOverwriting,[this](const juce::FileChooser& c){auto f=c.getResult();if(f!=juce::File{}){if(f.getFileExtension().isEmpty())f=f.withFileExtension(".sonaraproject");showStatus(p.saveProject(f)?"Project saved":"Project save failed");}fileChooser.reset();});}
void SonaraAudioProcessorEditor::chooseLoadProject(){fileChooser=std::make_unique<juce::FileChooser>("Load SONARA project",juce::File{},"*.sonaraproject");fileChooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[this](const juce::FileChooser& c){const auto f=c.getResult();if(f.existsAsFile()&&p.loadProject(f)){bpm.setValue(p.getPreviewBpm(),juce::dontSendNotification);syncLockButtons();setTab(1);}else if(f.existsAsFile())showStatus("Project load failed");fileChooser.reset();});}
void SonaraAudioProcessorEditor::chooseExportMix(){fileChooser=std::make_unique<juce::FileChooser>("Export 24-bit full mix",juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("SONARA-Full-Mix.wav"),"*.wav");fileChooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::warnAboutOverwriting,[this](const juce::FileChooser& c){auto f=c.getResult();if(f!=juce::File{}){if(f.getFileExtension().isEmpty())f=f.withFileExtension(".wav");p.exportFullMix(f);}fileChooser.reset();});}
void SonaraAudioProcessorEditor::chooseExportStems(){fileChooser=std::make_unique<juce::FileChooser>("Choose folder for SONARA stems",juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),"*");fileChooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectDirectories,[this](const juce::FileChooser& c){const auto d=c.getResult();if(d.isDirectory())p.exportAllStems(d);fileChooser.reset();});}

void SonaraAudioProcessorEditor::setTab(int index){activeTab=juce::jlimit(0,6,index);juce::TextButton* tabs[]={&tabInstrument,&tabSong,&tabDrums,&tabFx,&tabReference,&tabMidi,&tabExport};for(int i=0;i<7;++i){tabs[i]->setToggleState(i==activeTab,juce::dontSendNotification);tabs[i]->setColour(juce::TextButton::buttonColourId,i==activeTab?juce::Colour(0xff2d205a):juce::Colour(0xff0e1728));}updateModeVisibility();repaint();}
void SonaraAudioProcessorEditor::updateModeVisibility(){
    const bool referenceTab=activeTab==4,midiTab=activeTab==5,exportTab=activeTab==6;const bool songView=activeTab==1||activeTab==2||midiTab||exportTab;
    timeline.setVisible(songView);pianoRoll.setVisible(songView);soundView.setVisible(!songView||activeTab==3||referenceTab);referenceSummary.setVisible(referenceTab);
    for(auto* b:std::array<juce::Component*,7>{&generateSound,&generateTrack,&similar,&mutate,&randomize,&undo,&redo})b->setVisible(!referenceTab&&!exportTab);
    for(auto* b:std::array<juce::Component*,4>{&loadReference,&importMidi,&resound,&rebuildReference})b->setVisible(referenceTab);
    for(auto* b:std::array<juce::Component*,6>{&saveSoundButton,&loadSoundButton,&saveProjectButton,&loadProjectButton,&exportMixButton,&exportStemsButton})b->setVisible(exportTab);
    dragPreviewMidi.setVisible(activeTab==0||midiTab);dragFullMidi.setVisible(activeTab==1||midiTab||exportTab);dragLaneMidi.setVisible(activeTab==1||activeTab==2||midiTab||exportTab);dragReferenceMidi.setVisible(referenceTab||midiTab);dragFullAudio.setVisible(exportTab);dragLaneAudio.setVisible(exportTab);dragStems.setVisible(exportTab);
    previewSound.setVisible(activeTab==0||activeTab==3||referenceTab||midiTab);playSong.setVisible(activeTab==1||activeTab==2||referenceTab||midiTab||exportTab);stop.setVisible(true);
    if(referenceTab)referenceSummary.setText(p.hasReference()?p.getReferenceSummary():"LOAD AUDIO or IMPORT MIDI\n\nSONARA analyzes tempo, key and a dominant instrumental melody into editable note data. RESOUND plays that melody with new generated SoundDNA. REBUILD creates new drums, bass, chords, synths and arrangement around the extracted melody without copying the reference audio.",juce::dontSendNotification);
}

void SonaraAudioProcessorEditor::timerCallback(){playbackProgress=p.isSongPlaying()?p.songPosition01():p.previewPosition01();pulse+=.065f;if(pulse>juce::MathConstants<float>::twoPi)pulse=0.f;soundView.animation=pulse;patchName.setText(p.currentPatch().name,juce::dontSendNotification);statusLine.setText(p.generationStatus,juce::dontSendNotification);connect.setButtonText("CYANORYX • BRIDGE READY");playSong.setButtonText(p.isSongPlaying()?"PLAYING SONG":"PLAY SONG");previewSound.setButtonText(p.isPreviewPlaying()?"PREVIEWING":"PREVIEW SOUND");auto a=p.arrangementSnapshot();selectedLaneLabel.setText(a&&juce::isPositiveAndBelow(p.getSelectedLane(),(int)a->getLanes().size())?"SELECTED • "+a->getLanes()[(size_t)p.getSelectedLane()].name:"SELECTED • none",juce::dontSendNotification);if(activeTab==4&&p.hasReference())referenceSummary.setText(p.getReferenceSummary(),juce::dontSendNotification);timeline.repaint();pianoRoll.repaint();soundView.repaint();repaint();}

void SonaraAudioProcessorEditor::paint(juce::Graphics& g){auto r=getLocalBounds().toFloat();juce::ColourGradient b(bg0,0,0,bg1,r.getWidth(),r.getHeight(),false);g.setGradientFill(b);g.fillAll();g.setColour(purple.withAlpha(.07f+.025f*std::sin(pulse)));g.fillEllipse(r.getCentreX()-360,r.getCentreY()-330,720,660);g.setColour(cyan.withAlpha(.035f));g.fillEllipse(r.getRight()-430,40,380,380);g.setColour(text);g.setFont(juce::FontOptions(32.f).withStyle("Bold"));g.drawText("SONARA",30,18,220,38,juce::Justification::left);g.setColour(cyan);g.setFont(11.f);g.drawText("AI MUSIC CREATOR • SOUND + DRUM + TRACK",32,51,360,18,juce::Justification::left);const auto left=juce::Rectangle<float>(22,118,210,(float)getHeight()-195),right=juce::Rectangle<float>((float)getWidth()-244,118,222,(float)getHeight()-195);g.setColour(panel.withAlpha(.96f));g.fillRoundedRectangle(left,12);g.fillRoundedRectangle(right,12);g.setColour(juce::Colour(0xff24324a));g.drawRoundedRectangle(left,12,1);g.drawRoundedRectangle(right,12,1);g.setColour(dim);g.setFont(10.f);g.drawText("PROMPT PRESETS",38,134,165,18,juce::Justification::left);g.drawText("PERFORMANCE / SOUND DNA",getWidth()-226,134,190,18,juce::Justification::left);g.setColour(juce::Colour(0xff17243a));g.fillRoundedRectangle(252,118,(float)getWidth()-516,(float)getHeight()-195,12);g.setColour(juce::Colour(0xff263650));g.drawRoundedRectangle(252,118,(float)getWidth()-516,(float)getHeight()-195,12,1);g.setColour(text.withAlpha(.75f));g.setFont(10.f);g.drawText("MUTATION LOCKS",35,getHeight()-265,170,16,juce::Justification::left);if(activeTab==4){g.setColour(cyan.withAlpha(.75f));g.drawText("REFERENCE / RESOUND",270,238,220,18,juce::Justification::left);}if(activeTab==6){g.setColour(cyan.withAlpha(.75f));g.drawText("24-BIT AUDIO + STEM EXPORT",270,238,260,18,juce::Justification::left);}const float progress=juce::jlimit(0.f,1.f,p.generationProgress.load());const auto prog=juce::Rectangle<float>(30,(float)getHeight()-40,(float)getWidth()-60,5);g.setColour(juce::Colour(0xff16243a));g.fillRoundedRectangle(prog,3);g.setColour(cyan);g.fillRoundedRectangle(prog.withWidth(prog.getWidth()*progress),3);}

void SonaraAudioProcessorEditor::resized(){const int w=getWidth(),h=getHeight();const int tabY=82,gap=5,tabW=(w-60-6*gap)/7;juce::TextButton* tabs[]={&tabInstrument,&tabSong,&tabDrums,&tabFx,&tabReference,&tabMidi,&tabExport};for(int i=0;i<7;++i)tabs[i]->setBounds(30+i*(tabW+gap),tabY,tabW,30);connect.setBounds(w-250,24,220,34);int py=160;for(auto& b:presets){b.setBounds(35,py,184,31);py+=36;}const int lockY=h-240;juce::ToggleButton* locks1[]={&lockOsc,&lockUnison,&lockEnv,&lockFilter};juce::ToggleButton* locks2[]={&lockMod,&lockSources,&lockTone,&lockFx};for(int i=0;i<4;++i){locks1[i]->setBounds(34+(i%2)*94,lockY+(i/2)*30,90,27);locks2[i]->setBounds(34+(i%2)*94,lockY+64+(i/2)*30,90,27);}const int cx=270,cw=w-550;prompt.setBounds(cx,132,cw,42);auto layoutRow=[&](std::initializer_list<juce::Component*> items,int y,int height=38){const int n=(int)items.size();if(n<=0)return;const int g=6,cell=(cw-g*(n-1))/n;int x=cx;for(auto* c:items){c->setBounds(x,y,cell,height);x+=cell+g;}};layoutRow({&generateSound,&generateTrack,&similar,&mutate,&randomize,&undo,&redo},184);layoutRow({&loadReference,&importMidi,&resound,&rebuildReference},184);layoutRow({&saveSoundButton,&loadSoundButton,&saveProjectButton,&loadProjectButton,&exportMixButton,&exportStemsButton},184);const int upperY=230,upperH=230;soundView.setBounds(cx,upperY,cw,upperH);timeline.setBounds(cx,upperY,cw,upperH);referenceSummary.setBounds(cx+28,upperY+48,cw-56,upperH-72);pianoRoll.setBounds(cx,upperY+upperH+9,cw,juce::jmax(105,h-upperY-upperH-200));patchName.setBounds(cx+18,upperY+8,cw-36,22);selectedLaneLabel.setBounds(cx,h-154,190,20);layoutRow({&dragPreviewMidi,&dragFullMidi,&dragLaneMidi,&dragReferenceMidi,&dragFullAudio,&dragLaneAudio,&dragStems},h-128,34);previewSound.setBounds(cx,h-84,120,34);playSong.setBounds(cx+128,h-84,112,34);stop.setBounds(cx+248,h-84,70,34);bpm.setBounds(cx+328,h-84,juce::jmax(140,cw-328),34);playbackBar.setBounds(cx,h-44,cw,15);statusLine.setBounds(cx,h-27,cw,18);variation1.setBounds(w-226,164,42,28);variation2.setBounds(w-180,164,42,28);variation3.setBounds(w-134,164,42,28);variation4.setBounds(w-88,164,42,28);captureA.setBounds(w-226,200,88,30);captureB.setBounds(w-130,200,88,30);recallA.setBounds(w-226,236,88,30);recallB.setBounds(w-130,236,88,30);const int mx=w-225,my=292;macroBrightness.setBounds(mx,my,92,92);macroMovement.setBounds(mx+96,my,92,92);macroSpace.setBounds(mx,my+104,92,92);macroImpact.setBounds(mx+96,my+104,92,92);updateModeVisibility();}
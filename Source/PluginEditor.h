#pragma once

#include "PluginProcessor.h"

struct KissTheme
{
    enum class Anim { None, Stars, Drip, Trip, Bubbles, Embers, Wind };

    juce::Colour background, panel, text, dim, accent, glow;
    Anim anim = Anim::None;
};

const KissTheme& getKissTheme (int index);
int getNumKissThemes();

//==================================================================================================
// Knob drawing. The halo grows with the value - nothing at zero - and the slider's box is
// padded so it never gets squared off against the edge of its own bounds.
//==================================================================================================
class KissLookAndFeel : public juce::LookAndFeel_V4
{
public:
    KissLookAndFeel() = default;

    void setTheme (const KissTheme& t) { theme = t; }

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override;

    juce::Font getLabelFont (juce::Label&) override;

private:
    KissTheme theme = getKissTheme (0);
};

//==================================================================================================
// Any filter curve, drawn as the control itself. Drag sideways for frequency, click to switch
// it on or off, and resonance lives on its own handle.
//==================================================================================================
class FilterDisplay : public juce::Component
{
public:
    FilterDisplay (KissAudioProcessor&, juce::String onId, juce::String freqId,
                   juce::String qId, juce::String typeId, juce::String caption,
                   float loHz, float hiHz);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;

    void setTheme (const KissTheme& t) { theme = t; repaint(); }
    void refresh();

    std::function<void()> onFrequencyMoved;

private:
    KissAudioProcessor& processor;
    juce::String onId, freqId, qId, typeId, caption;
    float loHz, hiHz;
    KissTheme theme = getKissTheme (0);
    float startFreq = 0.0f;
    bool  dragged = false;
    float lastFreq = -1.0f, lastQ = -1.0f, lastType = -1.0f;
    bool  lastOn = false;
};

//==================================================================================================
class QHandle : public juce::Component
{
public:
    QHandle (KissAudioProcessor&, juce::String onId, juce::String qId);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

    void setTheme (const KissTheme& t) { theme = t; repaint(); }
    void refresh();

    std::function<void()> onQMoved;

private:
    KissAudioProcessor& processor;
    juce::String onId, qId;
    KissTheme theme = getKissTheme (0);
    float startQ = 1.0f, lastQ = -1.0f;
};

//==================================================================================================
class PresetMenuItem : public juce::PopupMenu::CustomComponent
{
public:
    PresetMenuItem (juce::String nameIn, int chamberSizeIn, const KissTheme& themeIn,
                    bool showTrashIn = false);

    void getIdealSize (int& idealWidth, int& idealHeight) override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    std::function<void()> onTrash;

private:
    juce::Rectangle<int> trashArea() const;

    juce::String name;
    int chamberSize;
    KissTheme theme;
    bool showTrash = false;
};

//==================================================================================================

class KissAudioProcessorEditor : public juce::AudioProcessorEditor,
                                 private juce::Timer
{
public:
    explicit KissAudioProcessorEditor (KissAudioProcessor&);
    ~KissAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    // Everything lives on one fixed-size surface that gets scaled to the window, so the
    // plugin resizes without a single control drifting out of place.
    struct Surface : public juce::Component
    {
        explicit Surface (KissAudioProcessorEditor& o) : owner (o) { setInterceptsMouseClicks (false, true); }
        void paint (juce::Graphics&) override;
        KissAudioProcessorEditor& owner;
    };

    struct Knob
    {
        juce::Slider slider;
        juce::Label  caption;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void addKnob (Knob&, const juce::String& parameterID, const juce::String& captionText);
    void layoutKnob (Knob&, juce::Rectangle<int> cell, int boxSize);

    void timerCallback() override;
    void refreshFromParameters();
    void applyTheme();
    void cycleTheme();
    void cycleDivision();
    void cyclePostType();
    void showPresetMenu();
    void promptSavePreset();
    void ripple (juce::Component& from, float amount01);
    void stepPreset (int delta);
    void refreshPresetName();

    int  getChoiceValue (const juce::String& id) const;
    void setChoiceValue (const juce::String& id, int newValue);

    void paintBackdrop (juce::Graphics&);
    void seedParticles();

    KissAudioProcessor& processor;
    KissLookAndFeel     lnf;
    KissTheme           theme = getKissTheme (0);
    Surface             surface { *this };

    juce::GroupComponent delayGroup, reverbGroup, sfxGroup, postGroup;

    Knob reverbTime, reverbTone, reverbMix;
    Knob delayTimeKnob, delayFeedback, delayMix;

    juce::TextButton divisionButton { "1/4" };
    juce::TextButton syncButton { "SYNC" };
    std::unique_ptr<ButtonAttachment> syncAttachment;
    juce::Label delayReadout;

    FilterDisplay repeatFilter;
    QHandle       repeatQ;
    juce::Label   repeatReadout;

    FilterDisplay postFilter;
    QHandle       postQ;
    juce::Label   postReadout;
    juce::TextButton postTypeButton { "LOW PASS" };
    juce::Label   postChainNote;

    struct SfxSlotUI
    {
        juce::TextButton button;
        Knob amount;
        std::unique_ptr<ButtonAttachment> attachment;
    };

    SfxSlotUI sfxSlots[numSfxSlots];

    juce::TextButton themeButton { "DRIP" };
    juce::TextButton presetButton { "PRESETS" };
    juce::TextButton saveButton { "SAVE" };

    // Step through the whole factory list without opening the menu.
    juce::TextButton prevPreset { "<" }, nextPreset { ">" };
    juce::Label      presetName;
    int              lastShownPreset = -2;
    juce::String     pendingArchive;

    int   lastTheme = -1, lastSync = -1, lastDiv = -1, lastPostType = -1;
    float animPhase = 0.0f;

    // One ripple, from wherever the last knob move happened.
    juce::Point<float> rippleAt { -1.0f, -1.0f };
    float rippleT = -1.0f;
    float rippleReach = 0.0f;   // how far the wave travels, set by the knob's own value

    juce::Random rng { 20260919 };
    struct Particle { float x, y, vx, vy, s, a, r, dr; };
    juce::Array<Particle> parts;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KissAudioProcessorEditor)
};

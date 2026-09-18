#pragma once

#include "PluginProcessor.h"

// The "editor" is the window the DAW opens when you double-click the plugin. It runs on
// the normal UI thread, and it never touches audio directly: every knob is attached to a
// parameter in the processor's apvts, and the processor reads those on the audio thread.
class KissAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit KissAudioProcessorEditor (KissAudioProcessor&);
    ~KissAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;    // draw the background / anything that's not a child component
    void resized() override;                  // position the child components; called on open and on resize

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    // One knob = a rotary slider, a caption above it, and the attachment that keeps the
    // slider in sync with its parameter in both directions (turning the knob updates the
    // parameter; automation or preset recall updates the knob).
    struct Knob
    {
        juce::Slider slider;
        juce::Label  caption;
        std::unique_ptr<SliderAttachment> attachment;   // declared last so it's destroyed first
    };

    void addKnob (Knob&, const juce::String& parameterID, const juce::String& captionText);
    static void layoutKnobsInRow (juce::Rectangle<int> area, std::initializer_list<Knob*> knobs);

    KissAudioProcessor& processor;

    juce::GroupComponent delayGroup, reverbGroup;   // the labelled boxes around each section
    Knob delayTime, delayFeedback, delayMix;
    Knob reverbSize, reverbDamping, reverbWidth, reverbMix;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KissAudioProcessorEditor)
};

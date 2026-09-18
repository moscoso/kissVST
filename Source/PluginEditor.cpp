#include "PluginEditor.h"

KissAudioProcessorEditor::KissAudioProcessorEditor (KissAudioProcessor& p)
    : AudioProcessorEditor (p), processor (p)
{
    delayGroup.setText ("DELAY");
    reverbGroup.setText ("REVERB");
    addAndMakeVisible (delayGroup);
    addAndMakeVisible (reverbGroup);

    addKnob (delayTime,     "delayTime",     "Time");
    addKnob (delayFeedback, "delayFeedback", "Feedback");
    addKnob (delayMix,      "delayMix",      "Mix");

    addKnob (reverbSize,    "reverbSize",    "Size");
    addKnob (reverbDamping, "reverbDamping", "Damping");
    addKnob (reverbWidth,   "reverbWidth",   "Width");
    addKnob (reverbMix,     "reverbMix",     "Mix");

    setSize (700, 260);   // triggers the first resized()
}

void KissAudioProcessorEditor::addKnob (Knob& k, const juce::String& parameterID, const juce::String& captionText)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
    addAndMakeVisible (k.slider);

    k.caption.setText (captionText, juce::dontSendNotification);
    k.caption.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (k.caption);

    // The attachment copies the parameter's range, default and text formatting onto the slider,
    // so those are defined once (in PluginProcessor.cpp) rather than repeated here.
    k.attachment = std::make_unique<SliderAttachment> (processor.apvts, parameterID, k.slider);
}

void KissAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e1e24));

    g.setColour (juce::Colours::white);
    g.setFont (20.0f);
    g.drawText ("KISS", getLocalBounds().removeFromTop (40), juce::Justification::centred);
}

void KissAudioProcessorEditor::resized()
{
    // Layout is done by slicing rectangles off the window bounds, top to bottom, left to right.
    auto area = getLocalBounds().reduced (12);
    area.removeFromTop (36);   // title strip, drawn in paint()

    const int gap = 12;

    // Delay gets 3/7 of the width and reverb 4/7 - proportional to knob count, so every knob ends up the same size.
    auto delayArea = area.removeFromLeft ((area.getWidth() - gap) * 3 / 7);
    area.removeFromLeft (gap);
    auto reverbArea = area;

    delayGroup.setBounds (delayArea);
    reverbGroup.setBounds (reverbArea);

    // Inset so the knobs sit inside each group's border and below its title text.
    layoutKnobsInRow (delayArea.reduced (10).withTrimmedTop (14),  { &delayTime, &delayFeedback, &delayMix });
    layoutKnobsInRow (reverbArea.reduced (10).withTrimmedTop (14), { &reverbSize, &reverbDamping, &reverbWidth, &reverbMix });
}

void KissAudioProcessorEditor::layoutKnobsInRow (juce::Rectangle<int> area, std::initializer_list<Knob*> knobs)
{
    const int cellWidth = area.getWidth() / (int) knobs.size();

    for (auto* k : knobs)
    {
        auto cell = area.removeFromLeft (cellWidth);
        k->caption.setBounds (cell.removeFromTop (20));
        k->slider.setBounds (cell);
    }
}

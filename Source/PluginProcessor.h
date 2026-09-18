#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

// The "processor" is the audio half of the plugin. The DAW calls processBlock() on a
// high-priority audio thread with a small buffer of samples (typically 64-512) and
// expects it back, processed, before the next one is due. Nothing in here may block,
// allocate, or lock. The UI half lives in PluginEditor.h and talks to this class only
// through the parameter tree (apvts).
class KissAudioProcessor : public juce::AudioProcessor
{
public:
    KissAudioProcessor();
    ~KissAudioProcessor() override = default;

    // --- audio lifecycle -------------------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    // --- editor ----------------------------------------------------------------------
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // --- plugin description ----------------------------------------------------------
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 5.0; }   // let echoes ring out after a clip stops

    // Presets. We don't implement programs, but the interface requires exactly one.
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    // Called by the DAW when saving/loading a project so knob positions persist.
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // All the plugin's parameters, with automation, state saving and thread-safe access
    // handled for us. The editor attaches its sliders to this.
    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Cached pointers into apvts so processBlock() can read parameters with a single
    // atomic load instead of a string lookup per block.
    std::atomic<float>* delayTime     = nullptr;   // ms
    std::atomic<float>* delayFeedback = nullptr;   // %
    std::atomic<float>* delayMix      = nullptr;   // %
    std::atomic<float>* reverbSize    = nullptr;   // %
    std::atomic<float>* reverbDamping = nullptr;   // %
    std::atomic<float>* reverbWidth   = nullptr;   // %
    std::atomic<float>* reverbMix     = nullptr;   // %

    // --- DSP -------------------------------------------------------------------------
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLine;
    juce::SmoothedValue<float> delaySamples;   // glides the delay time so knob moves don't click
    juce::dsp::Reverb reverb;
    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KissAudioProcessor)
};

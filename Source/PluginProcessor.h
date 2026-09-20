#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

// The audio half of the plugin. The DAW calls processBlock() on a high-priority thread
// with a small buffer and expects it back before the next one is due. Nothing in here
// may block, allocate, or lock.
//
// Signal chain: REVERB -> DELAY -> six SFX slots in series.
//
// The six slots are fixed: 1 chorus, 2 flanger, 3 phaser, 4 ring, 5 crush, 6 bonus.
// Each is a simple on/off with its own amount, so the rack reads like a pedalboard.

inline constexpr int numSfxSlots = 6;

class KissAudioProcessor : public juce::AudioProcessor
{
public:
    KissAudioProcessor();
    ~KissAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    //==============================================================================
    // Presets. One compact row each, so sixty of them stay readable.
    //==============================================================================
    struct FactoryPreset
    {
        const char* name;
        int   folder;        // index into getPresetFolders()
        int   size;          // 0..9, drives the little room icon in the menu
        float rvTime, rvTone, rvMix;
        int   div;           // index into the division list, or -1 for milliseconds
        float ms;            // used only when div is -1
        float feedback, dMix;
        int   filtOn;
        float freq, q;
        int   sfxMask;       // bit 0 chorus, 1 flanger, 2 phaser, 3 ring, 4 crush, 5 bonus
        float sfxAmt;
    };

    static const juce::StringArray& getPresetFolders();
    static const std::vector<FactoryPreset>& getFactoryPresets();
    static const juce::Array<int>& getFavouriteIndices();

    void applyFactoryPreset (int index);
    void applyStateXml (const juce::XmlElement& xml);

    static juce::File getUserPresetFolder();
    juce::StringArray getUserPresetNames() const;
    bool saveUserPreset (const juce::String& name);
    bool loadUserPreset (const juce::String& name);
    bool archiveUserPreset (const juce::String& name);

    // Tempo, refreshed once per block. The editor reads it to show the synced time in ms.
    std::atomic<float> hostBpm { 120.0f };

    // Which factory preset is loaded, so the editor can show it and step through them.
    std::atomic<int> currentPreset { -1 };

    // Shared with the editor so the two halves can never disagree about the maths.
    static float  getDivisionSixteenths (int index);
    static double divisionToMs (int index, float bpm);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    std::atomic<float>* reverbTime    = nullptr;
    std::atomic<float>* reverbTone    = nullptr;
    std::atomic<float>* reverbMix     = nullptr;

    std::atomic<float>* delaySync     = nullptr;
    std::atomic<float>* delayDiv      = nullptr;
    std::atomic<float>* delayTime     = nullptr;
    std::atomic<float>* delayFeedback = nullptr;
    std::atomic<float>* delayMix      = nullptr;

    std::atomic<float>* filterOn      = nullptr;
    std::atomic<float>* filterFreq    = nullptr;
    std::atomic<float>* filterQ       = nullptr;

    // The master filter, last in the chain, across everything.
    std::atomic<float>* postOn        = nullptr;
    std::atomic<float>* postType      = nullptr;
    std::atomic<float>* postFreq      = nullptr;
    std::atomic<float>* postQ         = nullptr;

    std::atomic<float>* sfxOn[numSfxSlots]     { };
    std::atomic<float>* sfxAmount[numSfxSlots] { };

    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLine;
    juce::SmoothedValue<float> delaySamples;
    juce::dsp::StateVariableTPTFilter<float> feedbackFilter;
    juce::dsp::StateVariableTPTFilter<float> postFilter;
    juce::dsp::Reverb reverb;

    // Each slot owns its own state, otherwise two effects would fight over one set of buffers.
    struct SfxSlot
    {
        juce::dsp::Chorus<float> chorus;    // also does flanger duty, short and hot
        juce::dsp::Phaser<float> phaser;
        juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> warp;

        float lfoPhase  = 0.0f;
        float holdValue[2] { 0.0f, 0.0f };
        float holdCount = 0.0f;

        void prepare (const juce::dsp::ProcessSpec&, double sampleRate);
    };

    SfxSlot slots[numSfxSlots];

    void processSfxSlot (SfxSlot&, juce::AudioBuffer<float>&, int kind, float amount);

    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KissAudioProcessor)
};

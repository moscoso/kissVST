#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // Longest delay the Time knob allows. The delay line is sized from this in prepareToPlay().
    constexpr float maxDelayMs = 2000.0f;

    // How parameter values are displayed in the knob's text box (and in the DAW's automation lane).
    juce::String msText      (float v, int) { return juce::String (juce::roundToInt (v)) + " ms"; }
    juce::String percentText (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; }
}

//==================================================================================================
// Parameters
//==================================================================================================

juce::AudioProcessorValueTreeState::ParameterLayout KissAudioProcessor::createParameterLayout()
{
    using Param = juce::AudioParameterFloat;
    using Attr  = juce::AudioParameterFloatAttributes;
    using Range = juce::NormalisableRange<float>;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Each parameter: { id, version }, display name, range (min, max, step, skew), default, attributes.
    // The id is what the editor and the DAW use to refer to it, so never rename one after release.

    // --- Delay ---
    // Skew 0.4 gives the low end of the knob more travel, so short slap-back times are easy to dial in.
    layout.add (std::make_unique<Param> (juce::ParameterID { "delayTime", 1 }, "Delay Time",
                                         Range (1.0f, maxDelayMs, 1.0f, 0.4f), 350.0f,
                                         Attr().withStringFromValueFunction (msText)));
    // Capped below 100% so the echoes always decay eventually.
    layout.add (std::make_unique<Param> (juce::ParameterID { "delayFeedback", 1 }, "Delay Feedback",
                                         Range (0.0f, 95.0f, 1.0f), 40.0f,
                                         Attr().withStringFromValueFunction (percentText)));
    layout.add (std::make_unique<Param> (juce::ParameterID { "delayMix", 1 }, "Delay Mix",
                                         Range (0.0f, 100.0f, 1.0f), 30.0f,
                                         Attr().withStringFromValueFunction (percentText)));

    // --- Reverb ---
    layout.add (std::make_unique<Param> (juce::ParameterID { "reverbSize", 1 }, "Reverb Size",
                                         Range (0.0f, 100.0f, 1.0f), 50.0f,
                                         Attr().withStringFromValueFunction (percentText)));
    layout.add (std::make_unique<Param> (juce::ParameterID { "reverbDamping", 1 }, "Reverb Damping",
                                         Range (0.0f, 100.0f, 1.0f), 50.0f,
                                         Attr().withStringFromValueFunction (percentText)));
    layout.add (std::make_unique<Param> (juce::ParameterID { "reverbWidth", 1 }, "Reverb Width",
                                         Range (0.0f, 100.0f, 1.0f), 100.0f,
                                         Attr().withStringFromValueFunction (percentText)));
    layout.add (std::make_unique<Param> (juce::ParameterID { "reverbMix", 1 }, "Reverb Mix",
                                         Range (0.0f, 100.0f, 1.0f), 30.0f,
                                         Attr().withStringFromValueFunction (percentText)));

    return layout;
}

//==================================================================================================
// Lifecycle
//==================================================================================================

KissAudioProcessor::KissAudioProcessor()
    : AudioProcessor (BusesProperties().withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    delayTime     = apvts.getRawParameterValue ("delayTime");
    delayFeedback = apvts.getRawParameterValue ("delayFeedback");
    delayMix      = apvts.getRawParameterValue ("delayMix");
    reverbSize    = apvts.getRawParameterValue ("reverbSize");
    reverbDamping = apvts.getRawParameterValue ("reverbDamping");
    reverbWidth   = apvts.getRawParameterValue ("reverbWidth");
    reverbMix     = apvts.getRawParameterValue ("reverbMix");
}

bool KissAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Mono or stereo, and the same on both sides. (The reverb only knows those two.)
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

// Called before playback starts and whenever the sample rate or buffer size changes.
// This is where we're allowed to allocate.
void KissAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    juce::dsp::ProcessSpec spec { sampleRate,
                                  (juce::uint32) samplesPerBlock,
                                  (juce::uint32) getTotalNumOutputChannels() };

    delayLine.setMaximumDelayInSamples (juce::roundToInt (sampleRate * maxDelayMs / 1000.0) + 1);
    delayLine.prepare (spec);

    delaySamples.reset (sampleRate, 0.05);   // 50 ms glide when the Time knob moves
    delaySamples.setCurrentAndTargetValue ((float) (delayTime->load() * sampleRate / 1000.0));

    reverb.prepare (spec);
}

//==================================================================================================
// Audio
//==================================================================================================

void KissAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;   // stops tiny reverb tails from becoming CPU-expensive denormals

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // ---- 1. Delay -------------------------------------------------------------------
    // A feedback delay: what comes out of the delay line is fed back in, scaled by feedback,
    // so each echo is quieter than the last. Done per sample (not per block) so the delay
    // time can glide smoothly while you turn the knob - you'll hear a tape-style pitch bend.
    const float feedback = delayFeedback->load() * 0.01f;
    const float mix      = delayMix->load() * 0.01f;
    delaySamples.setTargetValue ((float) (delayTime->load() * currentSampleRate / 1000.0));

    for (int i = 0; i < numSamples; ++i)
    {
        delayLine.setDelay (delaySamples.getNextValue());

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);

            const float dry     = data[i];
            const float delayed = delayLine.popSample (ch);
            delayLine.pushSample (ch, dry + delayed * feedback);

            data[i] = dry * (1.0f - mix) + delayed * mix;
        }
    }

    // ---- 2. Reverb ------------------------------------------------------------------
    // juce::dsp::Reverb is a Freeverb (Schroeder comb + allpass) implementation with a
    // dry/wet mix built in, so we just hand it the whole buffer.
    juce::Reverb::Parameters rp;
    rp.roomSize   = reverbSize->load() * 0.01f;
    rp.damping    = reverbDamping->load() * 0.01f;
    rp.width      = reverbWidth->load() * 0.01f;
    rp.wetLevel   = reverbMix->load() * 0.01f;
    rp.dryLevel   = 1.0f - rp.wetLevel;
    rp.freezeMode = 0.0f;
    reverb.setParameters (rp);

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> context (block);
    reverb.process (context);
}

//==================================================================================================
// State
//==================================================================================================

void KissAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void KissAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==================================================================================================

juce::AudioProcessorEditor* KissAudioProcessor::createEditor()
{
    return new KissAudioProcessorEditor (*this);
}

// The one global entry point every JUCE plugin must provide; the host calls it to create us.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KissAudioProcessor();
}

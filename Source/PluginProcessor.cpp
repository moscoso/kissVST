#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // 1 WHOLE at 48 BPM is 5 seconds, so the line has to be long enough to hold it.
    // The old 2-second ceiling silently clamped every whole note below 120 BPM.
    constexpr float maxDelayMs = 5000.0f;

    juce::String msText      (float v, int) { return juce::String (juce::roundToInt (v)) + " ms"; }
    juce::String percentText (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; }
    juce::String qText       (float v, int) { return "Q " + juce::String (v, 2); }
    juce::String hzText      (float v, int)
    {
        return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + " kHz"
                            : juce::String (juce::roundToInt (v)) + " Hz";
    }
    juce::String secText (float v, int)
    {
        return juce::String (0.25f + (v * 0.01f) * 7.75f, 1) + " s";
    }

    // Note lengths in sixteenth notes. Triplets are two thirds of the straight value.
    const float divisionSixteenths[] { 2.0f/3.0f, 1.0f, 4.0f/3.0f, 2.0f,
                                       8.0f/3.0f, 4.0f, 16.0f/3.0f, 8.0f, 16.0f };
}

extern const juce::StringArray kDivisionNames
    { "1/16 T", "1/16", "1/8 T", "1/8", "1/4 T", "1/4", "1/2 T", "1/2", "1 WHOLE" };
extern const juce::StringArray kSfxNames
    { "CHORUS", "FLANGER", "PHASER", "RING", "CRUSH", "BONUS" };
extern const juce::StringArray kPostTypeNames { "LOW PASS", "BAND PASS", "HIGH PASS" };
extern const juce::StringArray kThemeNames
    { "MIDNIGHT", "NEON", "DRIP", "TRIP", "BONE", "WATER", "FIRE", "AIR" };

float KissAudioProcessor::getDivisionSixteenths (int index)
{
    return divisionSixteenths[juce::jlimit (0, kDivisionNames.size() - 1, index)];
}

double KissAudioProcessor::divisionToMs (int index, float bpm)
{
    const auto safeBpm = juce::jlimit (20.0f, 300.0f, bpm);
    return (double) getDivisionSixteenths (index) * (60000.0 / (double) safeBpm) / 4.0;
}

juce::String sfxParamId (int slot, const char* suffix)
{
    return "sfx" + juce::String (slot + 1) + suffix;
}

//==================================================================================================
// Parameters
//==================================================================================================

juce::AudioProcessorValueTreeState::ParameterLayout KissAudioProcessor::createParameterLayout()
{
    using Param  = juce::AudioParameterFloat;
    using Choice = juce::AudioParameterChoice;
    using Bool   = juce::AudioParameterBool;
    using Attr   = juce::AudioParameterFloatAttributes;
    using Range  = juce::NormalisableRange<float>;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<Param> (juce::ParameterID { "reverbTime", 1 }, "Reverb Time",
                                         Range (0.0f, 100.0f, 1.0f), 45.0f,
                                         Attr().withStringFromValueFunction (secText)));
    layout.add (std::make_unique<Param> (juce::ParameterID { "reverbTone", 1 }, "Reverb Tone",
                                         Range (0.0f, 100.0f, 1.0f), 50.0f,
                                         Attr().withStringFromValueFunction (percentText)));
    layout.add (std::make_unique<Param> (juce::ParameterID { "reverbMix", 1 }, "Reverb Dry/Wet",
                                         Range (0.0f, 100.0f, 1.0f), 25.0f,
                                         Attr().withStringFromValueFunction (percentText)));

    layout.add (std::make_unique<Bool>   (juce::ParameterID { "delaySync", 1 }, "Delay Sync", true));
    layout.add (std::make_unique<Choice> (juce::ParameterID { "delayDiv", 1 }, "Delay Division",
                                          kDivisionNames, 5));      // 1/4
    layout.add (std::make_unique<Param>  (juce::ParameterID { "delayTime", 1 }, "Delay Time",
                                          Range (1.0f, maxDelayMs, 1.0f, 0.35f), 350.0f,
                                          Attr().withStringFromValueFunction (msText)));
    layout.add (std::make_unique<Param>  (juce::ParameterID { "delayFeedback", 1 }, "Delay Feedback",
                                          Range (0.0f, 95.0f, 1.0f), 40.0f,
                                          Attr().withStringFromValueFunction (percentText)));
    layout.add (std::make_unique<Param>  (juce::ParameterID { "delayMix", 1 }, "Delay Dry/Wet",
                                          Range (0.0f, 100.0f, 1.0f), 30.0f,
                                          Attr().withStringFromValueFunction (percentText)));

    layout.add (std::make_unique<Bool>  (juce::ParameterID { "filterOn", 1 }, "Filter", false));
    layout.add (std::make_unique<Param> (juce::ParameterID { "filterFreq", 1 }, "Filter Freq",
                                         Range (120.0f, 12000.0f, 1.0f, 0.3f), 1600.0f,
                                         Attr().withStringFromValueFunction (hzText)));
    layout.add (std::make_unique<Param> (juce::ParameterID { "filterQ", 1 }, "Filter Q",
                                         Range (0.30f, 8.0f, 0.01f, 0.5f), 1.20f,
                                         Attr().withStringFromValueFunction (qText)));

    // Six fixed slots. Chorus opens gentler than the rest, so it starts at 30%.
    for (int i = 0; i < numSfxSlots; ++i)
    {
        layout.add (std::make_unique<Bool> (juce::ParameterID { sfxParamId (i, "On"), 1 },
                                            kSfxNames[i], false));
        layout.add (std::make_unique<Param> (juce::ParameterID { sfxParamId (i, "Amt"), 1 },
                                             kSfxNames[i] + " Amount",
                                             Range (0.0f, 100.0f, 1.0f), 30.0f,
                                             Attr().withStringFromValueFunction (percentText)));
    }

    // The master filter. Last in the chain, after everything, on the whole signal.
    layout.add (std::make_unique<Bool>   (juce::ParameterID { "postOn", 1 }, "Master Filter", false));
    layout.add (std::make_unique<Choice> (juce::ParameterID { "postType", 1 }, "Master Filter Type",
                                          kPostTypeNames, 0));
    layout.add (std::make_unique<Param>  (juce::ParameterID { "postFreq", 1 }, "Master Filter Freq",
                                          Range (30.0f, 18000.0f, 1.0f, 0.28f), 8000.0f,
                                          Attr().withStringFromValueFunction (hzText)));
    layout.add (std::make_unique<Param>  (juce::ParameterID { "postQ", 1 }, "Master Filter Q",
                                          Range (0.30f, 8.0f, 0.01f, 0.5f), 0.80f,
                                          Attr().withStringFromValueFunction (qText)));

    layout.add (std::make_unique<Choice> (juce::ParameterID { "theme", 1 }, "Theme", kThemeNames, 2));

    return layout;
}

//==================================================================================================
// Presets
//==================================================================================================

const juce::StringArray& KissAudioProcessor::getPresetFolders()
{
    static const juce::StringArray folders
    {
        "ROOMS  -  SMALL TO BIG",
        "TAPE & TIME",
        "DUB CHAMBER",
        "GHOST TOWN",
        "BROKEN MACHINE",
        "MOTION SICKNESS",
        "VOCAL BOOTH",
        "DRUM ROOM",
        "MIAMI AFTER DARK"
    };

    return folders;
}

// name, folder, size, rvTime, rvTone, rvMix, div, ms, feedback, dMix, filtOn, freq, q, sfxMask, sfxAmt
const std::vector<KissAudioProcessor::FactoryPreset>& KissAudioProcessor::getFactoryPresets()
{
    static const std::vector<FactoryPreset> p
    {
        // ---- 0  ROOMS ---------------------------------------------------------------------
        { "MATCHBOX",          0, 0,   6, 30, 14,  1,   0, 12, 16, 0, 1600, 1.2f,  0, 30 },
        { "TILE PRAYER",       0, 1,  18, 78, 26,  3,   0, 22, 22, 1, 3400, 1.6f,  0, 30 },
        { "BRICK BASEMENT",    0, 2,  32, 34, 30,  5,   0, 30, 26, 1, 1500, 1.1f,  0, 30 },
        { "STAIRWELL GHOST",   0, 3,  42, 62, 36,  4,   0, 48, 34, 1, 2600, 1.4f,  1, 28 },
        { "VELVET CHAPEL",     0, 4,  55, 34, 42,  7,   0, 40, 26, 1, 1200, 0.9f,  0, 30 },
        { "MARBLE BALLROOM",   0, 5,  64, 72, 46,  7,   0, 46, 30, 0, 1600, 1.2f,  2, 22 },
        { "CANYON HOLLER",     0, 6,  74, 46, 52,  8,   0, 62, 44, 1,  900, 0.8f,  0, 30 },
        { "CATHEDRAL DUST",    0, 7,  84, 26, 58,  8,   0, 58, 38, 1,  700, 1.1f, 16, 18 },
        { "ICE CAVERN",        0, 8,  92, 92, 64,  5,   0, 70, 48, 1, 5200, 2.2f,  5, 32 },
        { "THE VOID",          0, 9, 100, 18, 76, -1, 900, 84, 60, 1,  420, 1.8f, 44, 55 },

        // ---- 1  TAPE & TIME ---------------------------------------------------------------
        { "SLAPBACK DINER",    1, 1,  14, 62, 16,  1,   0, 10, 32, 1, 2800, 1.0f,  0, 30 },
        { "QUARTER-INCH SUN",  1, 3,  38, 54, 30,  3,   0, 34, 30, 1, 2200, 0.9f,  1, 22 },
        { "WOW & FLUTTER",     1, 3,  34, 42, 28,  5,   0, 40, 34, 1, 1900, 1.1f, 32, 34 },
        { "SATURDAY REEL",     1, 4,  46, 48, 34,  5,   0, 46, 32, 1, 1700, 1.0f,  1, 26 },
        { "BOUNCE DOWN",       1, 2,  26, 38, 22,  3,   0, 28, 26, 1, 1400, 1.3f, 16, 14 },
        { "DUSTY HEAD",        1, 4,  44, 24, 32,  5,   0, 52, 36, 1,  850, 1.5f, 16, 22 },
        { "PRINT THROUGH",     1, 5,  58, 30, 40,  7,   0, 64, 42, 1,  780, 1.2f, 33, 30 },
        { "HALF-SPEED",        1, 6,  66, 22, 44, -1,1200, 72, 46, 1,  520, 1.0f, 32, 46 },
        { "LAST GENERATION",   1, 7,  72, 16, 50,  8,   0, 80, 52, 1,  420, 1.7f, 48, 40 },

        // ---- 2  DUB CHAMBER ---------------------------------------------------------------
        { "THE KETTLE",        2, 5,  54, 44, 36,  5,   0, 78, 48, 1, 1100, 2.4f,  0, 30 },
        { "ECHO GUN",          2, 4,  44, 58, 32,  3,   0, 84, 54, 1, 1600, 3.2f,  0, 30 },
        { "RIDDIM SMOKE",      2, 5,  50, 36, 38,  4,   0, 74, 50, 1,  900, 2.0f, 32, 26 },
        { "SPRING TANK",       2, 3,  36, 70, 34,  2,   0, 66, 44, 1, 2400, 3.6f,  2, 30 },
        { "DELAY THROW",       2, 6,  62, 50, 30,  7,   0, 90, 40, 1, 1300, 2.8f,  0, 30 },
        { "FOUNDATION",        2, 7,  70, 28, 44,  8,   0, 80, 52, 1,  560, 1.6f, 16, 20 },
        { "SIREN STREET",      2, 6,  60, 66, 42,  4,   0, 88, 58, 1, 2000, 4.5f, 12, 44 },

        // ---- 3  GHOST TOWN ----------------------------------------------------------------
        { "SLOW WEATHER",      3, 6,  68, 40, 48,  7,   0, 54, 36, 1, 1000, 0.9f,  1, 24 },
        { "TAPE HALO",         3, 5,  60, 56, 44,  5,   0, 48, 32, 1, 2100, 1.1f,  1, 30 },
        { "DISTANT CHOIR",     3, 7,  78, 62, 54,  8,   0, 50, 34, 1, 2600, 1.0f,  5, 26 },
        { "EMPTY POOL",        3, 6,  66, 82, 50,  5,   0, 58, 40, 1, 4200, 1.8f,  0, 30 },
        { "LONG GOODBYE",      3, 8,  88, 44, 60,  8,   0, 66, 42, 1, 1200, 1.2f,  1, 20 },
        { "SNOWFIELD",         3, 8,  90, 88, 58,  7,   0, 44, 30, 1, 6000, 1.4f,  4, 28 },
        { "THE LOBBY AT 3AM",  3, 5,  56, 34, 40,  5,   0, 52, 34, 1,  980, 1.0f,  2, 18 },
        { "DRIFTWOOD",         3, 7,  76, 30, 52, -1, 640, 62, 44, 1,  760, 1.3f, 33, 30 },
        { "SLOW BLOOM",        3, 8,  94, 58, 62,  8,   0, 34, 24, 1, 1900, 0.9f,  0, 30 },

        // ---- 4  BROKEN MACHINE ------------------------------------------------------------
        { "DIAL-UP",           4, 2,  24, 74, 26,  1,   0, 40, 40, 1, 3200, 3.0f, 24, 62 },
        { "BAD SOLDER",        4, 3,  32, 30, 30,  2,   0, 56, 44, 1,  680, 2.2f, 16, 70 },
        { "ROBOT FLU",         4, 4,  40, 52, 34,  3,   0, 62, 46, 1, 1500, 2.6f, 40, 58 },
        { "METAL MOUTH",       4, 3,  30, 86, 28,  1,   0, 48, 42, 1, 5400, 4.0f,  8, 66 },
        { "CARRIER LOST",      4, 5,  52, 24, 42,  5,   0, 72, 50, 1,  520, 1.9f, 48, 54 },
        { "FAX MACHINE",       4, 2,  22, 90, 24,  0,   0, 36, 38, 1, 7200, 5.0f, 24, 74 },
        { "SAMPLE CRIME",      4, 4,  44, 40, 36,  3,   0, 58, 46, 1, 1250, 2.0f, 16, 80 },
        { "DEAD BATTERY",      4, 6,  64, 18, 46, -1, 780, 76, 52, 1,  400, 1.5f, 32, 72 },

        // ---- 5  MOTION SICKNESS -----------------------------------------------------------
        { "SEASICK",           5, 4,  46, 48, 34,  5,   0, 44, 34, 1, 1700, 1.1f, 32, 48 },
        { "JET WASH",          5, 5,  54, 66, 38,  4,   0, 58, 40, 1, 2600, 1.6f,  2, 66 },
        { "SWIRL",             5, 4,  42, 58, 32,  3,   0, 40, 32, 1, 2000, 1.2f,  1, 52 },
        { "PHASE SHIFT",       5, 5,  50, 54, 36,  5,   0, 46, 36, 1, 1800, 1.3f,  4, 58 },
        { "TAPE WOBBLE",       5, 3,  36, 44, 30,  3,   0, 42, 34, 1, 1500, 1.0f, 32, 40 },
        { "LIQUID GLASS",      5, 6,  62, 78, 44,  7,   0, 52, 38, 1, 3600, 1.5f,  7, 44 },
        { "CAROUSEL",          5, 5,  56, 60, 40,  4,   0, 54, 40, 1, 2200, 1.4f,  5, 50 },
        { "TILT-A-WHIRL",      5, 7,  72, 50, 48,  6,   0, 68, 46, 1, 1400, 1.8f, 38, 62 },

        // ---- 6  VOCAL BOOTH ---------------------------------------------------------------
        { "AIR AROUND IT",     6, 3,  34, 70, 20,  3,   0, 18, 16, 1, 3800, 1.0f,  0, 30 },
        { "DOUBLE ME",         6, 1,  16, 62, 14,  0,   0,  8, 26, 1, 2900, 1.1f,  1, 24 },
        { "BACK OF THE ROOM",  6, 5,  56, 46, 38,  5,   0, 30, 22, 1, 1500, 0.9f,  0, 30 },
        { "RADIO PREACHER",    6, 4,  44, 34, 30,  5,   0, 56, 38, 1,  820, 2.6f, 16, 24 },
        { "WHISPER HALL",      6, 6,  68, 74, 46,  7,   0, 38, 26, 1, 4400, 1.2f,  1, 22 },
        { "AD-LIB THROW",      6, 4,  40, 56, 26,  4,   0, 72, 44, 1, 1900, 2.0f,  0, 30 },

        // ---- 7  DRUM ROOM -----------------------------------------------------------------
        { "GOLD PLATE",        7, 3,  40, 74, 26,  1,   0, 12, 16, 1, 3000, 1.3f,  0, 30 },
        { "SUNKEN PLATE",      7, 4,  52, 22, 34,  1,   0, 16, 20, 1,  820, 1.1f,  0, 30 },
        { "ROOM MIC",          7, 2,  26, 52, 20,  0,   0, 10, 14, 0, 1600, 1.2f,  0, 30 },
        { "TIGHT TILE",        7, 1,  14, 80, 16,  1,   0,  8, 12, 1, 4600, 1.6f,  0, 30 },
        { "TRAP TAIL",         7, 4,  48, 40, 30,  3,   0, 44, 32, 1, 1200, 1.4f,  0, 30 },
        { "BREAK ROOM",        7, 3,  34, 44, 26,  2,   0, 38, 30, 1, 1600, 1.1f, 16, 20 },
        { "BOOM BAP CHAMBER",  7, 4,  42, 30, 32,  3,   0, 36, 28, 1,  940, 1.3f, 16, 26 },

        // ---- 8  MIAMI AFTER DARK ----------------------------------------------------------
        { "CAUSEWAY",          8, 6,  64, 72, 42,  5,   0, 56, 40, 1, 2800, 1.2f,  1, 26 },
        { "HUMIDITY",          8, 7,  76, 36, 50,  7,   0, 60, 44, 1,  880, 1.0f, 32, 30 },
        { "NEON WINDOW",       8, 5,  52, 84, 38,  4,   0, 50, 38, 1, 5000, 2.0f,  4, 46 },
        { "4AM OCEAN DRIVE",   8, 6,  66, 58, 46,  6,   0, 68, 48, 1, 1600, 1.5f,  5, 38 },
        { "AFTERPARTY GARAGE", 8, 4,  44, 28, 34,  3,   0, 52, 36, 1,  760, 1.8f, 17, 34 },
        { "PALM SHADOW",       8, 5,  58, 64, 40,  5,   0, 42, 30, 1, 2400, 1.1f,  1, 20 },
    };

    return p;
}

// Ten that earn their place: one per job, not ten shades of the same sound. Held by name
// and looked up once, so reordering the table above can never silently point these elsewhere.
const juce::Array<int>& KissAudioProcessor::getFavouriteIndices()
{
    static const juce::Array<int> favs = []
    {
        const juce::StringArray wanted
        {
            "SLAPBACK DINER",   // the one you reach for on everything
            "BRICK BASEMENT",   // an honest small room
            "VELVET CHAPEL",    // an honest big one
            "AIR AROUND IT",    // vocals, without sounding like an effect
            "GOLD PLATE",       // drums, same
            "ECHO GUN",         // dub throw
            "LONG GOODBYE",     // ambient tail
            "SEASICK",          // movement
            "CAUSEWAY",         // the local one
            "THE VOID"          // the deep end
        };

        juce::Array<int> found;
        const auto& all = getFactoryPresets();

        for (const auto& name : wanted)
            for (int i = 0; i < (int) all.size(); ++i)
                if (name == all[(size_t) i].name)
                    found.add (i);

        return found;
    }();

    return favs;
}

void KissAudioProcessor::applyFactoryPreset (int index)
{
    const auto& all = getFactoryPresets();

    if (! juce::isPositiveAndBelow (index, (int) all.size()))
        return;

    const auto& p = all[(size_t) index];
    currentPreset.store (index);

    auto set = [this] (const juce::String& id, float plain)
    {
        if (auto* param = apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (plain));
    };

    set ("reverbTime", p.rvTime);
    set ("reverbTone", p.rvTone);
    set ("reverbMix",  p.rvMix);

    set ("delaySync", p.div >= 0 ? 1.0f : 0.0f);

    if (p.div >= 0)
        set ("delayDiv", (float) p.div);
    else
        set ("delayTime", p.ms);

    set ("delayFeedback", p.feedback);
    set ("delayMix",      p.dMix);

    set ("filterOn",   (float) p.filtOn);
    set ("filterFreq", p.freq);
    set ("filterQ",    p.q);

    // A preset speaks for every slot, so the ones it doesn't light up get switched off.
    for (int i = 0; i < numSfxSlots; ++i)
    {
        const bool on = (p.sfxMask & (1 << i)) != 0;
        set (sfxParamId (i, "On"), on ? 1.0f : 0.0f);

        if (on)
            set (sfxParamId (i, "Amt"), p.sfxAmt);
    }
}

void KissAudioProcessor::applyStateXml (const juce::XmlElement& xml)
{
    if (xml.hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (xml));
}

juce::File KissAudioProcessor::getUserPresetFolder()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Kiss").getChildFile ("Presets");
    dir.createDirectory();
    return dir;
}

juce::StringArray KissAudioProcessor::getUserPresetNames() const
{
    juce::StringArray names;

    for (const auto& f : getUserPresetFolder().findChildFiles (juce::File::findFiles, false, "*.kiss"))
        names.add (f.getFileNameWithoutExtension());

    names.sortNatural();
    return names;
}

bool KissAudioProcessor::saveUserPreset (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim());

    if (clean.isEmpty())
        return false;

    if (auto xml = apvts.copyState().createXml())
        return xml->writeTo (getUserPresetFolder().getChildFile (clean + ".kiss"));

    return false;
}

// Nothing is ever actually destroyed - the file moves into Presets/Archive, so a mis-click
// costs you a trip to the folder rather than the preset.
bool KissAudioProcessor::archiveUserPreset (const juce::String& name)
{
    const auto file = getUserPresetFolder().getChildFile (name + ".kiss");

    if (! file.existsAsFile())
        return false;

    auto archive = getUserPresetFolder().getChildFile ("Archive");
    archive.createDirectory();

    auto dest = archive.getChildFile (name + ".kiss");

    for (int i = 2; dest.existsAsFile() && i < 500; ++i)
        dest = archive.getChildFile (name + " " + juce::String (i) + ".kiss");

    return file.moveFileTo (dest);
}

bool KissAudioProcessor::loadUserPreset (const juce::String& name)
{
    const auto file = getUserPresetFolder().getChildFile (name + ".kiss");

    if (! file.existsAsFile())
        return false;

    if (auto xml = juce::XmlDocument::parse (file))
    {
        applyStateXml (*xml);
        return true;
    }

    return false;
}

//==================================================================================================
// Lifecycle
//==================================================================================================

KissAudioProcessor::KissAudioProcessor()
    : AudioProcessor (BusesProperties().withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    reverbTime    = apvts.getRawParameterValue ("reverbTime");
    reverbTone    = apvts.getRawParameterValue ("reverbTone");
    reverbMix     = apvts.getRawParameterValue ("reverbMix");

    delaySync     = apvts.getRawParameterValue ("delaySync");
    delayDiv      = apvts.getRawParameterValue ("delayDiv");
    delayTime     = apvts.getRawParameterValue ("delayTime");
    delayFeedback = apvts.getRawParameterValue ("delayFeedback");
    delayMix      = apvts.getRawParameterValue ("delayMix");

    filterOn      = apvts.getRawParameterValue ("filterOn");
    filterFreq    = apvts.getRawParameterValue ("filterFreq");
    filterQ       = apvts.getRawParameterValue ("filterQ");

    postOn        = apvts.getRawParameterValue ("postOn");
    postType      = apvts.getRawParameterValue ("postType");
    postFreq      = apvts.getRawParameterValue ("postFreq");
    postQ         = apvts.getRawParameterValue ("postQ");

    for (int i = 0; i < numSfxSlots; ++i)
    {
        sfxOn[i]     = apvts.getRawParameterValue (sfxParamId (i, "On"));
        sfxAmount[i] = apvts.getRawParameterValue (sfxParamId (i, "Amt"));
    }
}

bool KissAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

void KissAudioProcessor::SfxSlot::prepare (const juce::dsp::ProcessSpec& spec, double sampleRate)
{
    chorus.prepare (spec);
    chorus.reset();
    phaser.prepare (spec);
    phaser.reset();

    warp.setMaximumDelayInSamples (juce::roundToInt (sampleRate * 0.05) + 2);
    warp.prepare (spec);
    warp.reset();

    lfoPhase  = 0.0f;
    holdCount = 0.0f;
    holdValue[0] = holdValue[1] = 0.0f;
}

void KissAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    juce::dsp::ProcessSpec spec { sampleRate,
                                  (juce::uint32) samplesPerBlock,
                                  (juce::uint32) juce::jmax (1, getTotalNumOutputChannels()) };

    delayLine.setMaximumDelayInSamples (juce::roundToInt (sampleRate * maxDelayMs / 1000.0) + 1);
    delayLine.prepare (spec);
    delayLine.reset();

    delaySamples.reset (sampleRate, 0.05);
    delaySamples.setCurrentAndTargetValue ((float) (delayTime->load() * sampleRate / 1000.0));

    feedbackFilter.prepare (spec);
    feedbackFilter.setType (juce::dsp::StateVariableTPTFilterType::bandpass);
    feedbackFilter.reset();

    postFilter.prepare (spec);
    postFilter.reset();

    reverb.prepare (spec);
    reverb.reset();

    for (auto& s : slots)
        s.prepare (spec, sampleRate);
}

//==================================================================================================
// Audio
//==================================================================================================

void KissAudioProcessor::processSfxSlot (SfxSlot& slot, juce::AudioBuffer<float>& buffer,
                                         int kind, float amount)
{
    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> context (block);

    switch (kind)
    {
        case 0:   // CHORUS - slow, wide, no feedback
            slot.chorus.setRate (0.6f + amount * 1.4f);
            slot.chorus.setDepth (0.25f + amount * 0.6f);
            slot.chorus.setCentreDelay (12.0f);
            slot.chorus.setFeedback (0.0f);
            slot.chorus.setMix (amount * 0.8f);
            slot.chorus.process (context);
            break;

        case 1:   // FLANGER - the same box, short and hot
            slot.chorus.setRate (0.15f + amount * 0.8f);
            slot.chorus.setDepth (0.4f + amount * 0.55f);
            slot.chorus.setCentreDelay (2.2f);
            slot.chorus.setFeedback (juce::jlimit (-0.95f, 0.95f, 0.35f + amount * 0.55f));
            slot.chorus.setMix (0.5f);
            slot.chorus.process (context);
            break;

        case 2:   // PHASER
            slot.phaser.setRate (0.2f + amount * 2.5f);
            slot.phaser.setDepth (0.3f + amount * 0.7f);
            slot.phaser.setCentreFrequency (300.0f + amount * 900.0f);
            slot.phaser.setFeedback (juce::jlimit (-0.95f, 0.95f, amount * 0.7f));
            slot.phaser.setMix (0.5f + amount * 0.4f);
            slot.phaser.process (context);
            break;

        case 3:   // RING
        {
            const float inc = juce::MathConstants<float>::twoPi
                                * (40.0f + amount * 1200.0f) / (float) currentSampleRate;

            for (int i = 0; i < numSamples; ++i)
            {
                const float m = std::sin (slot.lfoPhase);
                slot.lfoPhase += inc;

                if (slot.lfoPhase > juce::MathConstants<float>::twoPi)
                    slot.lfoPhase -= juce::MathConstants<float>::twoPi;

                for (int ch = 0; ch < numChannels; ++ch)
                {
                    auto* d = buffer.getWritePointer (ch);
                    d[i] = d[i] * (1.0f - amount) + d[i] * m * amount;
                }
            }
            break;
        }

        case 4:   // CRUSH - bit depth down and sample rate down together
        {
            const float steps = std::pow (2.0f, juce::jmap (amount, 0.0f, 1.0f, 16.0f, 3.0f));
            const float hold  = 1.0f + amount * 40.0f;

            for (int i = 0; i < numSamples; ++i)
            {
                slot.holdCount += 1.0f;
                const bool refresh = slot.holdCount >= hold;

                if (refresh)
                    slot.holdCount = 0.0f;

                for (int ch = 0; ch < juce::jmin (numChannels, 2); ++ch)
                {
                    auto* d = buffer.getWritePointer (ch);

                    if (refresh)
                        slot.holdValue[ch] = std::round (d[i] * steps) / steps;

                    d[i] = d[i] * (1.0f - amount) + slot.holdValue[ch] * amount;
                }
            }
            break;
        }

        case 5:   // BONUS - a wobbling tape motor with a shimmer riding on it
        {
            const float inc   = juce::MathConstants<float>::twoPi
                                  * (0.3f + amount * 5.0f) / (float) currentSampleRate;
            const float depth = (float) (currentSampleRate * 0.0005) * (1.0f + amount * 12.0f);
            const float base  = (float) (currentSampleRate * 0.006);

            for (int i = 0; i < numSamples; ++i)
            {
                const float m = std::sin (slot.lfoPhase);
                slot.lfoPhase += inc;

                if (slot.lfoPhase > juce::MathConstants<float>::twoPi)
                    slot.lfoPhase -= juce::MathConstants<float>::twoPi;

                slot.warp.setDelay (juce::jmax (1.0f, base + m * depth));

                const float shimmer = 1.0f + 0.35f * amount * std::sin (slot.lfoPhase * 11.0f);

                for (int ch = 0; ch < numChannels; ++ch)
                {
                    auto* d = buffer.getWritePointer (ch);
                    slot.warp.pushSample (ch, d[i]);
                    d[i] = d[i] * (1.0f - amount) + slot.warp.popSample (ch) * shimmer * amount;
                }
            }
            break;
        }

        default: break;
    }
}

void KissAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples <= 0 || numChannels <= 0)
        return;

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
                hostBpm.store ((float) *bpm);

    // ---- 1. Reverb -------------------------------------------------------------------
    {
        const float t    = juce::jlimit (0.0f, 1.0f, reverbTime->load() * 0.01f);
        const float tone = juce::jlimit (0.0f, 1.0f, reverbTone->load() * 0.01f);
        const float wet  = juce::jlimit (0.0f, 1.0f, reverbMix->load()  * 0.01f);

        juce::Reverb::Parameters rp;
        rp.roomSize   = 0.22f + t * 0.77f;
        rp.damping    = 1.0f - tone;
        rp.width      = 0.25f + tone * 0.75f;
        rp.wetLevel   = wet;
        rp.dryLevel   = 1.0f - wet;
        rp.freezeMode = 0.0f;
        reverb.setParameters (rp);

        juce::dsp::AudioBlock<float> block (buffer);
        juce::dsp::ProcessContextReplacing<float> context (block);
        reverb.process (context);
    }

    // ---- 2. Delay --------------------------------------------------------------------
    {
        const float feedback = juce::jlimit (0.0f, 0.95f, delayFeedback->load() * 0.01f);
        const float mix      = juce::jlimit (0.0f, 1.0f,  delayMix->load()      * 0.01f);
        const bool  useSync  = delaySync->load() > 0.5f;
        const bool  useFilt  = filterOn->load()  > 0.5f;

        double targetMs = useSync ? divisionToMs ((int) delayDiv->load(), hostBpm.load())
                                  : (double) delayTime->load();

        targetMs = juce::jlimit (1.0, (double) maxDelayMs, targetMs);
        delaySamples.setTargetValue ((float) (targetMs * currentSampleRate / 1000.0));

        // A resonant band-pass peaks at its own resonance value, so the filter alone can
        // multiply a repeat several times over before feedback is applied. Compensating for
        // that is the difference between a filtered delay and a siren.
        float filtComp = 1.0f;

        if (useFilt)
        {
            const float q = juce::jlimit (0.05f, 20.0f, filterQ->load());
            feedbackFilter.setCutoffFrequency (juce::jlimit (60.0f, (float) (currentSampleRate * 0.45),
                                                             filterFreq->load()));
            feedbackFilter.setResonance (q);
            filtComp = 1.0f / juce::jmax (1.0f, q);
        }

        for (int i = 0; i < numSamples; ++i)
        {
            delayLine.setDelay (delaySamples.getNextValue());

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* data = buffer.getWritePointer (ch);

                const float dry     = data[i];
                const float delayed = delayLine.popSample (ch);
                const float shaped  = useFilt ? feedbackFilter.processSample (ch, delayed) * filtComp
                                              : delayed;

                // Second belt: the signal going back round is softly saturated, so the loop
                // has a ceiling it physically cannot climb past however hot the settings are.
                const float back = std::tanh (shaped * feedback * 1.25f) * 0.8f;

                delayLine.pushSample (ch, dry + back);

                data[i] = dry * (1.0f - mix) + shaped * mix;
            }
        }
    }

    // ---- 3. SFX slots, in series ------------------------------------------------------
    for (int i = 0; i < numSfxSlots; ++i)
    {
        const float amount = juce::jlimit (0.0f, 1.0f, sfxAmount[i]->load() * 0.01f);

        if (sfxOn[i]->load() > 0.5f && amount > 0.0001f)
            processSfxSlot (slots[i], buffer, i, amount);
    }

    // ---- 4. The master filter, across everything, last ---------------------------------
    if (postOn->load() > 0.5f)
    {
        using FT = juce::dsp::StateVariableTPTFilterType;
        const int type = juce::jlimit (0, 2, (int) postType->load());

        postFilter.setType (type == 0 ? FT::lowpass : (type == 1 ? FT::bandpass : FT::highpass));
        postFilter.setCutoffFrequency (juce::jlimit (20.0f, (float) (currentSampleRate * 0.45),
                                                     postFreq->load()));
        postFilter.setResonance (juce::jlimit (0.05f, 20.0f, postQ->load()));

        for (int i = 0; i < numSamples; ++i)
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* d = buffer.getWritePointer (ch);
                d[i] = postFilter.processSample (ch, d[i]);
            }
    }

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* d = buffer.getWritePointer (ch);

        for (int i = 0; i < numSamples; ++i)
        {
            const float x = d[i];

            if (! std::isfinite (x)) { d[i] = 0.0f; continue; }

            const float a = std::abs (x);

            if (a > 0.7f)
            {
                const float sign = x < 0.0f ? -1.0f : 1.0f;
                d[i] = sign * (0.7f + 0.3f * std::tanh ((a - 0.7f) / 0.3f));
            }
        }
    }
}

//==================================================================================================

void KissAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void KissAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        applyStateXml (*xml);
}

juce::AudioProcessorEditor* KissAudioProcessor::createEditor()
{
    return new KissAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KissAudioProcessor();
}

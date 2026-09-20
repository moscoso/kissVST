#include "PluginEditor.h"

extern const juce::StringArray kDivisionNames;
extern const juce::StringArray kSfxNames;
extern const juce::StringArray kThemeNames;
extern const juce::StringArray kPostTypeNames;
juce::String sfxParamId (int slot, const char* suffix);

namespace
{
    // One fixed surface, scaled to whatever size the window is dragged to.
    constexpr int designW = 1000, designH = 616;

    constexpr int knobBox      = 128;   // the slider's box; the ring is drawn 14px inside it
    constexpr int smallKnobBox = 74;
    constexpr int knobPad      = 14;    // room for the halo, so it never squares off
    constexpr int repeatPanelW = 156;

    float readParam (KissAudioProcessor& p, const juce::String& id)
    {
        if (auto* v = p.apvts.getRawParameterValue (id)) return v->load();
        return 0.0f;
    }

    void writeParam (KissAudioProcessor& p, const juce::String& id, float plain)
    {
        if (auto* param = p.apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (plain));
    }

    float curveGain (int type, float freq, float centre, float q)
    {
        const float r = freq / centre;

        if (type == 1)                                   // band pass
        {
            const float x = r - 1.0f / r;
            return 1.0f / std::sqrt (1.0f + q * q * x * x);
        }

        const float n = type == 0 ? r : 1.0f / r;        // low pass / high pass
        const float order = 1.0f + q * 0.35f;
        return 1.0f / std::sqrt (1.0f + std::pow (n, 2.0f * order));
    }
}

static juce::Font kissFont (float h, bool bold)
{
    return juce::Font (juce::FontOptions().withHeight (h).withStyle (bold ? "Bold" : "Regular"));
}

//==================================================================================================
// Themes
//==================================================================================================

const KissTheme& getKissTheme (int index)
{
    using A = KissTheme::Anim;

    static const KissTheme themes[]
    {
        { juce::Colour (0xff070a12), juce::Colour (0xff10151f), juce::Colour (0xffe7ebf1), juce::Colour (0xff5b6472), juce::Colour (0xff6ea8fe), juce::Colour (0xffcfe2ff), A::Stars },
        { juce::Colour (0xff0a0612), juce::Colour (0xff170c28), juce::Colour (0xfff0e8ff), juce::Colour (0xff6b5a8c), juce::Colour (0xffff2fd0), juce::Colour (0xff00e5ff), A::None },
        { juce::Colour (0xff05120f), juce::Colour (0xff0b201b), juce::Colour (0xffd8fff0), juce::Colour (0xff4d7d6d), juce::Colour (0xff2fffb0), juce::Colour (0xff2fffb0), A::Drip },
        { juce::Colour (0xff140420), juce::Colour (0xff230934), juce::Colour (0xffffe9ff), juce::Colour (0xff7d5a96), juce::Colour (0xffffc400), juce::Colour (0xffff4ecd), A::Trip },
        { juce::Colour (0xfff2efe6), juce::Colour (0xffe3ded1), juce::Colour (0xff23201a), juce::Colour (0xff8d8676), juce::Colour (0xffc4452f), juce::Colour (0xffc4452f), A::None },

        // --- elemental ---
        { juce::Colour (0xff04121e), juce::Colour (0xff0a2033), juce::Colour (0xffdff2ff), juce::Colour (0xff4c7a9c), juce::Colour (0xff3fc9ff), juce::Colour (0xff7fe7ff), A::Bubbles },
        { juce::Colour (0xff180502), juce::Colour (0xff2c0a03), juce::Colour (0xffffe0c2), juce::Colour (0xff9c5330), juce::Colour (0xffff5a0a), juce::Colour (0xffffd257), A::Embers },
        { juce::Colour (0xff0b1114), juce::Colour (0xff141e23), juce::Colour (0xffe4f2f6), juce::Colour (0xff62808b), juce::Colour (0xffa9d8e8), juce::Colour (0xffd6f0fa), A::Wind  },
    };

    return themes[juce::jlimit (0, 7, index)];
}

int getNumKissThemes() { return 8; }

//==================================================================================================
// Knob look
//==================================================================================================

void KissLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                        float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                        juce::Slider& slider)
{
    const float fade = slider.isEnabled() ? 1.0f : 0.3f;

    // The padding is what keeps the halo from being clipped into a rectangle by its own box.
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced ((float) knobPad);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto thick  = juce::jmax (3.0f, radius * 0.16f);
    const auto angle  = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    const auto ringR  = radius - thick * 0.6f;

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, ringR, ringR, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (theme.accent.withAlpha (0.16f * fade));
    g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    if (sliderPos > 0.001f)
    {
        juce::Path filled;
        filled.addCentredArc (centre.x, centre.y, ringR, ringR, 0.0f, rotaryStartAngle, angle, true);
        g.setColour (theme.accent.withAlpha (fade));
        g.strokePath (filled, juce::PathStrokeType (thick, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
    }

    const auto dotR   = thick * 0.95f;
    const auto dotPos = centre.getPointOnCircumference (ringR, angle);

    // The halo answers to the value: nothing at zero, a real glow wide open. Six thin
    // layers instead of four, so it fades out instead of stopping.
    const float amt = juce::jlimit (0.0f, 1.0f, sliderPos);

    if (amt > 0.01f)
    {
        for (int i = 6; i >= 1; --i)
        {
            const float spread = dotR * (1.0f + (float) i * (0.45f + amt * 0.62f));

            if (spread > (float) knobPad + dotR * 2.2f)
                continue;                                    // never paint past the padding

            g.setColour (theme.glow.withAlpha (0.10f * amt / (float) i * fade));
            g.fillEllipse (dotPos.x - spread, dotPos.y - spread, spread * 2.0f, spread * 2.0f);
        }
    }

    // A tight bloom right at the dot, so the grab point reads from across the room.
    g.setColour (theme.glow.withAlpha ((0.16f + 0.22f * amt) * fade));
    g.fillEllipse (dotPos.x - dotR * 2.1f, dotPos.y - dotR * 2.1f, dotR * 4.2f, dotR * 4.2f);

    g.setColour (theme.glow.brighter (0.45f).withAlpha ((0.6f + 0.4f * amt) * fade));
    g.fillEllipse (dotPos.x - dotR, dotPos.y - dotR, dotR * 2.0f, dotR * 2.0f);
}

juce::Font KissLookAndFeel::getLabelFont (juce::Label&) { return kissFont (11.5f, false); }

//==================================================================================================
// Filter display
//==================================================================================================

FilterDisplay::FilterDisplay (KissAudioProcessor& p, juce::String onIdIn, juce::String freqIdIn,
                              juce::String qIdIn, juce::String typeIdIn, juce::String captionIn,
                              float lo, float hi)
    : processor (p), onId (std::move (onIdIn)), freqId (std::move (freqIdIn)),
      qId (std::move (qIdIn)), typeId (std::move (typeIdIn)), caption (std::move (captionIn)),
      loHz (lo), hiHz (hi)
{
    setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
}

void FilterDisplay::refresh()
{
    const float f = readParam (processor, freqId);
    const float q = readParam (processor, qId);
    const float t = typeId.isEmpty() ? 1.0f : readParam (processor, typeId);
    const bool  o = readParam (processor, onId) > 0.5f;

    if (! juce::approximatelyEqual (f, lastFreq) || ! juce::approximatelyEqual (q, lastQ)
        || ! juce::approximatelyEqual (t, lastType) || o != lastOn)
    {
        lastFreq = f; lastQ = q; lastType = t; lastOn = o;
        repaint();
    }
}

void FilterDisplay::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat().reduced (1.0f);

    g.setColour (theme.panel);
    g.fillRoundedRectangle (area, 5.0f);

    auto header = area.removeFromTop (17.0f);
    auto plot   = area.reduced (6.0f, 4.0f);
    const bool on = lastOn;

    g.setColour (on ? theme.accent : theme.dim);
    g.setFont (kissFont (9.5f, true));
    g.drawText (caption + (on ? "  ON" : "  OFF"), header.reduced (7.0f, 0.0f),
                juce::Justification::centredLeft);

    g.setColour (theme.dim.withAlpha (0.55f));
    g.setFont (kissFont (8.5f, false));
    g.drawText ("DRAG", header.reduced (7.0f, 0.0f), juce::Justification::centredRight);

    g.setColour (theme.dim.withAlpha (0.26f));

    for (int i = 1; i < 4; ++i)
        g.drawVerticalLine ((int) (plot.getX() + plot.getWidth() * (float) i / 4.0f),
                            plot.getY(), plot.getBottom());

    g.setColour (theme.dim.withAlpha (0.45f));
    g.drawRoundedRectangle (plot, 3.0f, 0.9f);

    const int   type   = typeId.isEmpty() ? 1 : juce::jlimit (0, 2, (int) lastType);
    const float centre = juce::jlimit (loHz, hiHz, lastFreq);
    const float q      = juce::jmax (0.05f, lastQ);
    const float ratio  = hiHz / loHz;

    juce::Path curve, fill;
    bool started = false;

    for (float px = 0.0f; px <= plot.getWidth(); px += 2.0f)
    {
        const float f = loHz * std::pow (ratio, px / plot.getWidth());
        const float py = plot.getBottom() - curveGain (type, f, centre, q) * (plot.getHeight() - 5.0f) - 2.5f;

        if (! started)
        {
            curve.startNewSubPath (plot.getX() + px, py);
            fill.startNewSubPath (plot.getX() + px, plot.getBottom());
            fill.lineTo (plot.getX() + px, py);
            started = true;
        }
        else { curve.lineTo (plot.getX() + px, py); fill.lineTo (plot.getX() + px, py); }
    }

    fill.lineTo (plot.getRight(), plot.getBottom());
    fill.closeSubPath();

    g.setColour ((on ? theme.accent : theme.dim).withAlpha (on ? 0.20f : 0.08f));
    g.fillPath (fill);
    g.setColour (on ? theme.accent : theme.dim.withAlpha (0.55f));
    g.strokePath (curve, juce::PathStrokeType (on ? 1.9f : 1.2f));

    if (on)
    {
        const float px = plot.getX() + plot.getWidth() * std::log (centre / loHz) / std::log (ratio);
        const float py = plot.getBottom()
                           - curveGain (type, centre, centre, q) * (plot.getHeight() - 5.0f) - 2.5f;

        g.setColour (theme.glow.withAlpha (0.16f));
        g.drawVerticalLine ((int) px, plot.getY(), plot.getBottom());
        g.setColour (theme.glow.withAlpha (0.32f));
        g.fillEllipse (px - 5.5f, py - 5.5f, 11.0f, 11.0f);
        g.setColour (theme.glow.brighter (0.4f));
        g.fillEllipse (px - 2.6f, py - 2.6f, 5.2f, 5.2f);
    }
}

void FilterDisplay::mouseDown (const juce::MouseEvent&)
{
    startFreq = readParam (processor, freqId);
    dragged = false;
}

void FilterDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (e.getDistanceFromDragStart() > 3) dragged = true;
    if (! dragged) return;

    const float perPixel = std::log (hiHz / loHz) / juce::jmax (40.0f, (float) getWidth());
    writeParam (processor, freqId,
                juce::jlimit (loHz, hiHz,
                              startFreq * std::exp (perPixel * (float) e.getDistanceFromDragStartX())));

    if (onFrequencyMoved) onFrequencyMoved();
}

void FilterDisplay::mouseUp (const juce::MouseEvent&)
{
    if (! dragged)
        writeParam (processor, onId, readParam (processor, onId) > 0.5f ? 0.0f : 1.0f);
}

//==================================================================================================
// Q handle
//==================================================================================================

QHandle::QHandle (KissAudioProcessor& p, juce::String onIdIn, juce::String qIdIn)
    : processor (p), onId (std::move (onIdIn)), qId (std::move (qIdIn))
{
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

void QHandle::refresh()
{
    const float q = readParam (processor, qId);

    if (! juce::approximatelyEqual (q, lastQ)) { lastQ = q; repaint(); }
}

void QHandle::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat().reduced (1.0f);
    const bool on = readParam (processor, onId) > 0.5f;

    g.setColour (theme.panel);
    g.fillRoundedRectangle (area, 4.0f);
    g.setColour ((on ? theme.accent : theme.dim).withAlpha (0.45f));
    g.drawRoundedRectangle (area, 4.0f, 1.0f);

    g.setColour (on ? theme.accent : theme.dim);
    g.setFont (kissFont (10.5f, true));
    g.drawText ("Q  " + juce::String (juce::jmax (0.0f, lastQ), 2),
                area.reduced (9.0f, 0.0f), juce::Justification::centredLeft);

    g.setColour (theme.dim.withAlpha (0.7f));
    g.setFont (kissFont (8.5f, false));
    g.drawText ("DRAG UP / DOWN", area.reduced (9.0f, 0.0f), juce::Justification::centredRight);
}

void QHandle::mouseDown (const juce::MouseEvent&) { startQ = readParam (processor, qId); }

void QHandle::mouseDrag (const juce::MouseEvent& e)
{
    writeParam (processor, qId,
                juce::jlimit (0.30f, 8.0f,
                              startQ * std::exp (-(float) e.getDistanceFromDragStartY() * 0.012f)));

    if (onQMoved) onQMoved();
}

//==================================================================================================
// Preset menu row
//==================================================================================================

PresetMenuItem::PresetMenuItem (juce::String n, int s, const KissTheme& t, bool trash)
    : name (std::move (n)), chamberSize (s), theme (t), showTrash (trash) {}

juce::Rectangle<int> PresetMenuItem::trashArea() const
{
    return getLocalBounds().removeFromRight (34);
}

void PresetMenuItem::mouseDown (const juce::MouseEvent& e)
{
    if (showTrash && trashArea().contains (e.getPosition()) && onTrash)
        onTrash();

    triggerMenuItem();
}

void PresetMenuItem::getIdealSize (int& w, int& h) { w = 262; h = 32; }

void PresetMenuItem::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();

    if (isItemHighlighted())
    {
        g.setColour (theme.accent.withAlpha (0.18f));
        g.fillRoundedRectangle (area.toFloat().reduced (2.0f), 4.0f);
    }

    auto room = area.removeFromLeft (46).reduced (7, 7).toFloat();
    g.setColour (theme.dim.withAlpha (0.9f));
    g.drawRoundedRectangle (room, 2.0f, 1.1f);

    {
        juce::Graphics::ScopedSaveState clipToRoom (g);
        g.reduceClipRegion (room.toNearestInt());

        const auto src = juce::Point<float> (room.getX() + 5.0f, room.getCentreY());
        const int rings = 2 + chamberSize / 2;
        const float step = (room.getWidth() - 6.0f) / (float) rings;

        for (int i = 0; i < rings; ++i)
        {
            const float r = 4.0f + step * (float) i;
            juce::Path arc;
            arc.addCentredArc (src.x, src.y, r, r, 0.0f,
                               juce::MathConstants<float>::halfPi * 0.35f,
                               juce::MathConstants<float>::halfPi * 1.65f, true);
            g.setColour (theme.accent.withAlpha (0.92f - (float) i * 0.12f));
            g.strokePath (arc, juce::PathStrokeType (1.25f));
        }

        g.setColour (theme.accent);
        g.fillEllipse (src.x - 1.5f, src.y - 1.5f, 3.0f, 3.0f);
    }

    if (showTrash)
    {
        auto bin = area.removeFromRight (34).toFloat().reduced (10.0f, 8.0f);

        g.setColour (theme.dim.withAlpha (isItemHighlighted() ? 0.95f : 0.6f));
        g.drawRoundedRectangle (bin.withTrimmedTop (3.0f), 1.5f, 1.2f);          // body
        g.fillRect (bin.getX() - 1.0f, bin.getY() + 2.0f, bin.getWidth() + 2.0f, 1.4f);  // lid
        g.fillRect (bin.getCentreX() - 2.5f, bin.getY() - 1.0f, 5.0f, 2.0f);      // handle

        for (int i = 0; i < 2; ++i)
            g.drawVerticalLine ((int) (bin.getX() + bin.getWidth() * (i == 0 ? 0.35f : 0.65f)),
                                bin.getY() + 7.0f, bin.getBottom() - 2.5f);
    }

    g.setColour (theme.text);
    g.setFont (kissFont (12.5f, true));
    g.drawText (name, area.reduced (6, 0), juce::Justification::centredLeft, true);
}

//==================================================================================================
// Editor
//==================================================================================================

KissAudioProcessorEditor::KissAudioProcessorEditor (KissAudioProcessor& p)
    : AudioProcessorEditor (p), processor (p),
      repeatFilter (p, "filterOn", "filterFreq", "filterQ", {}, "REPEAT FILTER", 120.0f, 12000.0f),
      repeatQ (p, "filterOn", "filterQ"),
      postFilter (p, "postOn", "postFreq", "postQ", "postType", "MASTER FILTER", 30.0f, 18000.0f),
      postQ (p, "postOn", "postQ")
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (surface);

    auto add = [this] (juce::Component& c) { surface.addAndMakeVisible (c); };

    reverbGroup.setText ("REVERB");
    delayGroup.setText ("DELAY      T = TRIPLET");
    sfxGroup.setText ("SFX      SIX PEDALS, STACK AS MANY AS YOU LIKE");
    postGroup.setText ("MASTER FILTER      LAST IN THE CHAIN, ACROSS EVERYTHING");
    add (reverbGroup); add (delayGroup); add (sfxGroup); add (postGroup);

    addKnob (reverbTime, "reverbTime", "TIME");
    addKnob (reverbTone, "reverbTone", "TONE");
    addKnob (reverbMix,  "reverbMix",  "DRY / WET");
    addKnob (delayTimeKnob, "delayTime",     "TIME");
    addKnob (delayFeedback, "delayFeedback", "FEEDBACK");
    addKnob (delayMix,      "delayMix",      "DRY / WET");

    delayTimeKnob.slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);

    add (divisionButton);  divisionButton.onClick = [this] { cycleDivision(); };
    syncButton.setClickingTogglesState (true);
    add (syncButton);
    syncAttachment = std::make_unique<ButtonAttachment> (processor.apvts, "delaySync", syncButton);

    add (delayReadout);   delayReadout.setJustificationType (juce::Justification::centred);

    add (repeatFilter); add (repeatQ); add (repeatReadout);
    repeatReadout.setJustificationType (juce::Justification::centred);
    repeatFilter.onFrequencyMoved = [this] { ripple (repeatFilter, 0.45f); };
    repeatQ.onQMoved              = [this] { ripple (repeatQ, 0.45f); };

    add (postFilter); add (postQ); add (postReadout); add (postTypeButton); add (postChainNote);
    postReadout.setJustificationType (juce::Justification::centred);
    postTypeButton.onClick = [this] { cyclePostType(); };
    postFilter.onFrequencyMoved = [this] { ripple (postFilter, 0.45f); };
    postQ.onQMoved              = [this] { ripple (postQ, 0.45f); };
    postChainNote.setText ("REVERB  >  DELAY  >  SFX  >  HERE", juce::dontSendNotification);
    postChainNote.setJustificationType (juce::Justification::centredLeft);

    for (int i = 0; i < numSfxSlots; ++i)
    {
        auto& s = sfxSlots[i];
        s.button.setButtonText (kSfxNames[i]);
        s.button.setClickingTogglesState (true);
        add (s.button);
        s.attachment = std::make_unique<ButtonAttachment> (processor.apvts, sfxParamId (i, "On"), s.button);

        addKnob (s.amount, sfxParamId (i, "Amt"), "");
        s.amount.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 58, 15);
    }

    add (prevPreset);   prevPreset.onClick = [this] { stepPreset (-1); };
    add (nextPreset);   nextPreset.onClick = [this] { stepPreset (+1); };
    add (presetName);
    presetName.setJustificationType (juce::Justification::centredRight);

    add (themeButton);  themeButton.onClick  = [this] { cycleTheme(); };
    add (presetButton); presetButton.onClick = [this] { showPresetMenu(); };
    add (saveButton);   saveButton.onClick   = [this] { promptSavePreset(); };

    seedParticles();
    applyTheme();
    refreshPresetName();
    refreshFromParameters();

    // Resizable, with the surface scaled to fit - so nothing can drift out of place.
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) designW / (double) designH);
    setResizeLimits (designW / 2, designH / 2, designW * 2, designH * 2);
    setSize (designW, designH);

    startTimerHz (30);
}

KissAudioProcessorEditor::~KissAudioProcessorEditor() { setLookAndFeel (nullptr); }

void KissAudioProcessorEditor::addKnob (Knob& k, const juce::String& parameterID,
                                        const juce::String& captionText)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 17);
    surface.addAndMakeVisible (k.slider);

    // Every knob move sends a wave out across the panel from where it happened.
    k.slider.onValueChange = [this, sl = &k.slider]
    {
        const auto range = sl->getRange();
        const float span = (float) (range.getLength());
        ripple (*sl, span > 0.0f ? (float) ((sl->getValue() - range.getStart()) / span) : 0.5f);
    };

    if (captionText.isNotEmpty())
    {
        k.caption.setText (captionText, juce::dontSendNotification);
        k.caption.setJustificationType (juce::Justification::centred);
        surface.addAndMakeVisible (k.caption);
    }

    k.attachment = std::make_unique<SliderAttachment> (processor.apvts, parameterID, k.slider);
}

void KissAudioProcessorEditor::layoutKnob (Knob& k, juce::Rectangle<int> cell, int boxSize)
{
    if (k.caption.isVisible())
        k.caption.setBounds (cell.removeFromTop (18));

    const int textBox = k.slider.getTextBoxHeight();
    auto block = cell.removeFromTop (boxSize + textBox);
    k.slider.setBounds (block.withSizeKeepingCentre (juce::jmin (boxSize, block.getWidth()),
                                                     boxSize + textBox));
}

// How far the wave goes is the knob's own value: a nudge off zero barely leaves the knob,
// halfway crosses the panel, wide open runs off the edge of the window.
void KissAudioProcessorEditor::ripple (juce::Component& from, float amount01)
{
    const float a = juce::jlimit (0.0f, 1.0f, amount01);
    rippleAt    = from.getBounds().toFloat().getCentre();
    rippleReach = 40.0f + a * a * 1500.0f;
    rippleT     = 0.0f;
}

//==================================================================================================

void KissAudioProcessorEditor::resized()
{
    // Scale the fixed surface into whatever the window has become.
    const float s = juce::jmin ((float) getWidth() / (float) designW,
                                (float) getHeight() / (float) designH);
    surface.setTransform (juce::AffineTransform::scale (s));
    surface.setBounds (0, 0, designW, designH);

    auto area = juce::Rectangle<int> (0, 0, designW, designH).reduced (12);

    auto top = area.removeFromTop (34);
    top.removeFromLeft (150);
    themeButton.setBounds (top.removeFromRight (112).reduced (2));
    top.removeFromRight (6);
    saveButton.setBounds (top.removeFromRight (66).reduced (2));
    top.removeFromRight (6);
    presetButton.setBounds (top.removeFromRight (132).reduced (2));

    // The stepper sits under PRESETS and says where you actually are in the list.
    auto navRow = area.removeFromTop (22);
    nextPreset.setBounds (navRow.removeFromRight (26).reduced (1));
    navRow.removeFromRight (4);
    prevPreset.setBounds (navRow.removeFromRight (26).reduced (1));
    navRow.removeFromRight (8);
    presetName.setBounds (navRow.removeFromRight (420));

    area.removeFromTop (6);

    // ---- master filter, pinned to the bottom because that is where it sits in the chain --
    auto postArea = area.removeFromBottom (142);
    area.removeFromBottom (8);
    postGroup.setBounds (postArea);
    {
        auto inner = postArea.reduced (12).withTrimmedTop (14);
        auto right = inner.removeFromRight (230);
        inner.removeFromRight (12);
        postFilter.setBounds (inner);

        postTypeButton.setBounds (right.removeFromTop (28));
        right.removeFromTop (8);
        postQ.setBounds (right.removeFromTop (26));
        right.removeFromTop (6);
        postReadout.setBounds (right.removeFromTop (18));
        right.removeFromTop (4);
        postChainNote.setBounds (right.removeFromTop (16));
    }

    // ---- sfx ------------------------------------------------------------------------------
    auto sfxArea = area.removeFromBottom (26 + 4 + smallKnobBox + 15 + 24);
    area.removeFromBottom (8);
    sfxGroup.setBounds (sfxArea);
    {
        auto inner = sfxArea.reduced (10).withTrimmedTop (14);
        const int w = inner.getWidth() / numSfxSlots;

        for (int i = 0; i < numSfxSlots; ++i)
        {
            auto cell = inner.removeFromLeft (w).reduced (5, 0);
            sfxSlots[i].button.setBounds (cell.removeFromTop (26));
            cell.removeFromTop (4);
            layoutKnob (sfxSlots[i].amount, cell, smallKnobBox);
        }
    }

    // ---- reverb / delay ---------------------------------------------------------------------
    const int gap = 12;
    const int reverbW = (area.getWidth() - gap - repeatPanelW) / 2;

    auto reverbArea = area.removeFromLeft (reverbW);
    area.removeFromLeft (gap);
    auto delayArea = area;

    reverbGroup.setBounds (reverbArea);
    delayGroup.setBounds (delayArea);

    {
        auto inner = reverbArea.reduced (10).withTrimmedTop (14);
        const int w = inner.getWidth() / 3;
        layoutKnob (reverbTime, inner.removeFromLeft (w), knobBox);
        layoutKnob (reverbTone, inner.removeFromLeft (w), knobBox);
        layoutKnob (reverbMix,  inner, knobBox);
    }

    {
        auto inner = delayArea.reduced (10).withTrimmedTop (14);

        auto filterCell = inner.removeFromRight (repeatPanelW - 20);
        repeatFilter.setBounds (filterCell.removeFromTop (108));
        filterCell.removeFromTop (6);
        repeatQ.setBounds (filterCell.removeFromTop (26));
        filterCell.removeFromTop (4);
        repeatReadout.setBounds (filterCell.removeFromTop (16));

        const int w = inner.getWidth() / 3;

        auto timeCell = inner.removeFromLeft (w);
        layoutKnob (delayTimeKnob, timeCell.removeFromTop (18 + knobBox), knobBox);

        auto under = timeCell;
        syncButton.setBounds (under.removeFromTop (20).reduced (16, 1));
        delayReadout.setBounds (under.removeFromTop (16));

        const auto ring = delayTimeKnob.slider.getBounds();
        divisionButton.setBounds (juce::Rectangle<int> (0, 0, 70, 24).withCentre (ring.getCentre()));

        layoutKnob (delayFeedback, inner.removeFromLeft (w), knobBox);
        layoutKnob (delayMix,      inner.removeFromLeft (w), knobBox);
    }
}

//==================================================================================================

void KissAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme.background);
    paintBackdrop (g);
}

// The ripple and the wordmark ride on the surface, so they scale with everything else.
void KissAudioProcessorEditor::Surface::paint (juce::Graphics& g)
{
    auto& e = owner;

    if (e.rippleT >= 0.0f && e.rippleT < 1.0f)
    {
        const float t = e.rippleT;

        for (int i = 0; i < 3; ++i)
        {
            const float lag = t - (float) i * 0.13f;

            if (lag <= 0.0f) continue;

            const float r = lag * e.rippleReach;
            const float a = (1.0f - lag) * 0.16f / (1.0f + (float) i);
            g.setColour (e.theme.glow.withAlpha (juce::jmax (0.0f, a)));
            g.drawEllipse (e.rippleAt.x - r, e.rippleAt.y - r, r * 2.0f, r * 2.0f, 1.6f);
        }
    }

    g.setColour (e.theme.text);
    g.setFont (kissFont (23.0f, true));
    g.drawText ("KISS", 14, 12, 120, 30, juce::Justification::centredLeft);

    g.setColour (e.theme.dim);
    g.setFont (kissFont (9.5f, false));
    g.drawText ("KEEP IT SIMPLE", 76, 20, 140, 14, juce::Justification::centredLeft);
}

void KissAudioProcessorEditor::seedParticles()
{
    parts.clearQuick();

    for (int i = 0; i < 34; ++i)
        parts.add ({ rng.nextFloat(), rng.nextFloat(),
                     (rng.nextFloat() - 0.5f) * 0.6f, 0.2f + rng.nextFloat() * 0.9f,
                     4.0f + rng.nextFloat() * 42.0f, 0.10f + rng.nextFloat() * 0.26f,
                     rng.nextFloat() * 6.28f, (rng.nextFloat() - 0.5f) * 0.02f });
}

void KissAudioProcessorEditor::paintBackdrop (juce::Graphics& g)
{
    const float w = (float) getWidth(), h = (float) getHeight();
    const auto A = theme.anim;

    if (A == KissTheme::Anim::None) return;

    if (A == KissTheme::Anim::Trip)
    {
        for (int band = 0; band < 16; ++band)
        {
            const float t = (float) band / 16.0f, yy = t * h;
            juce::Path p;
            p.startNewSubPath (0.0f, yy);

            for (float x = 0.0f; x <= w; x += 14.0f)
                p.lineTo (x, yy + std::sin (x * 0.014f + animPhase * 1.6f + t * 6.0f) * (10.0f + t * 22.0f));

            g.setColour (theme.accent.withRotatedHue (t * 0.6f + animPhase * 0.05f).withAlpha (0.13f));
            g.strokePath (p, juce::PathStrokeType (2.4f));
        }
        return;
    }

    if (A == KissTheme::Anim::Embers)
    {
        g.setGradientFill (juce::ColourGradient (theme.accent.withAlpha (0.0f), 0.0f, h * 0.55f,
                                                 theme.accent.withAlpha (0.20f), 0.0f, h, false));
        g.fillRect (0.0f, h * 0.55f, w, h * 0.45f);
    }

    for (int i = 0; i < parts.size(); ++i)
    {
        const auto& p = parts.getReference (i);
        const float span = 1.35f;

        if (A == KissTheme::Anim::Drip)
        {
            const float x = p.x * w;
            const float y = std::fmod (p.y + animPhase * p.vy * 0.07f, span) * h - h * 0.1f;
            const float len = 10.0f + p.s;

            g.setGradientFill (juce::ColourGradient (theme.accent.withAlpha (0.0f), x, y - len,
                                                     theme.accent.withAlpha (0.28f), x, y, false));
            g.fillRect (x - 0.8f, y - len, 1.6f, len);
            g.setColour (theme.accent.withAlpha (0.40f));
            g.fillEllipse (x - 1.7f, y - 1.7f, 3.4f, 3.4f);
        }
        else if (A == KissTheme::Anim::Bubbles)
        {
            const float x = p.x * w + std::sin (animPhase * 0.9f + p.r) * 12.0f;
            const float y = h - std::fmod (p.y + animPhase * p.vy * 0.06f, span) * h;
            const float r = 2.0f + p.s * 0.16f;

            g.setColour (theme.accent.withAlpha (p.a * 0.55f));
            g.drawEllipse (x - r, y - r, r * 2.0f, r * 2.0f, 1.1f);
            g.setColour (theme.glow.withAlpha (p.a * 0.35f));
            g.fillEllipse (x - r * 0.34f, y - r * 0.52f, r * 0.5f, r * 0.5f);
        }
        else if (A == KissTheme::Anim::Embers)
        {
            const float rise = std::fmod (p.y + animPhase * p.vy * 0.10f, span);
            const float x = p.x * w + std::sin (animPhase * 2.2f + p.r) * 30.0f * rise;
            const float y = h - rise * h;
            const float life = juce::jmax (0.0f, 1.0f - rise);
            const float r = juce::jmax (0.8f, life * (2.4f + p.s * 0.09f));

            // Three layers: a wide heat bloom, a body, and a white-hot core.
            g.setColour (theme.accent.withAlpha (p.a * life * 0.38f));
            g.fillEllipse (x - r * 4.5f, y - r * 4.5f, r * 9.0f, r * 9.0f);
            g.setColour (theme.glow.withAlpha (p.a * life * 0.85f));
            g.fillEllipse (x - r * 1.7f, y - r * 1.7f, r * 3.4f, r * 3.4f);
            g.setColour (juce::Colours::white.withAlpha (p.a * life * life * 0.8f));
            g.fillEllipse (x - r * 0.55f, y - r * 0.55f, r * 1.1f, r * 1.1f);
        }
        else if (A == KissTheme::Anim::Stars)
        {
            // Fixed sky, slow twinkle, one drifting streak.
            const float x = p.x * w, y = p.y * h;
            const float tw = 0.45f + 0.55f * std::sin (animPhase * (0.5f + p.vy) + p.r);
            const float r  = 0.6f + p.s * 0.022f;

            g.setColour (theme.glow.withAlpha (p.a * tw * 0.9f));
            g.fillEllipse (x - r, y - r, r * 2.0f, r * 2.0f);

            if (r > 1.4f)
            {
                g.setColour (theme.glow.withAlpha (p.a * tw * 0.20f));
                g.fillEllipse (x - r * 3.4f, y - r * 3.4f, r * 6.8f, r * 6.8f);
            }
        }
        else if (A == KissTheme::Anim::Wind)
        {
            const float y = p.y * h + std::sin (animPhase * 0.8f + p.r) * 16.0f;
            const float x = std::fmod (p.x + animPhase * (0.012f + p.vy * 0.01f), 1.25f) * w - w * 0.12f;
            const float len = 30.0f + p.s * 2.4f;

            g.setGradientFill (juce::ColourGradient (theme.accent.withAlpha (0.0f), x, y,
                                                     theme.accent.withAlpha (p.a * 0.5f), x + len, y, false));
            g.fillRect (x, y - 0.7f, len, 1.4f);
        }
    }
}

//==================================================================================================

void KissAudioProcessorEditor::timerCallback()
{
    animPhase += 0.035f;

    if (rippleT >= 0.0f)
    {
        rippleT += 0.045f;

        if (rippleT >= 1.0f) rippleT = -1.0f;

        surface.repaint();
    }

    refreshFromParameters();
    repeatFilter.refresh(); repeatQ.refresh();
    postFilter.refresh();   postQ.refresh();

    if (theme.anim != KissTheme::Anim::None) repaint();
}

void KissAudioProcessorEditor::refreshFromParameters()
{
    const int themeIndex = getChoiceValue ("theme");

    if (themeIndex != lastTheme) { lastTheme = themeIndex; applyTheme(); repaint(); }

    const bool sync = readParam (processor, "delaySync") > 0.5f;
    const int  div  = getChoiceValue ("delayDiv");

    if ((int) sync != lastSync || div != lastDiv)
    {
        lastSync = (int) sync; lastDiv = div;
        syncButton.setButtonText (sync ? "SYNC" : "MS");
        divisionButton.setButtonText (sync ? kDivisionNames[juce::jlimit (0, kDivisionNames.size() - 1, div)]
                                           : "MS");
        delayTimeKnob.slider.setEnabled (! sync);
    }

    // In MS the centre of the knob is the number, so you read it where your hand already is.
    if (! sync)
        divisionButton.setButtonText (juce::String (juce::roundToInt (readParam (processor, "delayTime"))));

    const int pt = getChoiceValue ("postType");

    if (pt != lastPostType)
    {
        lastPostType = pt;
        postTypeButton.setButtonText (kPostTypeNames[juce::jlimit (0, kPostTypeNames.size() - 1, pt)]);
    }

    const double ms = sync ? KissAudioProcessor::divisionToMs (div, processor.hostBpm.load())
                           : (double) readParam (processor, "delayTime");
    delayReadout.setText (juce::String (juce::roundToInt (ms)) + " ms", juce::dontSendNotification);

    auto hz = [] (float f)
    {
        return f >= 1000.0f ? juce::String (f / 1000.0f, 2) + " kHz"
                            : juce::String (juce::roundToInt (f)) + " Hz";
    };

    repeatReadout.setText (hz (readParam (processor, "filterFreq")), juce::dontSendNotification);
    postReadout.setText   (hz (readParam (processor, "postFreq")),   juce::dontSendNotification);

    refreshPresetName();

    for (int i = 0; i < numSfxSlots; ++i)
        sfxSlots[i].amount.slider.setEnabled (readParam (processor, sfxParamId (i, "On")) > 0.5f);
}

void KissAudioProcessorEditor::applyTheme()
{
    theme = getKissTheme (getChoiceValue ("theme"));
    lnf.setTheme (theme);
    repeatFilter.setTheme (theme); repeatQ.setTheme (theme);
    postFilter.setTheme (theme);   postQ.setTheme (theme);

    themeButton.setButtonText (kThemeNames[juce::jlimit (0, kThemeNames.size() - 1, getChoiceValue ("theme"))]);

    auto styleKnob = [this] (Knob& k)
    {
        k.caption.setColour (juce::Label::textColourId, theme.text);
        k.slider.setColour (juce::Slider::textBoxTextColourId, theme.text);
        k.slider.setColour (juce::Slider::textBoxOutlineColourId, theme.accent.withAlpha (0.25f));
        k.slider.setColour (juce::Slider::textBoxBackgroundColourId, theme.panel);
    };

    for (auto* k : { &reverbTime, &reverbTone, &reverbMix, &delayTimeKnob, &delayFeedback, &delayMix })
        styleKnob (*k);

    for (auto& s : sfxSlots) styleKnob (s.amount);

    for (auto* l : { &delayReadout, &repeatReadout, &postReadout, &postChainNote })
        l->setColour (juce::Label::textColourId, theme.dim);

    for (auto* grp : { &reverbGroup, &delayGroup, &sfxGroup, &postGroup })
    {
        grp->setColour (juce::GroupComponent::outlineColourId, theme.accent.withAlpha (0.35f));
        grp->setColour (juce::GroupComponent::textColourId, theme.dim);
    }

    auto styleButton = [this] (juce::TextButton& b)
    {
        b.setColour (juce::TextButton::buttonColourId, theme.panel);
        b.setColour (juce::TextButton::buttonOnColourId, theme.accent.withAlpha (0.65f));
        b.setColour (juce::TextButton::textColourOffId, theme.text);
        b.setColour (juce::TextButton::textColourOnId, theme.background);
    };

    for (auto* b : { &themeButton, &presetButton, &saveButton, &syncButton, &divisionButton,
                     &postTypeButton, &prevPreset, &nextPreset })
        styleButton (*b);

    presetName.setColour (juce::Label::textColourId, theme.dim);

    for (auto& s : sfxSlots) styleButton (s.button);

    repaint();
    surface.repaint();
}

//==================================================================================================

int  KissAudioProcessorEditor::getChoiceValue (const juce::String& id) const { return (int) readParam (processor, id); }
void KissAudioProcessorEditor::setChoiceValue (const juce::String& id, int v) { writeParam (processor, id, (float) v); }

// Walk the flat factory list. Wraps at both ends so you can hold a direction and browse.
void KissAudioProcessorEditor::stepPreset (int delta)
{
    const int n = (int) KissAudioProcessor::getFactoryPresets().size();

    if (n <= 0) return;

    const int now  = processor.currentPreset.load();
    const int next = now < 0 ? (delta > 0 ? 0 : n - 1) : ((now + delta) % n + n) % n;

    processor.applyFactoryPreset (next);
    refreshPresetName();
    ripple (presetName, 0.6f);
}

void KissAudioProcessorEditor::refreshPresetName()
{
    const int idx = processor.currentPreset.load();

    if (idx == lastShownPreset) return;

    lastShownPreset = idx;

    const auto& all = KissAudioProcessor::getFactoryPresets();

    if (! juce::isPositiveAndBelow (idx, (int) all.size()))
    {
        presetName.setText ("NO PRESET LOADED", juce::dontSendNotification);
        return;
    }

    const auto& p = all[(size_t) idx];
    const auto& folders = KissAudioProcessor::getPresetFolders();
    const auto folder = juce::isPositiveAndBelow (p.folder, folders.size()) ? folders[p.folder]
                                                                           : juce::String();

    presetName.setText (folder + "   >   " + juce::String (p.name) + "   ("
                          + juce::String (idx + 1) + " / " + juce::String ((int) all.size()) + ")",
                        juce::dontSendNotification);
}

void KissAudioProcessorEditor::cycleTheme()
{
    setChoiceValue ("theme", (getChoiceValue ("theme") + 1) % getNumKissThemes());
}

void KissAudioProcessorEditor::cyclePostType()
{
    setChoiceValue ("postType", (getChoiceValue ("postType") + 1) % kPostTypeNames.size());
}

void KissAudioProcessorEditor::cycleDivision()
{
    if (readParam (processor, "delaySync") < 0.5f) { writeParam (processor, "delaySync", 1.0f); return; }

    setChoiceValue ("delayDiv", (getChoiceValue ("delayDiv") + 1) % kDivisionNames.size());
    ripple (divisionButton, 0.5f);
}

//==================================================================================================

void KissAudioProcessorEditor::showPresetMenu()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel (&lnf);
    pendingArchive.clear();

    const auto& factory = KissAudioProcessor::getFactoryPresets();
    const auto& folders = KissAudioProcessor::getPresetFolders();

    auto rowFor = [&] (int i)
    {
        return std::make_unique<PresetMenuItem> (factory[(size_t) i].name, factory[(size_t) i].size, theme);
    };

    juce::PopupMenu favs;

    for (int idx : KissAudioProcessor::getFavouriteIndices())
        if (juce::isPositiveAndBelow (idx, (int) factory.size()))
            favs.addCustomItem (idx + 1, rowFor (idx));

    menu.addSubMenu ("FAVOURITES", favs);
    menu.addSeparator();

    for (int f = 0; f < folders.size(); ++f)
    {
        juce::PopupMenu sub;

        for (int i = 0; i < (int) factory.size(); ++i)
            if (factory[(size_t) i].folder == f)
                sub.addCustomItem (i + 1, rowFor (i));

        menu.addSubMenu (folders[f], sub);
    }

    const auto userNames = processor.getUserPresetNames();

    if (! userNames.isEmpty())
    {
        juce::PopupMenu bank;

        for (int i = 0; i < userNames.size(); ++i)
        {
            auto row = std::make_unique<PresetMenuItem> (userNames[i], 4, theme, true);
            const auto nm = userNames[i];
            row->onTrash = [this, nm] { pendingArchive = nm; };
            bank.addCustomItem (5000 + i, std::move (row));
        }

        menu.addSeparator();
        menu.addSubMenu ("YOUR BANK", bank);
    }

    juce::Component::SafePointer<KissAudioProcessorEditor> safe (this);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton),
                        [safe, userNames] (int result)
                        {
                            if (safe == nullptr) return;

                            // A click on the bin archives instead of loading.
                            if (safe->pendingArchive.isNotEmpty())
                            {
                                safe->processor.archiveUserPreset (safe->pendingArchive);
                                safe->pendingArchive.clear();
                                return;
                            }

                            if (result == 0) return;

                            if (result >= 5000) safe->processor.loadUserPreset (userNames[result - 5000]);
                            else                safe->processor.applyFactoryPreset (result - 1);
                        });
}

void KissAudioProcessorEditor::promptSavePreset()
{
    auto* w = new juce::AlertWindow ("SAVE PRESET",
                                     "Name it. It lands in your bank and shows up in the list.",
                                     juce::MessageBoxIconType::NoIcon);

    w->addTextEditor ("name", "My Chamber");
    w->addButton ("SAVE",   1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<KissAudioProcessorEditor> safe (this);

    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int result)
    {
        if (safe != nullptr && result == 1)
            safe->processor.saveUserPreset (w->getTextEditorContents ("name"));
    }), true);
}

#include "PluginEditor.h"

void RiserLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float rotaryStartAngle,
                                         float rotaryEndAngle, juce::Slider&)
{
    auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (14.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre  = bounds.getCentre();
    const float angle  = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    const float lineW  = juce::jmax (6.0f, radius * 0.12f);
    const float arcR   = radius - lineW * 0.5f;

    juce::Path bg;
    bg.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f,
                      rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (juce::Colour (0xff2c2c2c));
    g.strokePath (bg, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));

    if (sliderPos > 0.001f)
    {
        juce::Path val;
        val.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f,
                           rotaryStartAngle, angle, true);

        g.setColour (juce::Colour (0xffff5a1a).withAlpha (0.10f + 0.15f * sliderPos));
        g.strokePath (val, juce::PathStrokeType (lineW * 2.4f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));

        juce::ColourGradient grad (juce::Colour (0xffffa53a), centre.x - arcR, centre.y,
                                   juce::Colour (0xffff3a2a), centre.x + arcR, centre.y, false);
        g.setGradientFill (grad);
        g.strokePath (val, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
    }

    const juce::Point<float> dot (centre.x + std::sin (angle) * arcR,
                                  centre.y - std::cos (angle) * arcR);
    g.setColour (juce::Colours::white);
    g.fillEllipse (juce::Rectangle<float> (lineW * 0.9f, lineW * 0.9f).withCentre (dot));

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (radius * 0.42f, juce::Font::bold));
    g.drawText (juce::String (juce::roundToInt (sliderPos * 100.0f)) + "%",
                bounds.toNearestInt(), juce::Justification::centred);
}

RiserKnobAudioProcessorEditor::RiserKnobAudioProcessorEditor (RiserKnobAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    knob.setLookAndFeel (&lnf);
    knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    addAndMakeVisible (knob);

    knobAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "rise", knob);

    title.setText ("RISER KNOB", juce::dontSendNotification);
    title.setJustificationType (juce::Justification::centred);
    title.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (title);

    caption.setJustificationType (juce::Justification::centred);
    caption.setFont (juce::FontOptions (12.0f));
    caption.setColour (juce::Label::textColourId, juce::Colour (0xffbababa));
    addAndMakeVisible (caption);

    const char* names[4] = { "A", "B", "C", "D" };
    for (int i = 0; i < 4; ++i)
    {
        auto& b = modeButtons[i];
        b.setButtonText (names[i]);
        b.setClickingTogglesState (true);
        b.setRadioGroupId (1001);
        b.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff262626));
        b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffff5a1a));
        b.setColour (juce::TextButton::textColourOffId,  juce::Colour (0xffcfcfcf));
        b.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
        b.onClick = [this, i]
        {
            if (modeButtons[i].getToggleState() && modeAttachment != nullptr)
                modeAttachment->setValueAsCompleteGesture ((float) i);
        };
        addAndMakeVisible (b);
    }

    modeAttachment = std::make_unique<juce::ParameterAttachment> (
        *processor.apvts.getParameter ("mode"),
        [this] (float v) { setActiveMode (juce::jlimit (0, 3, (int) std::round (v))); },
        nullptr);
    modeAttachment->sendInitialUpdate();

    setSize (320, 440);
}

RiserKnobAudioProcessorEditor::~RiserKnobAudioProcessorEditor()
{
    knob.setLookAndFeel (nullptr);
}

void RiserKnobAudioProcessorEditor::setActiveMode (int index)
{
    modeButtons[index].setToggleState (true, juce::dontSendNotification);

    static const char* captions[4] = {
        "CLASSIC  ·  sauberer heller Buildup",
        "DARK  ·  trockener Tunnel-Crusher",
        "STORM  ·  Ambient-Chaos, riesiger Raum",
        "PUMP  ·  knackiges Sidechain-Monster"
    };
    caption.setText (captions[index], juce::dontSendNotification);
}

void RiserKnobAudioProcessorEditor::paint (juce::Graphics& g)
{
    juce::ColourGradient grad (juce::Colour (0xff161616), 0.0f, 0.0f,
                               juce::Colour (0xff20121a), 0.0f, (float) getHeight(), false);
    g.setGradientFill (grad);
    g.fillAll();

    g.setColour (juce::Colour (0xff8a8a8a));
    g.setFont (juce::FontOptions (10.0f));
    g.drawText ("drive · comp · sweep · noise · shift · echo · verb · pump · width",
                getLocalBounds().removeFromBottom (24), juce::Justification::centred);
}

void RiserKnobAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (18);
    title.setBounds (area.removeFromTop (36));

    area.removeFromBottom (20);                       // Platz fuer Footer
    caption.setBounds (area.removeFromBottom (22));

    auto buttonRow = area.removeFromBottom (46).reduced (8, 4);
    const int bw = buttonRow.getWidth() / 4;
    for (int i = 0; i < 4; ++i)
        modeButtons[i].setBounds (buttonRow.removeFromLeft (bw).reduced (4, 0));

    knob.setBounds (area.reduced (4));
}

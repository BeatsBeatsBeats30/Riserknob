#pragma once

#include "PluginProcessor.h"

class RiserLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override;
};

class RiserKnobAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit RiserKnobAudioProcessorEditor (RiserKnobAudioProcessor&);
    ~RiserKnobAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void setActiveMode (int index);

    RiserKnobAudioProcessor& processor;

    RiserLookAndFeel lnf;
    juce::Slider knob;
    juce::Label  title, caption;
    juce::TextButton modeButtons[4];
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> knobAttachment;
    std::unique_ptr<juce::ParameterAttachment> modeAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RiserKnobAudioProcessorEditor)
};

#pragma once

#include "PluginProcessor.h"
#include <BinaryData.h>

//==============================================================================
// Image-based knob LookAndFeel — rotates Rotary.png based on slider position
// The source image has its indicator at 12 o'clock, representing 50% (midpoint).
//==============================================================================
class KnobImageLookAndFeel : public juce::LookAndFeel_V4
{
public:
    KnobImageLookAndFeel()
    {
        knobImage = juce::ImageCache::getFromMemory(BinaryData::Rotary_png,
                                                     BinaryData::Rotary_pngSize);
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider&) override
    {
        if (! knobImage.isValid()) return;

        // The angle the knob should be rotated to.
        // Image indicator is at 12 o'clock (0 rad). At 50% the angle is the
        // midpoint of start..end which for default JUCE rotary is 0 rad.
        float angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

        auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
        float side = juce::jmin(bounds.getWidth(), bounds.getHeight());
        float cx = bounds.getCentreX();
        float cy = bounds.getCentreY();

        // Scale image to fit the knob area
        float scale = side / (float)knobImage.getWidth();
        float imgCx = knobImage.getWidth()  * 0.5f;
        float imgCy = knobImage.getHeight() * 0.5f;

        auto transform = juce::AffineTransform::rotation(angle, imgCx, imgCy)
                             .scaled(scale)
                             .translated(cx - imgCx * scale, cy - imgCy * scale);

        g.drawImageTransformed(knobImage, transform, false);
    }

private:
    juce::Image knobImage;
};

//==============================================================================
// Image-based switch LookAndFeel — rotates Switch.png based on slider position
// The source image has its indicator at 12 o'clock, representing 50% (midpoint).
//==============================================================================
class SwitchImageLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SwitchImageLookAndFeel()
    {
        switchImage = juce::ImageCache::getFromMemory(BinaryData::Switch_png,
                                                       BinaryData::Switch_pngSize);
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider&) override
    {
        if (! switchImage.isValid()) return;

        float angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

        auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
        float side = juce::jmin(bounds.getWidth(), bounds.getHeight());
        float cx = bounds.getCentreX();
        float cy = bounds.getCentreY();

        float scale = side / (float)switchImage.getWidth();
        float imgCx = switchImage.getWidth()  * 0.5f;
        float imgCy = switchImage.getHeight() * 0.5f;

        auto transform = juce::AffineTransform::rotation(angle, imgCx, imgCy)
                             .scaled(scale)
                             .translated(cx - imgCx * scale, cy - imgCy * scale);

        g.drawImageTransformed(switchImage, transform, false);
    }

private:
    juce::Image switchImage;
};

//==============================================================================
// VU meter component — draws VU.png background with a code-drawn needle
//==============================================================================
class VuMeterComponent : public juce::Component
{
public:
    VuMeterComponent()
    {
        meterImage = juce::ImageCache::getFromMemory(BinaryData::VU_png,
                                                       BinaryData::VU_pngSize);
    }

    void paint(juce::Graphics& g) override;
    void setLevel(float linearRms);

    float getImageAspectRatio() const
    {
        if (meterImage.isValid())
            return (float)meterImage.getWidth() / (float)meterImage.getHeight();
        return 1.6f;
    }

private:
    juce::Image meterImage;
    float currentDb = -60.0f;
    float targetDb  = -60.0f;

    float dbToAngle(float db) const;
};

//==============================================================================
class SlamityEditor : public juce::AudioProcessorEditor,
                      private juce::Timer
{
public:
    explicit SlamityEditor(SlamityProcessor&);
    ~SlamityEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    SlamityProcessor& processorRef;

    juce::Image backgroundImage;

    // Mackity controls
    juce::Slider mackInTrimSlider;
    juce::Slider mackOutPadSlider;
    juce::Slider mackDryWetSlider;

    // DrumSlam controls
    juce::Slider drumDriveSlider;
    juce::Slider drumOutputSlider;
    juce::Slider drumDryWetSlider;

    // Global controls
    juce::Slider chainOrderSlider;
    juce::Slider mainOutputSlider;
    juce::Slider mainDryWetSlider;

    // APVTS attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mackInTrimAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mackOutPadAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mackDryWetAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> drumDriveAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> drumOutputAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> drumDryWetAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> chainOrderAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mainOutputAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mainDryWetAttach;

    // VU meter components
    VuMeterComponent vuMackInTrim;
    VuMeterComponent vuMackOutPad;
    VuMeterComponent vuDrumDrive;
    VuMeterComponent vuDrumOutput;
    VuMeterComponent vuMainOut;

    // Custom L&F for knobs (image-based) and chain order switch
    KnobImageLookAndFeel knobLnF;
    SwitchImageLookAndFeel switchLnF;

    // Helper to draw Dymo-style label tape
    void drawDymoLabel(juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& text, float fontSize = 10.5f) const;

    // Helper to configure a standard rotary knob
    void setupKnob(juce::Slider& slider);

    // Hot-reload layout values from GUI/layout.txt (dev tool)
    std::map<juce::String, float> readLayoutFile() const;
    static float lv(const std::map<juce::String, float>& m, const juce::String& key, float def);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SlamityEditor)
};

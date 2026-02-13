#include "PluginEditor.h"
#include <cmath>

//==============================================================================
// VuMeterComponent implementation
//==============================================================================
void VuMeterComponent::setLevel(float linearRms)
{
    targetDb = (linearRms > 1.0e-10f)
             ? 20.0f * std::log10(linearRms)
             : -60.0f;

    // Exponential smoothing toward target (VU ballistics ~300ms)
    const float coeff = 0.15f;
    currentDb += coeff * (targetDb - currentDb);

    repaint();
}

float VuMeterComponent::dbToAngle(float db) const
{
    // Standard VU scale: piecewise linear interpolation
    // dB -> angle (degrees from 12 o'clock, negative = left)
    struct Point { float db; float angle; };
    static const Point table[] = {
        { -20.0f, -50.0f },
        { -10.0f, -28.0f },
        {  -7.0f, -17.0f },
        {  -5.0f,  -8.0f },
        {  -3.0f,   2.0f },
        {  -1.0f,  14.0f },
        {   0.0f,  25.0f },
        {   1.0f,  33.0f },
        {   2.0f,  39.0f },
        {   3.0f,  50.0f },
    };
    static const int numPoints = 10;

    if (db <= table[0].db) return table[0].angle;
    if (db >= table[numPoints - 1].db) return table[numPoints - 1].angle;

    for (int i = 0; i < numPoints - 1; ++i)
    {
        if (db >= table[i].db && db <= table[i + 1].db)
        {
            float t = (db - table[i].db) / (table[i + 1].db - table[i].db);
            return table[i].angle + t * (table[i + 1].angle - table[i].angle);
        }
    }
    return 0.0f;
}

void VuMeterComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Draw VU background image scaled to fill bounds
    if (meterImage.isValid())
        g.drawImage(meterImage, bounds);

    // Needle geometry — auto-calculated from component bounds
    float pivotX = bounds.getCentreX();
    float pivotY = bounds.getHeight() * 0.88f;
    float needleLen = bounds.getHeight() * 0.72f;

    // Convert current dB to angle
    float angleDeg = dbToAngle(currentDb);
    float angleRad = juce::degreesToRadians(angleDeg);

    // Needle tip (0 angle = straight up / 12 o'clock)
    float tipX = pivotX + needleLen * std::sin(angleRad);
    float tipY = pivotY - needleLen * std::cos(angleRad);

    // Draw needle shadow
    g.setColour(juce::Colour(0x40000000));
    g.drawLine(pivotX + 1.0f, pivotY + 1.0f, tipX + 1.0f, tipY + 1.0f, 1.5f);

    // Draw needle
    g.setColour(juce::Colour(0xff1a1a1a));
    g.drawLine(pivotX, pivotY, tipX, tipY, 1.2f);

    // Pivot cap
    g.setColour(juce::Colour(0xff2a2a2a));
    g.fillEllipse(pivotX - 3.0f, pivotY - 3.0f, 6.0f, 6.0f);
}

//==============================================================================
SlamityEditor::SlamityEditor(SlamityProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p)
{
    // Load background image
    backgroundImage = juce::ImageCache::getFromMemory(BinaryData::GUI_BG_NoLabel_png,
                                                       BinaryData::GUI_BG_NoLabel_pngSize);

    // Set window size to match image
    int w = backgroundImage.getWidth();
    int h = backgroundImage.getHeight();
    if (w <= 0 || h <= 0) { w = 512; h = 512; }
    setSize(w, h);

    // Configure all standard knobs
    setupKnob(mackInTrimSlider);
    setupKnob(mackOutPadSlider);
    setupKnob(mackDryWetSlider);
    setupKnob(drumDriveSlider);
    setupKnob(drumOutputSlider);
    setupKnob(drumDryWetSlider);
    setupKnob(mainOutputSlider);
    setupKnob(mainDryWetSlider);

    // Chain order rotary switch (2 positions)
    chainOrderSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    chainOrderSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    chainOrderSlider.setRange(0.0, 1.0, 1.0);
    chainOrderSlider.setLookAndFeel(&switchLnF);
    addAndMakeVisible(chainOrderSlider);

    // Create APVTS attachments (must be after slider setup)
    mackInTrimAttach  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processorRef.apvts, "mackInTrim",  mackInTrimSlider);
    mackOutPadAttach  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processorRef.apvts, "mackOutPad",  mackOutPadSlider);
    mackDryWetAttach  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processorRef.apvts, "mackDryWet",  mackDryWetSlider);
    drumDriveAttach   = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processorRef.apvts, "drumDrive",   drumDriveSlider);
    drumOutputAttach  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processorRef.apvts, "drumOutput",  drumOutputSlider);
    drumDryWetAttach  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processorRef.apvts, "drumDryWet",  drumDryWetSlider);
    chainOrderAttach  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processorRef.apvts, "chainOrder",  chainOrderSlider);
    mainOutputAttach  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processorRef.apvts, "mainOutput",  mainOutputSlider);
    mainDryWetAttach  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processorRef.apvts, "mainDryWet",  mainDryWetSlider);

    // VU meters
    addAndMakeVisible(vuMackInTrim);
    addAndMakeVisible(vuMackOutPad);
    addAndMakeVisible(vuDrumDrive);
    addAndMakeVisible(vuDrumOutput);
    addAndMakeVisible(vuMainOut);

    startTimerHz(30);
}

SlamityEditor::~SlamityEditor()
{
    stopTimer();
    mackInTrimSlider.setLookAndFeel(nullptr);
    mackOutPadSlider.setLookAndFeel(nullptr);
    mackDryWetSlider.setLookAndFeel(nullptr);
    drumDriveSlider.setLookAndFeel(nullptr);
    drumOutputSlider.setLookAndFeel(nullptr);
    drumDryWetSlider.setLookAndFeel(nullptr);
    mainOutputSlider.setLookAndFeel(nullptr);
    mainDryWetSlider.setLookAndFeel(nullptr);
    chainOrderSlider.setLookAndFeel(nullptr);
}

//==============================================================================
void SlamityEditor::setupKnob(juce::Slider& slider)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setLookAndFeel(&knobLnF);
    addAndMakeVisible(slider);
}

//==============================================================================
void SlamityEditor::timerCallback()
{
    vuMackInTrim.setLevel(processorRef.vuMackInTrim.load(std::memory_order_relaxed));
    vuMackOutPad.setLevel(processorRef.vuMackOutPad.load(std::memory_order_relaxed));
    vuDrumDrive.setLevel(processorRef.vuDrumDrive.load(std::memory_order_relaxed));
    vuDrumOutput.setLevel(processorRef.vuDrumOutput.load(std::memory_order_relaxed));
    vuMainOut.setLevel(processorRef.vuMainOutput.load(std::memory_order_relaxed));
}

//==============================================================================
void SlamityEditor::drawDymoLabel(juce::Graphics& g, juce::Rectangle<int> bounds,
                                   const juce::String& text) const
{
    auto area = bounds.toFloat();

    // Tape strip background (dark, like black Dymo tape)
    g.setColour(juce::Colour(0xff181820));
    g.fillRoundedRectangle(area, 2.5f);

    // Subtle top highlight (simulates the glossy tape surface)
    g.setColour(juce::Colour(0xff282830));
    g.fillRoundedRectangle(area.removeFromTop(area.getHeight() * 0.45f), 2.5f);

    // Outer border (tape edge)
    g.setColour(juce::Colour(0xff101018));
    g.drawRoundedRectangle(bounds.toFloat(), 2.5f, 0.8f);

    // Embossed text shadow (offset slightly down-right)
    auto monoFont = juce::Font(juce::Font::getDefaultMonospacedFontName(), 10.5f, juce::Font::bold);
    g.setFont(monoFont);

    g.setColour(juce::Colour(0xff060608));
    g.drawText(text, bounds.translated(0, 1), juce::Justification::centred);

    // Raised white text
    g.setColour(juce::Colour(0xffd8d8e0));
    g.drawText(text, bounds, juce::Justification::centred);
}

//==============================================================================
std::map<juce::String, float> SlamityEditor::readLayoutFile() const
{
    std::map<juce::String, float> vals;
    // Derive layout.txt path from this source file's compile-time location
    auto file = juce::File(__FILE__).getParentDirectory()
                    .getParentDirectory().getChildFile("GUI/layout.txt");
    if (! file.existsAsFile()) return vals;

    for (auto& line : juce::StringArray::fromLines(file.loadFileAsString()))
    {
        auto trimmed = line.trimStart();
        if (trimmed.isEmpty() || trimmed[0] == '#') continue;
        auto eq = trimmed.indexOfChar('=');
        if (eq < 0) continue;
        vals[trimmed.substring(0, eq).trim()] = trimmed.substring(eq + 1).trim().getFloatValue();
    }
    return vals;
}

float SlamityEditor::lv(const std::map<juce::String, float>& m, const juce::String& key, float def)
{
    auto it = m.find(key);
    return it != m.end() ? it->second : def;
}

//==============================================================================
void SlamityEditor::paint(juce::Graphics& g)
{
    // Draw background image scaled to fill
    if (backgroundImage.isValid())
        g.drawImage(backgroundImage, getLocalBounds().toFloat());
    else
        g.fillAll(juce::Colour(0xff1a1a1a));

    auto L = readLayoutFile();
    auto w = getWidth();
    auto h = getHeight();

    int labelH   = (int)lv(L, "labelH", 16.0f);
    int labelW   = (int)(w * lv(L, "labelW", 0.18f));
    int mdLabelW = (int)(w * lv(L, "mdLabelW", 0.06f));
    int mdLabelH = (int)lv(L, "mdLabelH", 14.0f);

    // Helper: draw a label centred at (xFrac, yFrac)
    auto label = [&](const char* xKey, float xDef, const char* yKey, float yDef,
                     const juce::String& text, int lw, int lh) {
        int cx = (int)(w * lv(L, xKey, xDef));
        int y  = (int)(h * lv(L, yKey, yDef));
        drawDymoLabel(g, { cx - lw / 2, y, lw, lh }, text);
    };

    // Mackity labels
    label("mackInTrimLabel_x", 0.165f, "mackInTrimLabel_y", 0.10f, "IN TRIM", labelW, labelH);
    label("mackOutPadLabel_x", 0.165f, "mackOutPadLabel_y", 0.30f, "OUT PAD", labelW, labelH);
    label("mackDryWetLabel_x", 0.165f, "mackDryWetLabel_y", 0.50f, "DRY/WET", labelW, labelH);

    // DrumSlam labels
    label("drumDriveLabel_x",  0.835f, "drumDriveLabel_y",  0.10f, "DRIVE",   labelW, labelH);
    label("drumOutputLabel_x", 0.835f, "drumOutputLabel_y", 0.30f, "OUTPUT",  labelW, labelH);
    label("drumDryWetLabel_x", 0.835f, "drumDryWetLabel_y", 0.50f, "DRY/WET", labelW, labelH);

    // Bottom row labels
    label("mainDryWetLabel_x", 0.50f,  "mainDryWetLabel_y", 0.72f, "DRY/WET", labelW, labelH);
    label("mainOutputLabel_x", 0.835f, "mainOutputLabel_y", 0.72f, "MAIN OUT", labelW, labelH);

    // M > D / D > M labels
    label("mdLeft_x",  0.12f, "mdLeft_y",  0.84f, "M > D", mdLabelW, mdLabelH);
    label("mdRight_x", 0.21f, "mdRight_y", 0.84f, "D > M", mdLabelW, mdLabelH);
}

//==============================================================================
void SlamityEditor::resized()
{
    auto L = readLayoutFile();
    auto w = getWidth();
    auto h = getHeight();

    int knobSize   = (int)(w * lv(L, "knobSize",   0.1155f));
    int switchSize = (int)(w * lv(L, "switchSize",  0.091f));

    // Helper: position a slider centred at (xFrac, yFrac top-edge)
    auto place = [&](juce::Slider& s, const char* xKey, float xDef,
                     const char* yKey, float yDef, int size) {
        int cx = (int)(w * lv(L, xKey, xDef));
        int y  = (int)(h * lv(L, yKey, yDef));
        s.setBounds(cx - size / 2, y, size, size);
    };

    // Mackity knobs
    place(mackInTrimSlider, "mackInTrimKnob_x", 0.165f, "mackInTrimKnob_y", 0.12f, knobSize);
    place(mackOutPadSlider, "mackOutPadKnob_x", 0.165f, "mackOutPadKnob_y", 0.32f, knobSize);
    place(mackDryWetSlider, "mackDryWetKnob_x", 0.165f, "mackDryWetKnob_y", 0.52f, knobSize);

    // DrumSlam knobs
    place(drumDriveSlider,  "drumDriveKnob_x",  0.835f, "drumDriveKnob_y",  0.12f, knobSize);
    place(drumOutputSlider, "drumOutputKnob_x",  0.835f, "drumOutputKnob_y", 0.32f, knobSize);
    place(drumDryWetSlider, "drumDryWetKnob_x",  0.835f, "drumDryWetKnob_y", 0.52f, knobSize);

    // Chain order switch
    place(chainOrderSlider, "chainSwitch_x", 0.165f, "chainSwitch_y", 0.74f, switchSize);

    // Bottom row knobs
    place(mainDryWetSlider, "mainDryWetKnob_x", 0.50f,  "mainDryWetKnob_y", 0.74f, knobSize);
    place(mainOutputSlider, "mainOutputKnob_x", 0.835f, "mainOutputKnob_y", 0.74f, knobSize);

    // VU meters — height from layout, width from image aspect ratio
    int vuH = (int)(w * lv(L, "vuH", 0.1155f));
    float vuAspect = vuMackInTrim.getImageAspectRatio();
    int vuW = (int)(vuH * vuAspect);

    auto placeVu = [&](VuMeterComponent& vu, const char* xKey, float xDef,
                        const char* yKey, float yDef) {
        int cx = (int)(w * lv(L, xKey, xDef));
        int y  = (int)(h * lv(L, yKey, yDef));
        vu.setBounds(cx - vuW / 2, y, vuW, vuH);
    };

    placeVu(vuMackInTrim, "vuMackInTrim_x", 0.37f, "vuMackInTrim_y", 0.14f);
    placeVu(vuDrumDrive,  "vuDrumDrive_x",  0.63f, "vuDrumDrive_y",  0.14f);
    placeVu(vuMackOutPad, "vuMackOutPad_x",  0.37f, "vuMackOutPad_y",  0.34f);
    placeVu(vuDrumOutput, "vuDrumOutput_x",  0.63f, "vuDrumOutput_y",  0.34f);
    placeVu(vuMainOut,    "vuMainOutput_x",  0.50f, "vuMainOutput_y",  0.54f);
}

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

//==============================================================================
// Slamity: Combined Airwindows Mackity + DrumSlam plugin
// DSP derived from Airwindows by Chris Johnson (MIT License)
//==============================================================================

class SlamityProcessor : public juce::AudioProcessor
{
public:
    //==============================================================================
    SlamityProcessor();
    ~SlamityProcessor() override;

    //==============================================================================
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    //==============================================================================
    juce::AudioProcessorValueTreeState apvts;

    // RMS levels for VU meters (mono sum, updated per block)
    std::atomic<float> vuMackInTrim{0.0f};
    std::atomic<float> vuMackOutPad{0.0f};
    std::atomic<float> vuDrumDrive{0.0f};
    std::atomic<float> vuDrumOutput{0.0f};
    std::atomic<float> vuMainOutput{0.0f};

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // --- Mackity DSP state ---
    double mack_iirSampleAL = 0.0;
    double mack_iirSampleBL = 0.0;
    double mack_iirSampleAR = 0.0;
    double mack_iirSampleBR = 0.0;
    double mack_biquadA[15] = {};
    double mack_biquadB[15] = {};

    // --- DrumSlam DSP state ---
    double drum_iirSampleAL = 0.0;
    double drum_iirSampleBL = 0.0;
    double drum_iirSampleCL = 0.0;
    double drum_iirSampleDL = 0.0;
    double drum_iirSampleEL = 0.0;
    double drum_iirSampleFL = 0.0;
    double drum_iirSampleGL = 0.0;
    double drum_iirSampleHL = 0.0;
    double drum_lastSampleL = 0.0;

    double drum_iirSampleAR = 0.0;
    double drum_iirSampleBR = 0.0;
    double drum_iirSampleCR = 0.0;
    double drum_iirSampleDR = 0.0;
    double drum_iirSampleER = 0.0;
    double drum_iirSampleFR = 0.0;
    double drum_iirSampleGR = 0.0;
    double drum_iirSampleHR = 0.0;
    double drum_lastSampleR = 0.0;
    bool drum_fpFlip = true;

    // --- TPDF dither state ---
    uint32_t fpdL = 1, fpdR = 1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SlamityProcessor)
};

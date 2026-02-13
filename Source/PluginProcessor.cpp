#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// Slamity: Combined Airwindows Mackity + DrumSlam plugin
// DSP derived from Airwindows by Chris Johnson (MIT License)
//==============================================================================

SlamityProcessor::SlamityProcessor()
    : AudioProcessor(BusesProperties()
                     .withInput("Input", juce::AudioChannelSet::stereo(), true)
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout())
{
}

SlamityProcessor::~SlamityProcessor() {}

juce::AudioProcessorValueTreeState::ParameterLayout SlamityProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Mackity parameters
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("mackInTrim", 1), "In Trim",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.1f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("mackOutPad", 1), "Out Pad",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("mackDryWet", 1), "Mack Dry/Wet",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 1.0f));

    // DrumSlam parameters
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("drumDrive", 1), "Drive",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("drumOutput", 1), "Output",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("drumDryWet", 1), "Drum Dry/Wet",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 1.0f));

    // Global parameters
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("chainOrder", 1), "Chain Order",
        juce::NormalisableRange<float>(0.0f, 1.0f, 1.0f), 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("mainOutput", 1), "Main Output",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("mainDryWet", 1), "Main Dry/Wet",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 1.0f));

    return { params.begin(), params.end() };
}

//==============================================================================
const juce::String SlamityProcessor::getName() const { return JucePlugin_Name; }
bool SlamityProcessor::acceptsMidi() const { return false; }
bool SlamityProcessor::producesMidi() const { return false; }
bool SlamityProcessor::isMidiEffect() const { return false; }
double SlamityProcessor::getTailLengthSeconds() const { return 0.0; }
int SlamityProcessor::getNumPrograms() { return 1; }
int SlamityProcessor::getCurrentProgram() { return 0; }
void SlamityProcessor::setCurrentProgram(int) {}
const juce::String SlamityProcessor::getProgramName(int) { return {}; }
void SlamityProcessor::changeProgramName(int, const juce::String&) {}

//==============================================================================
void SlamityProcessor::prepareToPlay(double, int)
{
    // Reset Mackity state
    mack_iirSampleAL = 0.0;
    mack_iirSampleBL = 0.0;
    mack_iirSampleAR = 0.0;
    mack_iirSampleBR = 0.0;
    std::fill(std::begin(mack_biquadA), std::end(mack_biquadA), 0.0);
    std::fill(std::begin(mack_biquadB), std::end(mack_biquadB), 0.0);

    // Reset DrumSlam state
    drum_iirSampleAL = 0.0;
    drum_iirSampleBL = 0.0;
    drum_iirSampleCL = 0.0;
    drum_iirSampleDL = 0.0;
    drum_iirSampleEL = 0.0;
    drum_iirSampleFL = 0.0;
    drum_iirSampleGL = 0.0;
    drum_iirSampleHL = 0.0;
    drum_lastSampleL = 0.0;

    drum_iirSampleAR = 0.0;
    drum_iirSampleBR = 0.0;
    drum_iirSampleCR = 0.0;
    drum_iirSampleDR = 0.0;
    drum_iirSampleER = 0.0;
    drum_iirSampleFR = 0.0;
    drum_iirSampleGR = 0.0;
    drum_iirSampleHR = 0.0;
    drum_lastSampleR = 0.0;
    drum_fpFlip = true;

    // Initialize TPDF dither state
    fpdL = 1; while (fpdL < 16386) fpdL = (uint32_t)rand() * (uint32_t)UINT32_MAX;
    fpdR = 1; while (fpdR < 16386) fpdR = (uint32_t)rand() * (uint32_t)UINT32_MAX;
}

void SlamityProcessor::releaseResources() {}

bool SlamityProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    return true;
}

void SlamityProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    const int sampleFrames = buffer.getNumSamples();
    if (sampleFrames == 0) return;

    const double sr = getSampleRate();
    double overallscale = 1.0;
    overallscale /= 44100.0;
    overallscale *= sr;

    // --- Read all parameters ---
    const float mackInTrimParam = *apvts.getRawParameterValue("mackInTrim");
    const float mackOutPadParam = *apvts.getRawParameterValue("mackOutPad");
    const float mackDryWetParam = *apvts.getRawParameterValue("mackDryWet");
    const float drumDriveParam  = *apvts.getRawParameterValue("drumDrive");
    const float drumOutputParam = *apvts.getRawParameterValue("drumOutput");
    const float drumDryWetParam = *apvts.getRawParameterValue("drumDryWet");
    const float chainOrderParam = *apvts.getRawParameterValue("chainOrder");
    const float mainOutputParam = *apvts.getRawParameterValue("mainOutput");
    const float mainDryWetParam = *apvts.getRawParameterValue("mainDryWet");

    // =====================================================================
    // MACKITY: Pre-block coefficient computation
    // =====================================================================
    double mackInTrim = mackInTrimParam * 10.0;
    double mackOutPad = mackOutPadParam;
    double mackWet = mackDryWetParam;
    mackInTrim *= mackInTrim;

    double mackIirAmountA = 0.001860867 / overallscale;
    double mackIirAmountB = 0.000287496 / overallscale;

    mack_biquadB[0] = mack_biquadA[0] = 19160.0 / sr;
    mack_biquadA[1] = 0.431684981684982;
    mack_biquadB[1] = 1.1582298;

    double K = tan(juce::MathConstants<double>::pi * mack_biquadA[0]);
    double norm = 1.0 / (1.0 + K / mack_biquadA[1] + K * K);
    mack_biquadA[2] = K * K * norm;
    mack_biquadA[3] = 2.0 * mack_biquadA[2];
    mack_biquadA[4] = mack_biquadA[2];
    mack_biquadA[5] = 2.0 * (K * K - 1.0) * norm;
    mack_biquadA[6] = (1.0 - K / mack_biquadA[1] + K * K) * norm;

    K = tan(juce::MathConstants<double>::pi * mack_biquadB[0]);
    norm = 1.0 / (1.0 + K / mack_biquadB[1] + K * K);
    mack_biquadB[2] = K * K * norm;
    mack_biquadB[3] = 2.0 * mack_biquadB[2];
    mack_biquadB[4] = mack_biquadB[2];
    mack_biquadB[5] = 2.0 * (K * K - 1.0) * norm;
    mack_biquadB[6] = (1.0 - K / mack_biquadB[1] + K * K) * norm;

    // =====================================================================
    // DRUMSLAM: Pre-block coefficient computation
    // =====================================================================
    double drumIirAmountL = 0.0819 / overallscale;
    double drumIirAmountH = 0.377933067 / overallscale;
    double drumDrive = (drumDriveParam * 3.0) + 1.0;
    double drumOut = drumOutputParam;
    double drumWet = drumDryWetParam;

    // =====================================================================
    // GLOBAL
    // =====================================================================
    double mainOutGain = mainOutputParam;
    double mainWet = mainDryWetParam;
    bool mackFirst = chainOrderParam < 0.5f;

    // RMS accumulators for VU meters
    double rmsAccMackTrim = 0.0, rmsAccMackPad = 0.0;
    double rmsAccDrumDrive = 0.0, rmsAccDrumOut = 0.0;
    double rmsAccMainOut = 0.0;

    // --- Mackity processing lambda ---
    auto processMackity = [&](double& sL, double& sR) {
        double dryL = sL, dryR = sR;

        // High-pass IIR filter A (subsonic removal)
        if (fabs(mack_iirSampleAL) < 1.18e-37) mack_iirSampleAL = 0.0;
        mack_iirSampleAL = (mack_iirSampleAL * (1.0 - mackIirAmountA)) + (sL * mackIirAmountA);
        sL -= mack_iirSampleAL;
        if (fabs(mack_iirSampleAR) < 1.18e-37) mack_iirSampleAR = 0.0;
        mack_iirSampleAR = (mack_iirSampleAR * (1.0 - mackIirAmountA)) + (sR * mackIirAmountA);
        sR -= mack_iirSampleAR;

        // Input trim
        if (mackInTrim != 1.0) { sL *= mackInTrim; sR *= mackInTrim; }
        rmsAccMackTrim += sL * sL + sR * sR;

        // Biquad A lowpass (DF1)
        double outL = mack_biquadA[2]*sL + mack_biquadA[3]*mack_biquadA[7] + mack_biquadA[4]*mack_biquadA[8] - mack_biquadA[5]*mack_biquadA[9] - mack_biquadA[6]*mack_biquadA[10];
        mack_biquadA[8] = mack_biquadA[7]; mack_biquadA[7] = sL; sL = outL; mack_biquadA[10] = mack_biquadA[9]; mack_biquadA[9] = sL;

        double outR = mack_biquadA[2]*sR + mack_biquadA[3]*mack_biquadA[11] + mack_biquadA[4]*mack_biquadA[12] - mack_biquadA[5]*mack_biquadA[13] - mack_biquadA[6]*mack_biquadA[14];
        mack_biquadA[12] = mack_biquadA[11]; mack_biquadA[11] = sR; sR = outR; mack_biquadA[14] = mack_biquadA[13]; mack_biquadA[13] = sR;

        // Soft saturation (5th-order polynomial waveshaper)
        if (sL > 1.0) sL = 1.0;
        if (sL < -1.0) sL = -1.0;
        sL -= pow(sL, 5) * 0.1768;
        if (sR > 1.0) sR = 1.0;
        if (sR < -1.0) sR = -1.0;
        sR -= pow(sR, 5) * 0.1768;

        // Biquad B lowpass (DF1)
        outL = mack_biquadB[2]*sL + mack_biquadB[3]*mack_biquadB[7] + mack_biquadB[4]*mack_biquadB[8] - mack_biquadB[5]*mack_biquadB[9] - mack_biquadB[6]*mack_biquadB[10];
        mack_biquadB[8] = mack_biquadB[7]; mack_biquadB[7] = sL; sL = outL; mack_biquadB[10] = mack_biquadB[9]; mack_biquadB[9] = sL;

        outR = mack_biquadB[2]*sR + mack_biquadB[3]*mack_biquadB[11] + mack_biquadB[4]*mack_biquadB[12] - mack_biquadB[5]*mack_biquadB[13] - mack_biquadB[6]*mack_biquadB[14];
        mack_biquadB[12] = mack_biquadB[11]; mack_biquadB[11] = sR; sR = outR; mack_biquadB[14] = mack_biquadB[13]; mack_biquadB[13] = sR;

        // High-pass IIR filter B (DC removal)
        if (fabs(mack_iirSampleBL) < 1.18e-37) mack_iirSampleBL = 0.0;
        mack_iirSampleBL = (mack_iirSampleBL * (1.0 - mackIirAmountB)) + (sL * mackIirAmountB);
        sL -= mack_iirSampleBL;
        if (fabs(mack_iirSampleBR) < 1.18e-37) mack_iirSampleBR = 0.0;
        mack_iirSampleBR = (mack_iirSampleBR * (1.0 - mackIirAmountB)) + (sR * mackIirAmountB);
        sR -= mack_iirSampleBR;

        // Output pad
        if (mackOutPad != 1.0) { sL *= mackOutPad; sR *= mackOutPad; }
        rmsAccMackPad += sL * sL + sR * sR;

        // Mackity dry/wet
        if (mackWet != 1.0) {
            sL = (sL * mackWet) + (dryL * (1.0 - mackWet));
            sR = (sR * mackWet) + (dryR * (1.0 - mackWet));
        }
    };

    // --- DrumSlam processing lambda ---
    auto processDrumSlam = [&](double& sL, double& sR) {
        double dryL = sL, dryR = sR;

        double lowSampleL, lowSampleR;
        double midSampleL, midSampleR;
        double highSampleL, highSampleR;

        sL *= drumDrive;
        sR *= drumDrive;
        rmsAccDrumDrive += sL * sL + sR * sR;

        // 3-band split with alternating filter sets
        if (drum_fpFlip)
        {
            drum_iirSampleAL = (drum_iirSampleAL * (1.0 - drumIirAmountL)) + (sL * drumIirAmountL);
            drum_iirSampleBL = (drum_iirSampleBL * (1.0 - drumIirAmountL)) + (drum_iirSampleAL * drumIirAmountL);
            lowSampleL = drum_iirSampleBL;

            drum_iirSampleAR = (drum_iirSampleAR * (1.0 - drumIirAmountL)) + (sR * drumIirAmountL);
            drum_iirSampleBR = (drum_iirSampleBR * (1.0 - drumIirAmountL)) + (drum_iirSampleAR * drumIirAmountL);
            lowSampleR = drum_iirSampleBR;

            drum_iirSampleEL = (drum_iirSampleEL * (1.0 - drumIirAmountH)) + (sL * drumIirAmountH);
            drum_iirSampleFL = (drum_iirSampleFL * (1.0 - drumIirAmountH)) + (drum_iirSampleEL * drumIirAmountH);
            midSampleL = drum_iirSampleFL - drum_iirSampleBL;

            drum_iirSampleER = (drum_iirSampleER * (1.0 - drumIirAmountH)) + (sR * drumIirAmountH);
            drum_iirSampleFR = (drum_iirSampleFR * (1.0 - drumIirAmountH)) + (drum_iirSampleER * drumIirAmountH);
            midSampleR = drum_iirSampleFR - drum_iirSampleBR;

            highSampleL = sL - drum_iirSampleFL;
            highSampleR = sR - drum_iirSampleFR;
        }
        else
        {
            drum_iirSampleCL = (drum_iirSampleCL * (1.0 - drumIirAmountL)) + (sL * drumIirAmountL);
            drum_iirSampleDL = (drum_iirSampleDL * (1.0 - drumIirAmountL)) + (drum_iirSampleCL * drumIirAmountL);
            lowSampleL = drum_iirSampleDL;

            drum_iirSampleCR = (drum_iirSampleCR * (1.0 - drumIirAmountL)) + (sR * drumIirAmountL);
            drum_iirSampleDR = (drum_iirSampleDR * (1.0 - drumIirAmountL)) + (drum_iirSampleCR * drumIirAmountL);
            lowSampleR = drum_iirSampleDR;

            drum_iirSampleGL = (drum_iirSampleGL * (1.0 - drumIirAmountH)) + (sL * drumIirAmountH);
            drum_iirSampleHL = (drum_iirSampleHL * (1.0 - drumIirAmountH)) + (drum_iirSampleGL * drumIirAmountH);
            midSampleL = drum_iirSampleHL - drum_iirSampleDL;

            drum_iirSampleGR = (drum_iirSampleGR * (1.0 - drumIirAmountH)) + (sR * drumIirAmountH);
            drum_iirSampleHR = (drum_iirSampleHR * (1.0 - drumIirAmountH)) + (drum_iirSampleGR * drumIirAmountH);
            midSampleR = drum_iirSampleHR - drum_iirSampleDR;

            highSampleL = sL - drum_iirSampleHL;
            highSampleR = sR - drum_iirSampleHR;
        }
        drum_fpFlip = !drum_fpFlip;

        // Low band saturation
        if (lowSampleL > 1.0) lowSampleL = 1.0;
        if (lowSampleL < -1.0) lowSampleL = -1.0;
        if (lowSampleR > 1.0) lowSampleR = 1.0;
        if (lowSampleR < -1.0) lowSampleR = -1.0;
        lowSampleL -= (lowSampleL * (fabs(lowSampleL) * 0.448) * (fabs(lowSampleL) * 0.448));
        lowSampleR -= (lowSampleR * (fabs(lowSampleR) * 0.448) * (fabs(lowSampleR) * 0.448));
        lowSampleL *= drumDrive;
        lowSampleR *= drumDrive;

        // High band saturation
        if (highSampleL > 1.0) highSampleL = 1.0;
        if (highSampleL < -1.0) highSampleL = -1.0;
        if (highSampleR > 1.0) highSampleR = 1.0;
        if (highSampleR < -1.0) highSampleR = -1.0;
        highSampleL -= (highSampleL * (fabs(highSampleL) * 0.599) * (fabs(highSampleL) * 0.599));
        highSampleR -= (highSampleR * (fabs(highSampleR) * 0.599) * (fabs(highSampleR) * 0.599));
        highSampleL *= drumDrive;
        highSampleR *= drumDrive;

        // Mid band saturation with skew
        midSampleL *= drumDrive;
        midSampleR *= drumDrive;

        // Mid skew - left
        double skew = (midSampleL - drum_lastSampleL);
        drum_lastSampleL = midSampleL;
        double bridgerectifier = fabs(skew);
        if (bridgerectifier > 3.1415926) bridgerectifier = 3.1415926;
        bridgerectifier = sin(bridgerectifier);
        if (skew > 0) skew = bridgerectifier * 3.1415926;
        else skew = -bridgerectifier * 3.1415926;
        skew *= midSampleL;
        skew *= 1.557079633;
        bridgerectifier = fabs(midSampleL);
        bridgerectifier += skew;
        if (bridgerectifier > 1.57079633) bridgerectifier = 1.57079633;
        bridgerectifier = sin(bridgerectifier);
        bridgerectifier *= drumDrive;
        bridgerectifier += skew;
        if (bridgerectifier > 1.57079633) bridgerectifier = 1.57079633;
        bridgerectifier = sin(bridgerectifier);
        if (midSampleL > 0) midSampleL = bridgerectifier;
        else midSampleL = -bridgerectifier;

        // Mid skew - right
        skew = (midSampleR - drum_lastSampleR);
        drum_lastSampleR = midSampleR;
        bridgerectifier = fabs(skew);
        if (bridgerectifier > 3.1415926) bridgerectifier = 3.1415926;
        bridgerectifier = sin(bridgerectifier);
        if (skew > 0) skew = bridgerectifier * 3.1415926;
        else skew = -bridgerectifier * 3.1415926;
        skew *= midSampleR;
        skew *= 1.557079633;
        bridgerectifier = fabs(midSampleR);
        bridgerectifier += skew;
        if (bridgerectifier > 1.57079633) bridgerectifier = 1.57079633;
        bridgerectifier = sin(bridgerectifier);
        bridgerectifier *= drumDrive;
        bridgerectifier += skew;
        if (bridgerectifier > 1.57079633) bridgerectifier = 1.57079633;
        bridgerectifier = sin(bridgerectifier);
        if (midSampleR > 0) midSampleR = bridgerectifier;
        else midSampleR = -bridgerectifier;

        // Recombine bands
        sL = ((lowSampleL + midSampleL + highSampleL) / drumDrive) * drumOut;
        sR = ((lowSampleR + midSampleR + highSampleR) / drumDrive) * drumOut;
        rmsAccDrumOut += sL * sL + sR * sR;

        // DrumSlam dry/wet
        if (drumWet != 1.0) {
            sL = (sL * drumWet) + (dryL * (1.0 - drumWet));
            sR = (sR * drumWet) + (dryR * (1.0 - drumWet));
        }
    };

    // =====================================================================
    // PER-SAMPLE PROCESSING LOOP
    // =====================================================================
    float* channelL = buffer.getWritePointer(0);
    float* channelR = buffer.getWritePointer(1);

    for (int i = 0; i < sampleFrames; ++i)
    {
        double inputSampleL = channelL[i];
        double inputSampleR = channelR[i];

        // Airwindows denormal protection
        if (fabs(inputSampleL) < 1.18e-23) inputSampleL = fpdL * 1.18e-17;
        if (fabs(inputSampleR) < 1.18e-23) inputSampleR = fpdR * 1.18e-17;

        // Save for main dry/wet
        double mainDryL = inputSampleL;
        double mainDryR = inputSampleR;

        // Process in selected chain order
        if (mackFirst) {
            processMackity(inputSampleL, inputSampleR);
            processDrumSlam(inputSampleL, inputSampleR);
        } else {
            processDrumSlam(inputSampleL, inputSampleR);
            processMackity(inputSampleL, inputSampleR);
        }

        // Main output gain
        inputSampleL *= mainOutGain;
        inputSampleR *= mainOutGain;

        // Main dry/wet
        if (mainWet != 1.0) {
            inputSampleL = (inputSampleL * mainWet) + (mainDryL * (1.0 - mainWet));
            inputSampleR = (inputSampleR * mainWet) + (mainDryR * (1.0 - mainWet));
        }
        rmsAccMainOut += inputSampleL * inputSampleL + inputSampleR * inputSampleR;

        // TPDF dither (Airwindows convention)
        int expon; frexpf((float)inputSampleL, &expon);
        fpdL ^= fpdL << 13; fpdL ^= fpdL >> 17; fpdL ^= fpdL << 5;
        inputSampleL += (double)((double(fpdL) - uint32_t(0x7fffffff)) * 5.5e-36l * pow(2, expon + 62));
        frexpf((float)inputSampleR, &expon);
        fpdR ^= fpdR << 13; fpdR ^= fpdR >> 17; fpdR ^= fpdR << 5;
        inputSampleR += (double)((double(fpdR) - uint32_t(0x7fffffff)) * 5.5e-36l * pow(2, expon + 62));

        channelL[i] = (float)inputSampleL;
        channelR[i] = (float)inputSampleR;
    }

    // Store RMS levels for VU meters (mono sum: average of L+R)
    double invN = 1.0 / (double)sampleFrames;
    vuMackInTrim.store((float)std::sqrt(rmsAccMackTrim * invN * 0.5), std::memory_order_relaxed);              // 1.0x (no change)
    vuMackOutPad.store((float)(std::sqrt(rmsAccMackPad * invN * 0.5) * mackInTrimParam * 10.0), std::memory_order_relaxed); // scaled by In Trim
    vuDrumDrive.store((float)(std::sqrt(rmsAccDrumDrive * invN * 0.5) * 1.5), std::memory_order_relaxed);      // +50%
    vuDrumOutput.store((float)(std::sqrt(rmsAccDrumOut * invN * 0.5) * 1.75), std::memory_order_relaxed);      // +75%
    vuMainOutput.store((float)(std::sqrt(rmsAccMainOut * invN * 0.5) * 2.25), std::memory_order_relaxed);      // +125%
}

//==============================================================================
bool SlamityProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* SlamityProcessor::createEditor()
{
    return new SlamityEditor(*this);
}

//==============================================================================
void SlamityProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void SlamityProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr)
        if (xmlState->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SlamityProcessor();
}

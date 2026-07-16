#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

// ============================================================================
// Soft-Knee-Kompressor mit programmabhaengigem Release
// ============================================================================
class SoftKneeCompressor
{
public:
    void prepare (double sr) { sampleRate = sr; grDb = 0.0f; }

    void setParams (float thresholdDb_, float ratio_, float kneeDb_,
                    float attackMs, float releaseMs)
    {
        thresholdDb = thresholdDb_;
        ratio       = juce::jmax (1.0f, ratio_);
        kneeDb      = juce::jmax (0.1f, kneeDb_);
        aAtt     = std::exp (-1.0f / (0.001f * attackMs          * (float) sampleRate));
        aRel     = std::exp (-1.0f / (0.001f * releaseMs         * (float) sampleRate));
        aRelFast = std::exp (-1.0f / (0.001f * releaseMs * 0.35f * (float) sampleRate));
    }

    float processSample (float peakLinear)
    {
        const float levelDb = juce::Decibels::gainToDecibels (peakLinear, -80.0f);
        const float over    = levelDb - thresholdDb;

        float targetGr = 0.0f;
        if (over > -kneeDb * 0.5f)
        {
            if (over < kneeDb * 0.5f)
            {
                const float t = over + kneeDb * 0.5f;
                targetGr = (t * t) / (2.0f * kneeDb) * (1.0f - 1.0f / ratio);
            }
            else
                targetGr = over * (1.0f - 1.0f / ratio);
        }

        if (targetGr > grDb)
            grDb = aAtt * grDb + (1.0f - aAtt) * targetGr;
        else
        {
            const float rel = (grDb > 6.0f) ? aRelFast : aRel;
            grDb = rel * grDb + (1.0f - rel) * targetGr;
        }
        return juce::Decibels::decibelsToGain (-grDb);
    }

private:
    double sampleRate = 44100.0;
    float thresholdDb = 0.0f, ratio = 1.0f, kneeDb = 6.0f;
    float aAtt = 0.0f, aRel = 0.0f, aRelFast = 0.0f, grDb = 0.0f;
};

// ============================================================================
// Frequency-Shifter (Single-Sideband ueber Hilbert-Allpass-Netzwerk)
// ============================================================================
class FreqShifter
{
public:
    void prepare (double sr) { sampleRate = sr; reset(); }

    void reset()
    {
        for (auto& s : chainI) s = {};
        for (auto& s : chainQ) s = {};
        delayed = 0.0f;
        phase = 0.0;
    }

    void setShiftHz (float hz)
    {
        inc = juce::MathConstants<double>::twoPi * (double) hz / sampleRate;
    }

    float processSample (float x)
    {
        float i = x, q = x;
        for (int k = 0; k < 4; ++k) i = chainI[k].process (i, cI[k] * cI[k]);
        for (int k = 0; k < 4; ++k) q = chainQ[k].process (q, cQ[k] * cQ[k]);

        const float iD = delayed;
        delayed = i;

        const float s = (float) std::sin (phase);
        const float c = (float) std::cos (phase);
        phase += inc;
        if (phase > juce::MathConstants<double>::twoPi)
            phase -= juce::MathConstants<double>::twoPi;

        return iD * c - q * s;
    }

private:
    struct Allpass2
    {
        float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        float process (float x, float c)
        {
            const float y = c * (x + y2) - x2;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            return y;
        }
    };

    static constexpr float cI[4] { 0.6923878f,       0.9360654322959f,
                                   0.9882295226860f, 0.9987488452737f };
    static constexpr float cQ[4] { 0.4021921162426f, 0.8561710882420f,
                                   0.9722909545651f, 0.9952884791278f };

    Allpass2 chainI[4], chainQ[4];
    float delayed = 0.0f;
    double phase = 0.0, inc = 0.0, sampleRate = 44100.0;
};

// ============================================================================
// Riser Knob Pro — ein Knob, vier klar getrennte Charaktere
// ============================================================================
class RiserKnobAudioProcessor : public juce::AudioProcessor
{
public:
    struct ModeWeights
    {
        float drive, comp, reso, noise, pumpDepth, pumpAccel,
              echo, verb, shift, width;
        int   filterMode;     // 0 = dunkel (Highcut), 1 = hell (HP), 2 = Tunnel
        float pumpShape;      // hoeher = knackigerer Duck
        float echoBeats;      // Delay-Zeit in Beats
        float noiseTop;       // oberes Ende des Noise-Sweeps in Hz
        float verbSize;       // Reverb-Raumgroesse
    };

    // Stark getrennte Modi: jeder hat Effekte, die die anderen NICHT haben
    static constexpr ModeWeights modeTable[4] = {
        // A CLASSIC — sauberer heller Buildup: HP-Sweep + Noise + etwas Hall.
        //             KEIN Shift, kaum Echo, moderater 1/4-Pump.
        { 0.50f, 0.70f, 0.70f, 0.80f, 0.55f, 0.00f, 0.25f, 0.45f, 0.00f, 0.60f,
          1, 7.0f, 0.50f, 9000.0f, 0.80f },
        // B DARK — trockener Tunnel-Crusher: max Drive/Comp, dunkler Noise.
        //          KEIN Hall, KEIN Echo, KEIN Shift, KEINE Breite.
        { 1.00f, 1.00f, 1.00f, 0.60f, 0.75f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f,
          0, 6.0f, 0.50f, 3500.0f, 0.80f },
        // C STORM — Ambient-Chaos: Hall/Echo/Shift/Noise maximal, riesiger Raum,
        //           kaum Drive, kaum Pump. Schmiert breit in den Drop.
        { 0.30f, 0.50f, 0.80f, 1.00f, 0.20f, 0.50f, 1.00f, 1.00f, 1.00f, 1.00f,
          2, 5.0f, 0.75f, 12000.0f, 0.97f },
        // D PUMP — Sidechain-Monster: tiefer, knackiger Duck der beschleunigt
        //          (1/4 -> 1/8 -> 1/16), Echo dazu, KEIN Hall.
        { 0.70f, 0.85f, 0.50f, 0.70f, 1.00f, 1.00f, 0.60f, 0.00f, 0.25f, 0.90f,
          1, 12.0f, 0.50f, 9000.0f, 0.80f },
    };

    RiserKnobAudioProcessor();
    ~RiserKnobAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                       { return true; }

    const juce::String getName() const override           { return "Riser Knob"; }
    bool acceptsMidi() const override                      { return false; }
    bool producesMidi() const override                     { return false; }
    double getTailLengthSeconds() const override           { return 3.0; }

    int getNumPrograms() override                          { return 1; }
    int getCurrentProgram() override                       { return 0; }
    void setCurrentProgram (int) override                  {}
    const juce::String getProgramName (int) override       { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    juce::dsp::StateVariableTPTFilter<float> lowpass, highpass;
    juce::dsp::StateVariableTPTFilter<float> noiseBandpass;
    double flutterPhase = 0.0;
    SoftKneeCompressor comp;
    FreqShifter shifterL, shifterR;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> echoDelay { 192000 * 2 };
    juce::dsp::Reverb reverb;
    juce::SmoothedValue<float> riseSmoothed;

    float dcX1[2] { 0, 0 }, dcY1[2] { 0, 0 };
    juce::Random noiseL { 0x1234567 }, noiseR { 0x7654321 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RiserKnobAudioProcessor)
};

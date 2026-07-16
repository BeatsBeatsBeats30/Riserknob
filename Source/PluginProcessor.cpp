#include "PluginProcessor.h"
#include "PluginEditor.h"

constexpr RiserKnobAudioProcessor::ModeWeights RiserKnobAudioProcessor::modeTable[4];

// Schnelle tanh-Naeherung (Padé) — klingt identisch, spart massiv CPU im 4x-Loop
static inline float fastTanh (float x)
{
    x = juce::jlimit (-3.0f, 3.0f, x);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

RiserKnobAudioProcessor::RiserKnobAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout RiserKnobAudioProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "rise", 1 }, "Rise",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.0001f), 0.0f));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "mode", 1 }, "Mode",
        juce::StringArray { "A - Classic", "B - Dark", "C - Storm", "D - Pump" }, 0));

    return layout;
}

void RiserKnobAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const auto numCh = (juce::uint32) juce::jmax (2, getTotalNumOutputChannels());

    oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
        (size_t) numCh, 2,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);
    oversampling->initProcessing ((size_t) samplesPerBlock);
    setLatencySamples ((int) std::ceil (oversampling->getLatencyInSamples()));

    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = (juce::uint32) samplesPerBlock;
    spec.numChannels      = numCh;

    lowpass.prepare (spec);
    lowpass.setType (juce::dsp::StateVariableTPTFilter<float>::Type::lowpass);
    lowpass.setCutoffFrequency (20000.0f);
    lowpass.setResonance (0.7071f);

    highpass.prepare (spec);
    highpass.setType (juce::dsp::StateVariableTPTFilter<float>::Type::highpass);
    highpass.setCutoffFrequency (20.0f);
    highpass.setResonance (0.7071f);

    noiseBandpass.prepare (spec);
    noiseBandpass.setType (juce::dsp::StateVariableTPTFilter<float>::Type::bandpass);
    noiseBandpass.setCutoffFrequency (250.0f);
    noiseBandpass.setResonance (1.0f);
    flutterPhase = 0.0;

    comp.prepare (sampleRate);
    shifterL.prepare (sampleRate);
    shifterR.prepare (sampleRate);

    echoDelay.prepare (spec);
    echoDelay.setMaximumDelayInSamples ((int) (sampleRate * 2.0));
    echoDelay.reset();

    reverb.prepare (spec);
    reverb.reset();

    riseSmoothed.reset (sampleRate, 0.03);
    riseSmoothed.setCurrentAndTargetValue (apvts.getRawParameterValue ("rise")->load());

    dcX1[0] = dcX1[1] = dcY1[0] = dcY1[1] = 0.0f;
}

bool RiserKnobAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainOutputChannelSet() == layouts.getMainInputChannelSet();
}

void RiserKnobAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn      = getTotalNumInputChannels();
    const int numOut     = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();
    const double sr      = getSampleRate();

    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, numSamples);

    riseSmoothed.setTargetValue (apvts.getRawParameterValue ("rise")->load());
    const float rise = riseSmoothed.getNextValue();
    riseSmoothed.skip (juce::jmax (0, numSamples - 1));

    const int modeIdx = juce::jlimit (0, 3, (int) apvts.getRawParameterValue ("mode")->load());
    const ModeWeights& w = modeTable[modeIdx];

    double bpm = 124.0;
    double ppq = 0.0;
    bool playing = false;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (pos->getBpm().hasValue())         bpm = juce::jmax (20.0, *pos->getBpm());
            if (pos->getPpqPosition().hasValue()) ppq = *pos->getPpqPosition();
            playing = pos->getIsPlaying();
        }
    }

    // ---- Mapping ---------------------------------------------------------------
    const float driveAmt  = rise * w.drive;
    const float drive     = 1.0f + driveAmt * 6.0f;
    const float asym      = 0.12f * driveAmt;
    const float driveComp = 1.0f / std::sqrt (drive);

    const float compAmt = rise * w.comp;
    comp.setParams (-18.0f * compAmt, 1.0f + 3.5f * compAmt, 9.0f, 4.0f, 110.0f);

    float lpCut = 20000.0f, hpCut = 20.0f;
    if (w.filterMode == 0)       lpCut = 20000.0f * std::pow (1600.0f / 20000.0f, rise);
    else if (w.filterMode == 1)  hpCut =    20.0f * std::pow (900.0f  / 20.0f,    rise);
    else { hpCut = 20.0f * std::pow (400.0f / 20.0f, rise);
           lpCut = 20000.0f * std::pow (2000.0f / 20000.0f, rise); }

    const float reso = 0.7071f + 1.1f * rise * w.reso;
    lowpass.setCutoffFrequency (lpCut);
    highpass.setCutoffFrequency (hpCut);
    if (w.filterMode == 0) { lowpass.setResonance (reso);    highpass.setResonance (0.7071f); }
    else if (w.filterMode == 1) { highpass.setResonance (reso); lowpass.setResonance (0.7071f); }
    else { lowpass.setResonance (reso); highpass.setResonance (reso); }

    // Noise: konstant laut ab ~8%, der Knob morpht nur den Charakter
    const float noiseLevel = juce::jmin (1.0f, rise * 12.0f) * 0.11f * w.noise;
    const float nCut  = 250.0f * std::pow (w.noiseTop / 250.0f, rise);
    const float nReso = 1.0f + 3.0f * rise;
    const float nComp = 1.0f / std::sqrt (nReso);
    noiseBandpass.setCutoffFrequency (nCut);
    noiseBandpass.setResonance (nReso);
    const double flutterRate  = (double) (rise * rise * 16.0f);
    const float  flutterDepth = 0.40f * rise;

    const float pumpDepth = 0.40f * rise * w.pumpDepth;
    double pumpSub = 1.0;
    const float accel = rise * w.pumpAccel;
    if (accel > 0.30f) pumpSub = 0.5;
    if (accel > 0.55f) pumpSub = 0.25;

    const float echoFb   = juce::jlimit (0.0f, 0.75f, 0.65f * rise * w.echo);
    const float echoWet  = rise * rise * 0.60f * w.echo;
    const float delaySmp = juce::jlimit (32.0f, (float) (sr * 2.0) - 4.0f,
                                         (float) (60.0 / bpm * w.echoBeats * sr));

    const float verbWet = rise * rise * 0.50f * w.verb;

    const float shiftHz  = 250.0f * rise * rise * w.shift;
    const float shiftMix = juce::jmin (1.0f, rise * 1.4f) * 0.45f * w.shift;
    if (shiftMix > 0.001f)
    {
        shifterL.setShiftHz (shiftHz);
        shifterR.setShiftHz (shiftHz);
    }

    const float width    = 1.0f + 0.40f * rise * w.width;
    const float resoComp = 1.0f / (0.75f + 0.35f * reso);
    const float makeup   = juce::Decibels::decibelsToGain (compAmt * 5.0f + rise * 1.0f) * resoComp;

    // ==== 1) Saettigung (4x OS) — komplett uebersprungen, wenn kein Drive =======
    juce::dsp::AudioBlock<float> block (buffer);
    if (driveAmt > 0.003f)
    {
        auto osBlock = oversampling->processSamplesUp (block);
        for (size_t ch = 0; ch < osBlock.getNumChannels(); ++ch)
        {
            float* d = osBlock.getChannelPointer (ch);
            for (size_t i = 0; i < osBlock.getNumSamples(); ++i)
            {
                const float x = d[i] * drive;
                d[i] = fastTanh (x + asym * x * x) * driveComp;
            }
        }
        oversampling->processSamplesDown (block);

        // DC-Blocker nur noetig, wenn die asymmetrische Saettigung lief
        const float R = 1.0f - (float) (2.0 * juce::MathConstants<double>::pi * 20.0 / sr);
        for (int ch = 0; ch < juce::jmin (numOut, 2); ++ch)
        {
            float* d = buffer.getWritePointer (ch);
            for (int i = 0; i < numSamples; ++i)
            {
                const float x = d[i];
                const float y = x - dcX1[ch] + R * dcY1[ch];
                dcX1[ch] = x; dcY1[ch] = y;
                d[i] = y;
            }
        }
    }

    // ==== 2) Soft-Knee-Kompression — uebersprungen, wenn inaktiv ================
    if (compAmt > 0.003f)
    {
        float* l = buffer.getWritePointer (0);
        float* r = numOut > 1 ? buffer.getWritePointer (1) : nullptr;
        for (int i = 0; i < numSamples; ++i)
        {
            const float peak = r != nullptr ? juce::jmax (std::abs (l[i]), std::abs (r[i]))
                                            : std::abs (l[i]);
            const float g = comp.processSample (peak);
            l[i] *= g;
            if (r != nullptr) r[i] *= g;
        }
    }

    // ==== 3) Frequency-Shift (nur Modi mit Shift-Anteil) =========================
    if (shiftMix > 0.001f)
    {
        float* l = buffer.getWritePointer (0);
        float* r = numOut > 1 ? buffer.getWritePointer (1) : nullptr;
        for (int i = 0; i < numSamples; ++i)
        {
            l[i] = l[i] * (1.0f - shiftMix) + shifterL.processSample (l[i]) * shiftMix;
            if (r != nullptr)
                r[i] = r[i] * (1.0f - shiftMix) + shifterR.processSample (r[i]) * shiftMix;
        }
    }

    // ==== 4) Filter-Sweep ========================================================
    if (rise > 0.001f)
    {
        juce::dsp::ProcessContextReplacing<float> context (block);
        highpass.process (context);
        lowpass.process (context);
    }

    // ==== 5) Noise-Layer: konstant laut, Charakter wandert mit dem Knob ==========
    if (noiseLevel > 0.0001f)
    {
        const double fInc = juce::MathConstants<double>::twoPi * flutterRate / sr;
        float* l = buffer.getWritePointer (0);
        float* r = numOut > 1 ? buffer.getWritePointer (1) : nullptr;
        for (int i = 0; i < numSamples; ++i)
        {
            const float fl = 1.0f - flutterDepth * 0.5f
                                     * (1.0f + (float) std::sin (flutterPhase));
            flutterPhase += fInc;
            if (flutterPhase > juce::MathConstants<double>::twoPi)
                flutterPhase -= juce::MathConstants<double>::twoPi;

            const float g = noiseLevel * nComp * fl;
            l[i] += noiseBandpass.processSample (0, noiseL.nextFloat() * 2.0f - 1.0f) * g;
            if (r != nullptr)
                r[i] += noiseBandpass.processSample (1, noiseR.nextFloat() * 2.0f - 1.0f) * g;
        }
    }

    // ==== 6) Tempo-synces Echo ====================================================
    if (echoWet > 0.001f)
    {
        for (int ch = 0; ch < juce::jmin (numOut, 2); ++ch)
        {
            float* d = buffer.getWritePointer (ch);
            for (int i = 0; i < numSamples; ++i)
            {
                const float dl = echoDelay.popSample (ch, delaySmp, true);
                echoDelay.pushSample (ch, d[i] + dl * echoFb);
                d[i] += dl * echoWet;
            }
        }
    }
    else if (w.echo > 0.001f)
    {
        // Delay-Linie nur in Echo-Modi warmhalten (kein Knacks beim Aufdrehen)
        for (int ch = 0; ch < juce::jmin (numOut, 2); ++ch)
        {
            const float* d = buffer.getReadPointer (ch);
            for (int i = 0; i < numSamples; ++i)
            {
                echoDelay.popSample (ch, delaySmp, true);
                echoDelay.pushSample (ch, d[i]);
            }
        }
    }

    // ==== 7) Reverb-Wash — komplett uebersprungen, wenn trocken ==================
    if (verbWet > 0.001f)
    {
        juce::Reverb::Parameters rp;
        rp.roomSize   = w.verbSize;
        rp.damping    = 0.35f;
        rp.wetLevel   = verbWet;
        rp.dryLevel   = 1.0f;
        rp.width      = 1.0f;
        rp.freezeMode = 0.0f;
        reverb.setParameters (rp);

        juce::dsp::ProcessContextReplacing<float> context (block);
        reverb.process (context);
    }

    // ==== 8) Beat-Pump ============================================================
    if (pumpDepth > 0.001f && playing)
    {
        double p = ppq;
        const double ppqInc = bpm / 60.0 / sr;
        float* l = buffer.getWritePointer (0);
        float* r = numOut > 1 ? buffer.getWritePointer (1) : nullptr;
        for (int i = 0; i < numSamples; ++i)
        {
            const double local = p / pumpSub;
            const float phase  = (float) (local - std::floor (local));
            const float env    = std::exp (-phase * w.pumpShape);
            const float g      = 1.0f - pumpDepth * env;
            l[i] *= g;
            if (r != nullptr) r[i] *= g;
            p += ppqInc;
        }
    }

    // ==== 9) Stereo-Breite ========================================================
    if (numOut > 1 && width > 1.001f)
    {
        float* l = buffer.getWritePointer (0);
        float* r = buffer.getWritePointer (1);
        for (int i = 0; i < numSamples; ++i)
        {
            const float m = 0.5f * (l[i] + r[i]);
            const float s = 0.5f * (l[i] - r[i]) * width;
            l[i] = m + s;
            r[i] = m - s;
        }
    }

    // ==== 10) Auto-Makeup + weicher Safety-Limiter ================================
    auto softLimit = [] (float x)
    {
        const float ax = std::abs (x);
        if (ax <= 0.9f) return x;
        const float t = std::tanh ((ax - 0.9f) / 0.3f) * 0.3f;
        return (0.9f + t) * (x < 0.0f ? -1.0f : 1.0f);
    };

    if (rise > 0.001f)
    {
        for (int ch = 0; ch < numOut; ++ch)
        {
            float* d = buffer.getWritePointer (ch);
            for (int i = 0; i < numSamples; ++i)
                d[i] = softLimit (d[i] * makeup);
        }
    }
}

juce::AudioProcessorEditor* RiserKnobAudioProcessor::createEditor()
{
    return new RiserKnobAudioProcessorEditor (*this);
}

void RiserKnobAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); state.isValid())
        if (auto xml = state.createXml())
            copyXmlToBinary (*xml, destData);
}

void RiserKnobAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RiserKnobAudioProcessor();
}

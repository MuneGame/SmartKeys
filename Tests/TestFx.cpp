/*
  ==============================================================================

    TestFx.cpp
    SmartKeys — EFECTO de prueba (no se distribuye). Un plugin de audio que NO
    es un instrumento (VST3 "Fx|EQ"; en macOS también un Audio Unit de efecto,
    aufx). Las pruebas del catálogo lo ponen junto al instrumento de prueba para
    comprobar que la búsqueda de SmartKeys sólo ofrece generadores de sonido:
    este archivo no debe aparecer nunca en la lista de la ranura.

    Por dentro es una ganancia fija (-6 dB), sin MIDI.

  ==============================================================================
*/

#include <juce_audio_processors/juce_audio_processors.h>

namespace
{

class TestFxProcessor final : public juce::AudioProcessor
{
public:
    TestFxProcessor()
        : AudioProcessor (BusesProperties()
                              .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                              .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {
    }

    void prepareToPlay (double, int) override {}
    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        const auto out = layouts.getMainOutputChannelSet();
        return (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono())
                 && layouts.getMainInputChannelSet() == out;
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        buffer.applyGain (0.5f);
    }

    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override   { return new juce::GenericAudioProcessorEditor (*this); }
    bool hasEditor() const override                       { return true; }

    const juce::String getName() const override           { return "SmartKeysTestFx"; }
    bool acceptsMidi() const override                     { return false; }
    bool producesMidi() const override                    { return false; }
    double getTailLengthSeconds() const override          { return 0.0; }
    int getNumPrograms() override                         { return 1; }
    int getCurrentProgram() override                      { return 0; }
    void setCurrentProgram (int) override                 {}
    const juce::String getProgramName (int) override      { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TestFxProcessor)
};

} // namespace

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TestFxProcessor();
}

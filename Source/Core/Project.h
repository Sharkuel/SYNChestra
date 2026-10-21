#pragma once
#include "SampleMap.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace syn
{
/** Everything needed to record a library again: the rows, and the plugin with the sound that
    was picked in it. Saved as a .synchestra file. */
struct Project
{
    static constexpr const char* fileExtension = ".synchestra";

    SampleMap map;
    std::optional<juce::PluginDescription> plugin;
    juce::MemoryBlock pluginState;   // the plugin's own save of its settings (the patch, the kit...)

    std::unique_ptr<juce::XmlElement> toXml() const;
    static std::optional<Project> fromXml (const juce::XmlElement&);

    bool save (const juce::File&) const;
    static std::optional<Project> load (const juce::File&);
};
} // namespace syn

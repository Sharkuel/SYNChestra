#include "Project.h"

namespace syn
{
std::unique_ptr<juce::XmlElement> Project::toXml() const
{
    auto xml = std::make_unique<juce::XmlElement> ("SYNChestraProject");
    xml->setAttribute ("version", 1);
    if (auto mapXml = map.toValueTree().createXml())
        xml->addChildElement (mapXml.release());
    if (plugin.has_value())
    {
        auto* p = xml->createNewChildElement ("Plugin");
        if (auto description = plugin->createXml())
            p->addChildElement (description.release());
        p->createNewChildElement ("State")->addTextElement (pluginState.toBase64Encoding());
    }
    return xml;
}

std::optional<Project> Project::fromXml (const juce::XmlElement& xml)
{
    if (! xml.hasTagName ("SYNChestraProject"))
        return {};
    Project project;
    if (auto* mapXml = xml.getChildByName ("SampleMap"))
        project.map = SampleMap::fromValueTree (juce::ValueTree::fromXml (*mapXml));
    if (auto* p = xml.getChildByName ("Plugin"))
    {
        juce::PluginDescription description;
        if (auto* d = p->getChildByName ("PLUGIN"); d != nullptr && description.loadFromXml (*d))
            project.plugin = description;
        if (auto* state = p->getChildByName ("State"))
            project.pluginState.fromBase64Encoding (state->getAllSubText().trim());
    }
    return project;
}

bool Project::save (const juce::File& file) const
{
    auto xml = toXml();
    return xml != nullptr && xml->writeTo (file);
}

std::optional<Project> Project::load (const juce::File& file)
{
    if (auto xml = juce::parseXML (file))
        return fromXml (*xml);
    return {};
}
} // namespace syn

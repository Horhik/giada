/* -----------------------------------------------------------------------------
 *
 * Giada - Your Hardcore Loopmachine
 *
 * -----------------------------------------------------------------------------
 *
 * Copyright (C) 2010-2021 Giovanni A. Zuliani | Monocasual
 *
 * This file is part of Giada - Your Hardcore Loopmachine.
 *
 * Giada - Your Hardcore Loopmachine is free software: you can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License as published by the Free Software Foundation, either
 * version 3 of the License, or (at your option) any later version.
 *
 * Giada - Your Hardcore Loopmachine is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Giada - Your Hardcore Loopmachine. If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * -------------------------------------------------------------------------- */

#ifdef WITH_VST

#include "pluginManager.h"
#include "core/conf.h"
#include "core/const.h"
#include "core/kernelAudio.h"
#include "core/model/model.h"
#include "core/patch.h"
#include "core/plugins/plugin.h"
#include "utils/fs.h"
#include "utils/log.h"
#include "utils/string.h"
#include <cassert>

extern giada::m::model::Model g_model;
extern giada::m::conf::Data   g_conf;
extern giada::m::KernelAudio  g_kernelAudio;

namespace giada::m
{
PluginManager::PluginManager()
{
	formatManager_.addDefaultFormats();
	reset();
}

/* -------------------------------------------------------------------------- */

void PluginManager::reset()
{
	pluginId_       = IdManager();
	missingPlugins_ = false;

	unknownPluginList_.clear();

	loadList(u::fs::getHomePath() + G_SLASH + "plugins.xml");
	sortPlugins(static_cast<SortMethod>(g_conf.pluginSortMethod));
}

/* -------------------------------------------------------------------------- */

int PluginManager::scanDirs(const std::string& dirs, const std::function<void(float)>& cb)
{
	u::log::print("[pluginManager::scanDir] requested directories: '%s'\n", dirs);
	u::log::print("[pluginManager::scanDir] current plugins: %d\n", knownPluginList_.getNumTypes());

	knownPluginList_.clear(); // clear up previous plugins

	std::vector<std::string> dirVec = u::string::split(dirs, ";");

	juce::FileSearchPath searchPath;
	for (const std::string& dir : dirVec)
		searchPath.add(juce::File(dir));

	for (int i = 0; i < formatManager_.getNumFormats(); i++)
	{

		juce::PluginDirectoryScanner scanner(knownPluginList_, *formatManager_.getFormat(i), searchPath,
		    /*recursive=*/true, juce::File());

		juce::String name;
		while (scanner.scanNextFile(false, name))
		{
			u::log::print("[pluginManager::scanDir]   scanning '%s'\n", name.toRawUTF8());
			cb(scanner.getProgress());
		}
	}

	u::log::print("[pluginManager::scanDir] %d plugin(s) found\n", knownPluginList_.getNumTypes());
	return knownPluginList_.getNumTypes();
}

/* -------------------------------------------------------------------------- */

bool PluginManager::saveList(const std::string& filepath)
{
	bool out = knownPluginList_.createXml()->writeTo(juce::File(filepath));
	if (!out)
		u::log::print("[pluginManager::saveList] unable to save plugin list to %s\n", filepath);
	return out;
}

/* -------------------------------------------------------------------------- */

bool PluginManager::loadList(const std::string& filepath)
{
	std::unique_ptr<juce::XmlElement> elem(juce::XmlDocument::parse(juce::File(filepath)));
	if (elem == nullptr)
		return false;
	knownPluginList_.recreateFromXml(*elem);
	return true;
}

/* -------------------------------------------------------------------------- */

std::unique_ptr<Plugin> PluginManager::makePlugin(const std::string& pid, ID id)
{
	/* Plug-in ID generator is updated anyway, as we store Plugin objects also
	if they are in an invalid state. */

	pluginId_.set(id);

	const std::unique_ptr<juce::PluginDescription> pd = knownPluginList_.getTypeForIdentifierString(pid);
	if (pd == nullptr)
	{
		u::log::print("[pluginManager::makePlugin] no plugin found with pid=%s!\n", pid);
		return makeInvalidPlugin_(pid, id);
	}

	const int sampleRate = g_conf.samplerate;
	const int bufferSize = g_kernelAudio.getRealBufSize();

	juce::String                               error;
	std::unique_ptr<juce::AudioPluginInstance> pi = formatManager_.createPluginInstance(*pd, sampleRate, bufferSize, error);
	if (pi == nullptr)
	{
		u::log::print("[pluginManager::makePlugin] unable to create instance with pid=%s! Error: %s\n",
		    pid, error.toStdString());
		return makeInvalidPlugin_(pid, id);
	}

	u::log::print("[pluginManager::makePlugin] plugin instance with pid=%s created\n", pid);

	return std::make_unique<Plugin>(pluginId_.generate(id), std::move(pi), sampleRate, bufferSize);
}

/* -------------------------------------------------------------------------- */

std::unique_ptr<Plugin> PluginManager::makePlugin(int index)
{
	juce::PluginDescription pd = knownPluginList_.getTypes()[index];

	if (pd.uniqueId == 0) // Invalid
		return {};

	u::log::print("[pluginManager::makePlugin] plugin found, uid=%s, name=%s...\n",
	    pd.createIdentifierString().toRawUTF8(), pd.name.toRawUTF8());

	return makePlugin(pd.createIdentifierString().toStdString());
}

/* -------------------------------------------------------------------------- */

std::unique_ptr<Plugin> PluginManager::makePlugin(const Plugin& src)
{
	std::unique_ptr<Plugin> p = makePlugin(src.getUniqueId());

	for (int i = 0; i < src.getNumParameters(); i++)
		p->setParameter(i, src.getParameter(i));

	return p;
}

/* -------------------------------------------------------------------------- */

const patch::Plugin PluginManager::serializePlugin(const Plugin& p)
{
	patch::Plugin pp;
	pp.id     = p.id;
	pp.path   = p.getUniqueId();
	pp.bypass = p.isBypassed();
	pp.state  = p.getState().asBase64();

	for (const MidiLearnParam& param : p.midiInParams)
		pp.midiInParams.push_back(param.getValue());

	return pp;
}

/* -------------------------------------------------------------------------- */

std::unique_ptr<Plugin> PluginManager::deserializePlugin(const patch::Plugin& p, patch::Version version)
{
	std::unique_ptr<Plugin> plugin = makePlugin(p.path, p.id);
	if (!plugin->valid)
		return plugin; // Return invalid version

	/* Fill plug-in parameters. */
	plugin->setBypass(p.bypass);

	if (version < patch::Version{0, 17, 0}) // TODO - to be removed in 0.18.0
		for (unsigned j = 0; j < p.params.size(); j++)
			plugin->setParameter(j, p.params.at(j));
	else
		plugin->setState(PluginState(p.state));

	/* Fill plug-in MidiIn parameters. Don't fill Plugin::midiInParam if 
	Patch::midiInParams are zero: it would wipe out the current default 0x0
	values. */

	if (!p.midiInParams.empty())
	{
		plugin->midiInParams.clear();
		std::size_t paramIndex = 0;
		for (uint32_t midiInParam : p.midiInParams)
			plugin->midiInParams.emplace_back(midiInParam, paramIndex++);
	}

	return plugin;
}

/* -------------------------------------------------------------------------- */

std::vector<Plugin*> PluginManager::hydratePlugins(std::vector<ID> pluginIds)
{
	std::vector<Plugin*> out;
	for (ID id : pluginIds)
	{
		Plugin* plugin = g_model.find<Plugin>(id);
		if (plugin != nullptr)
			out.push_back(plugin);
	}
	return out;
}

/* -------------------------------------------------------------------------- */

int PluginManager::countAvailablePlugins()
{
	return knownPluginList_.getNumTypes();
}

/* -------------------------------------------------------------------------- */

int PluginManager::countUnknownPlugins()
{
	return unknownPluginList_.size();
}

/* -------------------------------------------------------------------------- */

PluginManager::PluginInfo PluginManager::getAvailablePluginInfo(int i)
{
	juce::PluginDescription pd = knownPluginList_.getTypes()[i];
	PluginInfo              pi;
	pi.uid              = pd.fileOrIdentifier.toStdString();
	pi.name             = pd.descriptiveName.toStdString();
	pi.category         = pd.category.toStdString();
	pi.manufacturerName = pd.manufacturerName.toStdString();
	pi.format           = pd.pluginFormatName.toStdString();
	pi.isInstrument     = pd.isInstrument;
	return pi;
}

/* -------------------------------------------------------------------------- */

bool PluginManager::hasMissingPlugins()
{
	return missingPlugins_;
}

/* -------------------------------------------------------------------------- */

std::string PluginManager::getUnknownPluginInfo(int i)
{
	return unknownPluginList_.at(i);
}

/* -------------------------------------------------------------------------- */

bool PluginManager::doesPluginExist(const std::string& pid)
{
	return formatManager_.doesPluginStillExist(*knownPluginList_.getTypeForFile(pid));
}

/* -------------------------------------------------------------------------- */

void PluginManager::sortPlugins(SortMethod method)
{
	switch (method)
	{
	case SortMethod::NAME:
		knownPluginList_.sort(juce::KnownPluginList::SortMethod::sortAlphabetically, true);
		break;
	case SortMethod::CATEGORY:
		knownPluginList_.sort(juce::KnownPluginList::SortMethod::sortByCategory, true);
		break;
	case SortMethod::MANUFACTURER:
		knownPluginList_.sort(juce::KnownPluginList::SortMethod::sortByManufacturer, true);
		break;
	case SortMethod::FORMAT:
		knownPluginList_.sort(juce::KnownPluginList::SortMethod::sortByFormat, true);
		break;
	}
}

/* -------------------------------------------------------------------------- */

std::unique_ptr<Plugin> PluginManager::makeInvalidPlugin_(const std::string& pid, ID id)
{
	missingPlugins_ = true;
	unknownPluginList_.push_back(pid);
	return std::make_unique<Plugin>(pluginId_.generate(id), pid); // Invalid plug-in
}
} // namespace giada::m

#endif // #ifdef WITH_VST

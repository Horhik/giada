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

#include "core/plugins/pluginHost.h"
#include "core/channels/channel.h"
#include "core/clock.h"
#include "core/const.h"
#include "core/model/model.h"
#include "core/plugins/plugin.h"
#include "core/plugins/pluginManager.h"
#include "deps/mcl-audio-buffer/src/audioBuffer.hpp"
#include "utils/log.h"
#include "utils/vector.h"
#include <cassert>

extern giada::m::Clock g_clock;

namespace giada::m
{
bool PluginHost::Info::getCurrentPosition(CurrentPositionInfo& result)
{
	result.bpm           = g_clock.getBpm();
	result.timeInSamples = g_clock.getCurrentFrame();
	result.timeInSeconds = g_clock.getCurrentSecond();
	result.isPlaying     = g_clock.isRunning();

	return true;
}

/* -------------------------------------------------------------------------- */

bool PluginHost::Info::canControlTransport()
{
	return false;
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

PluginHost::PluginHost(int bufferSize)
{
	m_messageManager = juce::MessageManager::getInstance();
	reset(bufferSize);
}

/* -------------------------------------------------------------------------- */

PluginHost::~PluginHost()
{
	m_messageManager->deleteInstance();
}

/* -------------------------------------------------------------------------- */

void PluginHost::reset(int bufferSize)
{
	model::clear<model::PluginPtrs>();
	m_audioBuffer.setSize(G_MAX_IO_CHANS, bufferSize);
}

/* -------------------------------------------------------------------------- */

void PluginHost::processStack(mcl::AudioBuffer& outBuf, const std::vector<Plugin*>& plugins,
    juce::MidiBuffer* events)
{
	assert(outBuf.countFrames() == m_audioBuffer.getNumSamples());

	/* If events are null: Audio stack processing (master in, master out or
	sample channels. No need for MIDI events. 
	If events are not null: MIDI stack (MIDI channels). MIDI channels must not 
	process the current buffer: give them an empty and clean one. */

	if (events == nullptr)
	{
		giadaToJuceTempBuf(outBuf);
		juce::MidiBuffer dummyEvents; // empty
		processPlugins(plugins, dummyEvents);
	}
	else
	{
		m_audioBuffer.clear();
		processPlugins(plugins, *events);
	}
	juceToGiadaOutBuf(outBuf);
}

/* -------------------------------------------------------------------------- */

void PluginHost::addPlugin(std::unique_ptr<Plugin> p, ID channelId)
{
	model::add(std::move(p));

	const Plugin& pluginRef = model::back<Plugin>();

	/* TODO - unfortunately JUCE wants mutable plugin objects due to the
	presence of the non-const processBlock() method. Why not const_casting
	only in the Plugin class? */
	model::get().getChannel(channelId).plugins.push_back(const_cast<Plugin*>(&pluginRef));
	model::swap(model::SwapType::HARD);
}

/* -------------------------------------------------------------------------- */

void PluginHost::swapPlugin(const m::Plugin& p1, const m::Plugin& p2, ID channelId)
{
	std::vector<m::Plugin*>& pvec   = model::get().getChannel(channelId).plugins;
	std::size_t              index1 = u::vector::indexOf(pvec, &p1);
	std::size_t              index2 = u::vector::indexOf(pvec, &p2);
	std::swap(pvec.at(index1), pvec.at(index2));

	model::swap(model::SwapType::HARD);
}

/* -------------------------------------------------------------------------- */

void PluginHost::freePlugin(const m::Plugin& plugin, ID channelId)
{
	u::vector::remove(model::get().getChannel(channelId).plugins, &plugin);
	model::swap(model::SwapType::HARD);
	model::remove(plugin);
}

void PluginHost::freePlugins(const std::vector<Plugin*>& plugins)
{
	// TODO - channels???
	for (const Plugin* p : plugins)
		model::remove(*p);
}

/* -------------------------------------------------------------------------- */

std::vector<Plugin*> PluginHost::clonePlugins(const std::vector<Plugin*>& plugins)
{
	std::vector<Plugin*> out;
	for (const Plugin* p : plugins)
	{
		model::add(pluginManager::makePlugin(*p));
		out.push_back(&model::back<Plugin>());
	}
	return out;
}

/* -------------------------------------------------------------------------- */

void PluginHost::setPluginParameter(ID pluginId, int paramIndex, float value)
{
	model::find<Plugin>(pluginId)->setParameter(paramIndex, value);
}

/* -------------------------------------------------------------------------- */

void PluginHost::setPluginProgram(ID pluginId, int programIndex)
{
	model::find<Plugin>(pluginId)->setCurrentProgram(programIndex);
}

/* -------------------------------------------------------------------------- */

void PluginHost::toggleBypass(ID pluginId)
{
	Plugin& plugin = *model::find<Plugin>(pluginId);
	plugin.setBypass(!plugin.isBypassed());
}

/* -------------------------------------------------------------------------- */

void PluginHost::runDispatchLoop()
{
	m_messageManager->runDispatchLoopUntil(10);
}

/* -------------------------------------------------------------------------- */

void PluginHost::giadaToJuceTempBuf(const mcl::AudioBuffer& outBuf)
{
	for (int i = 0; i < outBuf.countFrames(); i++)
		for (int j = 0; j < outBuf.countChannels(); j++)
			m_audioBuffer.setSample(j, i, outBuf[i][j]);
}

/* juceToGiadaOutBuf
Converts buffer from Juce to Giada. A note for the future: if we overwrite (=) 
(as we do now) it's SEND, if we add (+) it's INSERT. */

void PluginHost::juceToGiadaOutBuf(mcl::AudioBuffer& outBuf) const
{
	for (int i = 0; i < outBuf.countFrames(); i++)
		for (int j = 0; j < outBuf.countChannels(); j++)
			outBuf[i][j] = m_audioBuffer.getSample(j, i);
}

/* -------------------------------------------------------------------------- */

void PluginHost::processPlugins(const std::vector<Plugin*>& plugins, juce::MidiBuffer& events)
{
	for (Plugin* p : plugins)
	{
		if (!p->valid || p->isSuspended() || p->isBypassed())
			continue;
		p->process(m_audioBuffer, events);
	}
	events.clear();
}
} // namespace giada::m

#endif // #ifdef WITH_VST

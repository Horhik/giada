/* -----------------------------------------------------------------------------
 *
 * Giada - Your Hardcore Loopmachine
 *
 * -----------------------------------------------------------------------------
 *
 * Copyright (C) 2010-2020 Giovanni A. Zuliani | Monocasual
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

#include "eventDispatcher.h"
#include "core/clock.h"
#include "core/const.h"
#include "core/midiDispatcher.h"
#include "core/model/model.h"
#include "core/sequencer.h"
#include "utils/log.h"
#include <functional>

extern giada::m::model::Model   g_model;
extern giada::m::Sequencer      g_sequencer;
extern giada::m::Mixer          g_mixer;
extern giada::m::MidiDispatcher g_midiDispatcher;

namespace giada::m
{
EventDispatcher::EventDispatcher()
{
	m_worker.start([this]() { process(); }, /*sleep=*/G_EVENT_DISPATCHER_RATE_MS);
}

/* -------------------------------------------------------------------------- */

void EventDispatcher::pumpUIevent(Event e) { UIevents.push(e); }
void EventDispatcher::pumpMidiEvent(Event e) { MidiEvents.push(e); }

/* -------------------------------------------------------------------------- */

void EventDispatcher::processFuntions()
{
	for (const Event& e : m_eventBuffer)
	{
		switch (e.type)
		{
		case EventType::MIDI_DISPATCHER_LEARN:
			g_midiDispatcher.learn(std::get<Action>(e.data).event);
			break;

		case EventType::MIDI_DISPATCHER_PROCESS:
			g_midiDispatcher.process(std::get<Action>(e.data).event);
			break;

		case EventType::MIXER_SIGNAL_CALLBACK:
			g_mixer.execSignalCb();
			break;

		case EventType::MIXER_END_OF_REC_CALLBACK:
			g_mixer.execEndOfRecCb();
			break;

		default:
			break;
		}
	}
}

/* -------------------------------------------------------------------------- */

void EventDispatcher::processChannels()
{
	for (channel::Data& ch : g_model.get().channels)
		channel::react(ch, m_eventBuffer, g_mixer.isChannelAudible(ch));
	g_model.swap(model::SwapType::SOFT);
}

/* -------------------------------------------------------------------------- */

void EventDispatcher::processSequencer()
{
	g_sequencer.react(m_eventBuffer);
}

/* -------------------------------------------------------------------------- */

void EventDispatcher::process()
{
	m_eventBuffer.clear();

	Event e;
	while (UIevents.pop(e))
		m_eventBuffer.push_back(e);
	while (MidiEvents.pop(e))
		m_eventBuffer.push_back(e);

	if (m_eventBuffer.size() == 0)
		return;

	processFuntions();
	processChannels();
	processSequencer();
}
} // namespace giada::m
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

#ifndef G_SEQUENCER_H
#define G_SEQUENCER_H

#include "core/eventDispatcher.h"
#include "core/metronome.h"
#include "core/quantizer.h"
#include <vector>

namespace mcl
{
class AudioBuffer;
}

namespace giada::m
{
class KernelAudio;
class Clock;
class ActionRecorder;
class Sequencer final
{
public:
	enum class EventType
	{
		NONE,
		FIRST_BEAT,
		BAR,
		REWIND,
		ACTIONS
	};

	struct Event
	{
		EventType                  type    = EventType::NONE;
		Frame                      global  = 0;
		Frame                      delta   = 0;
		const std::vector<Action>* actions = nullptr;
	};

	using EventBuffer = RingBuffer<Event, G_MAX_SEQUENCER_EVENTS>;

	Sequencer(KernelAudio&, Clock&);

	/* reset
	Brings everything back to the initial state. */

	void reset();

	/* react
	Reacts to live events coming from the EventDispatcher (human events). */

	void react(const EventDispatcher::EventBuffer&);

	/* advance
	Parses sequencer events that might occur in a block and advances the internal 
	quantizer. Returns a reference to the internal EventBuffer filled with events
	(if any). Call this on each new audio block. */

	const EventBuffer& advance(Frame bufferSize, const ActionRecorder&);

	/* render
	Renders audio coming out from the sequencer: that is, the metronome! */

	void render(mcl::AudioBuffer& outBuf);

	/* raw[*]
	Raw functions to start, stop and rewind the sequencer. These functions must 
	be called only by clock:: when the JACK signal is received. Other modules 
	should use the non-raw versions below. */

	void rawStart();
	void rawStop();
	void rawRewind();

	void start();
	void stop();
	void rewind();

	bool isMetronomeOn();
	void toggleMetronome();
	void setMetronome(bool v);

	/* quantizer
	Used by the sequencer itself and each sample channel. */

	Quantizer quantizer;

	std::function<void()> onStartFromWait;
	std::function<void()> onStop;

private:
	void rewindQ(Frame delta);

	KernelAudio& m_kernelAudio;
	Clock&       m_clock;

	/* m_eventBuffer
	Buffer of events found in each block sent to channels for event parsing. 
	This is filled during react(). */

	EventBuffer m_eventBuffer;

	Metronome m_metronome;
};
} // namespace giada::m

#endif

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

#include "core/sequencer.h"
#include "core/actions/actionRecorder.h"
#include "core/clock.h"
#include "core/kernelAudio.h"
#include "core/metronome.h"
#include "core/quantizer.h"

namespace giada::m
{
namespace
{
constexpr int Q_ACTION_REWIND = 0;
} // namespace

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

Sequencer::Sequencer(Clock& c)
: onAboutStart(nullptr)
, onAboutStop(nullptr)
, m_clock(c)
{
	reset();
	quantizer.schedule(Q_ACTION_REWIND, [this](Frame delta) { rewindQ(delta); });
}

/* -------------------------------------------------------------------------- */

void Sequencer::reset()
{
	m_clock.rewind();
}

/* -------------------------------------------------------------------------- */

void Sequencer::react(const EventDispatcher::EventBuffer& events, const KernelAudio& kernelAudio)
{
	for (const EventDispatcher::Event& e : events)
	{
		if (e.type == EventDispatcher::EventType::SEQUENCER_START)
		{
			if (!kernelAudio.jackStart())
				rawStart();
			break;
		}
		if (e.type == EventDispatcher::EventType::SEQUENCER_STOP)
		{
			if (!kernelAudio.jackStop())
				rawStop();
			break;
		}
		if (e.type == EventDispatcher::EventType::SEQUENCER_REWIND)
		{
			if (!kernelAudio.jackSetPosition(0))
				rawRewind();
			break;
		}
	}
}

/* -------------------------------------------------------------------------- */

const Sequencer::EventBuffer& Sequencer::advance(Frame bufferSize, const ActionRecorder& actionRecorder)
{
	m_eventBuffer.clear();

	const Frame start        = m_clock.getCurrentFrame();
	const Frame end          = start + bufferSize;
	const Frame framesInLoop = m_clock.getFramesInLoop();
	const Frame framesInBar  = m_clock.getFramesInBar();
	const Frame framesInBeat = m_clock.getFramesInBeat();

	for (Frame i = start, local = 0; i < end; i++, local++)
	{

		Frame global = i % framesInLoop; // wraps around 'framesInLoop'

		if (global == 0)
		{
			m_eventBuffer.push_back({EventType::FIRST_BEAT, global, local});
			m_metronome.trigger(Metronome::Click::BEAT, local);
		}
		else if (global % framesInBar == 0)
		{
			m_eventBuffer.push_back({EventType::BAR, global, local});
			m_metronome.trigger(Metronome::Click::BAR, local);
		}
		else if (global % framesInBeat == 0)
		{
			m_metronome.trigger(Metronome::Click::BEAT, local);
		}

		const std::vector<Action>* as = actionRecorder.getActionsOnFrame(global);
		if (as != nullptr)
			m_eventBuffer.push_back({EventType::ACTIONS, global, local, as});
	}

	/* Advance clock and quantizer after the event parsing. */
	m_clock.advance(bufferSize);
	quantizer.advance(Range<Frame>(start, end), m_clock.getQuantizerStep());

	return m_eventBuffer;
}

/* -------------------------------------------------------------------------- */

void Sequencer::render(mcl::AudioBuffer& outBuf)
{
	if (m_metronome.running)
		m_metronome.render(outBuf);
}

/* -------------------------------------------------------------------------- */

void Sequencer::rawStart()
{
	assert(onAboutStart != nullptr);

	const ClockStatus status = m_clock.getStatus();
	onAboutStart(status);

	switch (status)
	{
	case ClockStatus::STOPPED:
		m_clock.setStatus(ClockStatus::RUNNING);
		break;
	case ClockStatus::WAITING:
		m_clock.setStatus(ClockStatus::RUNNING);
		break;
	default:
		break;
	}
}

/* -------------------------------------------------------------------------- */

void Sequencer::rawStop()
{
	assert(onAboutStop != nullptr);

	onAboutStop();
	m_clock.setStatus(ClockStatus::STOPPED);
}

/* -------------------------------------------------------------------------- */

void Sequencer::rawRewind()
{
	if (m_clock.canQuantize())
		quantizer.trigger(Q_ACTION_REWIND);
	else
		rewindQ(/*delta=*/0);
}

/* -------------------------------------------------------------------------- */

bool Sequencer::isMetronomeOn() { return m_metronome.running; }
void Sequencer::toggleMetronome() { m_metronome.running = !m_metronome.running; }
void Sequencer::setMetronome(bool v) { m_metronome.running = v; }

/* -------------------------------------------------------------------------- */

void Sequencer::rewindQ(Frame delta)
{
	m_clock.rewind();
	m_eventBuffer.push_back({EventType::REWIND, 0, delta});
}
} // namespace giada::m
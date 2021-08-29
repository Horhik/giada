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

#include "sequencer.h"
#include "core/clock.h"
#include "core/conf.h"
#include "core/const.h"
#include "core/kernelAudio.h"
#include "core/metronome.h"
#include "core/mixer.h"
#include "core/model/model.h"
#include "core/quantizer.h"
#include "core/recorder.h"

extern giada::m::KernelAudio g_kernelAudio;
extern giada::m::Clock       g_clock;
extern giada::m::Actions     g_actions;
extern giada::m::Recorder    g_recorder;

namespace giada::m
{
namespace
{
constexpr int Q_ACTION_REWIND = 0;
} // namespace

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

Sequencer::Sequencer(KernelAudio& k, Clock& c)
: m_kernelAudio(k)
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

void Sequencer::react(const EventDispatcher::EventBuffer& events)
{
	for (const EventDispatcher::Event& e : events)
	{
		if (e.type == EventDispatcher::EventType::SEQUENCER_START)
		{
			start();
			break;
		}
		if (e.type == EventDispatcher::EventType::SEQUENCER_STOP)
		{
			stop();
			break;
		}
		if (e.type == EventDispatcher::EventType::SEQUENCER_REWIND)
		{
			rewind();
			break;
		}
	}
}

/* -------------------------------------------------------------------------- */

const Sequencer::EventBuffer& Sequencer::advance(Frame bufferSize)
{
	m_eventBuffer.clear();

	const Frame start        = g_clock.getCurrentFrame();
	const Frame end          = start + bufferSize;
	const Frame framesInLoop = g_clock.getFramesInLoop();
	const Frame framesInBar  = g_clock.getFramesInBar();
	const Frame framesInBeat = g_clock.getFramesInBeat();

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

		const std::vector<Action>* as = g_actions.getActionsOnFrame(global);
		if (as != nullptr)
			m_eventBuffer.push_back({EventType::ACTIONS, global, local, as});
	}

	/* Advance clock and quantizer after the event parsing. */
	g_clock.advance(bufferSize);
	quantizer.advance(Range<Frame>(start, end), g_clock.getQuantizerStep());

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
	switch (g_clock.getStatus())
	{
	case ClockStatus::STOPPED:
		g_clock.setStatus(ClockStatus::RUNNING);
		break;
	case ClockStatus::WAITING:
		g_clock.setStatus(ClockStatus::RUNNING);
		g_recorder.stopActionRec();
		break;
	default:
		break;
	}
}

/* -------------------------------------------------------------------------- */

void Sequencer::rawStop()
{
	g_clock.setStatus(ClockStatus::STOPPED);

	/* If recordings (both input and action) are active deactivate them, but 
	store the takes. RecManager takes care of it. */

	if (g_recorder.isRecordingAction())
		g_recorder.stopActionRec();
	else if (g_recorder.isRecordingInput())
		g_recorder.stopInputRec(conf::conf.inputRecMode);
}

/* -------------------------------------------------------------------------- */

void Sequencer::rawRewind()
{
	if (g_clock.canQuantize())
		quantizer.trigger(Q_ACTION_REWIND);
	else
		rewindQ(/*delta=*/0);
}

/* -------------------------------------------------------------------------- */

void Sequencer::start()
{
#ifdef WITH_AUDIO_JACK
	if (g_kernelAudio.getAPI() == G_SYS_API_JACK)
		g_kernelAudio.jackStart();
	else
#endif
		rawStart();
}

/* -------------------------------------------------------------------------- */

void Sequencer::stop()
{
#ifdef WITH_AUDIO_JACK
	if (g_kernelAudio.getAPI() == G_SYS_API_JACK)
		g_kernelAudio.jackStop();
	else
#endif
		rawStop();
}

/* -------------------------------------------------------------------------- */

void Sequencer::rewind()
{
#ifdef WITH_AUDIO_JACK
	if (g_kernelAudio.getAPI() == G_SYS_API_JACK)
		g_kernelAudio.jackSetPosition(0);
	else
#endif
		rawRewind();
}

/* -------------------------------------------------------------------------- */

bool Sequencer::isMetronomeOn() { return m_metronome.running; }
void Sequencer::toggleMetronome() { m_metronome.running = !m_metronome.running; }
void Sequencer::setMetronome(bool v) { m_metronome.running = v; }

/* -------------------------------------------------------------------------- */

void Sequencer::rewindQ(Frame delta)
{
	g_clock.rewind();
	m_eventBuffer.push_back({EventType::REWIND, 0, delta});
}
} // namespace giada::m
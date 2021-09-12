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

#include "core/clock.h"
#include "core/const.h"
#include "core/kernelAudio.h"
#include "core/mixerHandler.h"
#include "core/model/model.h"
#include "core/sequencer.h"
#include "core/sync.h"
#include "glue/events.h"
#include "src/core/actions/actionRecorder.h"
#include "utils/log.h"
#include "utils/math.h"
#include <atomic>
#include <cassert>

extern giada::m::model::Model   g_model;
extern giada::m::ActionRecorder g_actionRecorder;

namespace giada::m
{
Clock::Clock(Synchronizer& s, int sampleRate)
: m_synchronizer(s)
, m_quantizerStep(1)
{
	reset(sampleRate);
}

/* -------------------------------------------------------------------------- */

float     Clock::getBpm() const { return g_model.get().sequencer.bpm; }
int       Clock::getBeats() const { return g_model.get().sequencer.beats; }
int       Clock::getBars() const { return g_model.get().sequencer.bars; }
int       Clock::getCurrentBeat() const { return g_model.get().sequencer.state->currentBeat.load(); }
int       Clock::getCurrentFrame() const { return g_model.get().sequencer.state->currentFrame.load(); }
float     Clock::getCurrentSecond(int sampleRate) const { return getCurrentFrame() / static_cast<float>(sampleRate); }
int       Clock::getFramesInBar() const { return g_model.get().sequencer.framesInBar; }
int       Clock::getFramesInBeat() const { return g_model.get().sequencer.framesInBeat; }
int       Clock::getFramesInLoop() const { return g_model.get().sequencer.framesInLoop; }
int       Clock::getFramesInSeq() const { return g_model.get().sequencer.framesInSeq; }
int       Clock::getQuantizerValue() const { return g_model.get().sequencer.quantize; }
int       Clock::getQuantizerStep() const { return m_quantizerStep; }
SeqStatus Clock::getStatus() const { return g_model.get().sequencer.status; }

/* -------------------------------------------------------------------------- */

bool Clock::isRunning() const
{
	return g_model.get().sequencer.status == SeqStatus::RUNNING;
}

/* -------------------------------------------------------------------------- */

bool Clock::isActive() const
{
	const model::Sequencer& c = g_model.get().sequencer;
	return c.status == SeqStatus::RUNNING || c.status == SeqStatus::WAITING;
}

/* -------------------------------------------------------------------------- */

bool Clock::isOnBar() const
{
	const model::Sequencer& c = g_model.get().sequencer;

	int currentFrame = c.state->currentFrame.load();

	if (c.status == SeqStatus::WAITING || currentFrame == 0)
		return false;
	return currentFrame % c.framesInBar == 0;
}

/* -------------------------------------------------------------------------- */

bool Clock::isOnBeat() const
{
	const model::Sequencer& c = g_model.get().sequencer;

	if (c.status == SeqStatus::WAITING)
		return c.state->currentFrameWait.load() % c.framesInBeat == 0;
	return c.state->currentFrame.load() % c.framesInBeat == 0;
}

/* -------------------------------------------------------------------------- */

bool Clock::isOnFirstBeat() const
{
	return g_model.get().sequencer.state->currentFrame.load() == 0;
}

/* -------------------------------------------------------------------------- */

Frame Clock::getMaxFramesInLoop(int sampleRate) const
{
	return (sampleRate * (60.0f / G_MIN_BPM)) * getBeats();
}

/* -------------------------------------------------------------------------- */

bool Clock::quantoHasPassed() const
{
	const model::Sequencer& c = g_model.get().sequencer;
	return getQuantizerValue() != 0 && c.state->currentFrame.load() % m_quantizerStep == 0;
}

/* -------------------------------------------------------------------------- */

bool Clock::canQuantize() const
{
	const model::Sequencer& c = g_model.get().sequencer;

	return c.quantize > 0 && c.status == SeqStatus::RUNNING;
}

/* -------------------------------------------------------------------------- */

float Clock::calcBpmFromRec(Frame recordedFrames, int sampleRate) const
{
	return (60.0f * getBeats()) / (recordedFrames / static_cast<float>(sampleRate));
}

/* -------------------------------------------------------------------------- */

void Clock::recomputeFrames(int sampleRate)
{
	recomputeFrames(g_model.get().sequencer, sampleRate);
	g_model.swap(model::SwapType::NONE);
}

/* -------------------------------------------------------------------------- */

void Clock::setBpm(float b, const KernelAudio& kernelAudio, int sampleRate)
{
	b = std::clamp(b, G_MIN_BPM, G_MAX_BPM);

	/* If JACK is being used, let it handle the bpm change. */
	if (!kernelAudio.jackSetBpm(b))
		setBpmRaw(b, sampleRate);
}

/* -------------------------------------------------------------------------- */

void Clock::setBeats(int newBeats, int newBars, int sampleRate)
{
	newBeats = std::clamp(newBeats, 1, G_MAX_BEATS);
	newBars  = std::clamp(newBars, 1, newBeats); // Bars cannot be greater than beats

	g_model.get().sequencer.beats = newBeats;
	g_model.get().sequencer.bars  = newBars;
	recomputeFrames(g_model.get().sequencer, sampleRate);

	g_model.swap(model::SwapType::HARD);
}

/* -------------------------------------------------------------------------- */

void Clock::setQuantize(int q, int sampleRate)
{
	g_model.get().sequencer.quantize = q;
	recomputeFrames(g_model.get().sequencer, sampleRate);

	g_model.swap(model::SwapType::HARD);
}

/* -------------------------------------------------------------------------- */

void Clock::setStatus(SeqStatus s)
{
	g_model.get().sequencer.status = s;
	g_model.swap(model::SwapType::SOFT);

	if (s == SeqStatus::RUNNING)
		m_synchronizer.sendMIDIstart();
	else if (s == SeqStatus::STOPPED)
		m_synchronizer.sendMIDIstop();
}

/* -------------------------------------------------------------------------- */

void Clock::reset(int sampleRate)
{
	g_model.get().sequencer.bars     = G_DEFAULT_BARS;
	g_model.get().sequencer.beats    = G_DEFAULT_BEATS;
	g_model.get().sequencer.bpm      = G_DEFAULT_BPM;
	g_model.get().sequencer.quantize = G_DEFAULT_QUANTIZE;
	recomputeFrames(g_model.get().sequencer, sampleRate);

	g_model.swap(model::SwapType::NONE);
}

/* -------------------------------------------------------------------------- */

void Clock::advance(Frame amount)
{
	const model::Sequencer& c = g_model.get().sequencer;

	if (c.status == SeqStatus::WAITING)
	{
		int f = (c.state->currentFrameWait.load() + amount) % c.framesInLoop;
		c.state->currentFrameWait.store(f);
		return;
	}

	int f = (c.state->currentFrame.load() + amount) % c.framesInLoop;
	int b = f / c.framesInBeat;

	c.state->currentFrame.store(f);
	c.state->currentBeat.store(b);
}

/* -------------------------------------------------------------------------- */

void Clock::rewind()
{
	const model::Sequencer& c = g_model.get().sequencer;

	c.state->currentFrame.store(0);
	c.state->currentBeat.store(0);
	c.state->currentFrameWait.store(0);

	m_synchronizer.sendMIDIrewind();
}

/* -------------------------------------------------------------------------- */

Frame Clock::quantize(Frame f)
{
	if (!canQuantize())
		return f;
	return u::math::quantize(f, m_quantizerStep) % getFramesInLoop(); // No overflow
}

/* -------------------------------------------------------------------------- */

void Clock::recomputeFrames(model::Sequencer& c, int sampleRate)
{
	c.framesInLoop = static_cast<int>((sampleRate * (60.0f / c.bpm)) * c.beats);
	c.framesInBar  = static_cast<int>(c.framesInLoop / (float)c.bars);
	c.framesInBeat = static_cast<int>(c.framesInLoop / (float)c.beats);
	c.framesInSeq  = c.framesInBeat * G_MAX_BEATS;

	if (c.quantize != 0)
		m_quantizerStep = c.framesInBeat / c.quantize;
}

/* -------------------------------------------------------------------------- */

void Clock::setBpmRaw(float v, int sampleRate)
{
	float ratio = g_model.get().sequencer.bpm / v;

	g_model.get().sequencer.bpm = v;
	recomputeFrames(g_model.get().sequencer, sampleRate);

	g_actionRecorder.updateBpm(ratio, m_quantizerStep);

	g_model.swap(model::SwapType::HARD);

	u::log::print("[clock::setBpmRaw] Bpm changed to %f\n", v);
}
} // namespace giada::m

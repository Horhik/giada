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

#include "clock.h"
#include "core/conf.h"
#include "core/const.h"
#include "core/kernelAudio.h"
#include "core/mixerHandler.h"
#include "core/model/model.h"
#include "core/recorderHandler.h"
#include "core/sequencer.h"
#include "core/sync.h"
#include "glue/events.h"
#include "utils/log.h"
#include "utils/math.h"
#include <atomic>
#include <cassert>

extern giada::m::Sequencer g_sequencer;

namespace giada::m
{
Clock::Clock(KernelAudio& k)
: m_kernelAudio(k)
, m_quantizerStep(1)
{
	reset();

#ifdef WITH_AUDIO_JACK

	if (m_kernelAudio.getAPI() == G_SYS_API_JACK)
	{
		sync::onJackRewind    = []() { g_sequencer.rawRewind(); };
		sync::onJackChangeBpm = [this](float bpm) { setBpmInternal(bpm); };
		sync::onJackStart     = []() { g_sequencer.rawStart(); };
		sync::onJackStop      = []() { g_sequencer.rawStop(); };
	}

#endif
}

/* -------------------------------------------------------------------------- */

float       Clock::getBpm() const { return model::get().clock.bpm; }
int         Clock::getBeats() const { return model::get().clock.beats; }
int         Clock::getBars() const { return model::get().clock.bars; }
int         Clock::getCurrentBeat() const { return model::get().clock.state->currentBeat.load(); }
int         Clock::getCurrentFrame() const { return model::get().clock.state->currentFrame.load(); }
float       Clock::getCurrentSecond() const { return getCurrentFrame() / static_cast<float>(conf::conf.samplerate); }
int         Clock::getFramesInBar() const { return model::get().clock.framesInBar; }
int         Clock::getFramesInBeat() const { return model::get().clock.framesInBeat; }
int         Clock::getFramesInLoop() const { return model::get().clock.framesInLoop; }
int         Clock::getFramesInSeq() const { return model::get().clock.framesInSeq; }
int         Clock::getQuantizerValue() const { return model::get().clock.quantize; }
int         Clock::getQuantizerStep() const { return m_quantizerStep; }
ClockStatus Clock::getStatus() const { return model::get().clock.status; }

/* -------------------------------------------------------------------------- */

bool Clock::isRunning() const
{
	return model::get().clock.status == ClockStatus::RUNNING;
}

/* -------------------------------------------------------------------------- */

bool Clock::isActive() const
{
	const model::Clock& c = model::get().clock;
	return c.status == ClockStatus::RUNNING || c.status == ClockStatus::WAITING;
}

/* -------------------------------------------------------------------------- */

bool Clock::isOnBar() const
{
	const model::Clock& c = model::get().clock;

	int currentFrame = c.state->currentFrame.load();

	if (c.status == ClockStatus::WAITING || currentFrame == 0)
		return false;
	return currentFrame % c.framesInBar == 0;
}

/* -------------------------------------------------------------------------- */

bool Clock::isOnBeat() const
{
	const model::Clock& c = model::get().clock;

	if (c.status == ClockStatus::WAITING)
		return c.state->currentFrameWait.load() % c.framesInBeat == 0;
	return c.state->currentFrame.load() % c.framesInBeat == 0;
}

/* -------------------------------------------------------------------------- */

bool Clock::isOnFirstBeat() const
{
	return model::get().clock.state->currentFrame.load() == 0;
}

/* -------------------------------------------------------------------------- */

Frame Clock::getMaxFramesInLoop() const
{
	return (conf::conf.samplerate * (60.0f / G_MIN_BPM)) * getBeats();
}

/* -------------------------------------------------------------------------- */

bool Clock::quantoHasPassed() const
{
	const model::Clock& c = model::get().clock;
	return getQuantizerValue() != 0 && c.state->currentFrame.load() % m_quantizerStep == 0;
}

/* -------------------------------------------------------------------------- */

bool Clock::canQuantize() const
{
	const model::Clock& c = model::get().clock;

	return c.quantize > 0 && c.status == ClockStatus::RUNNING;
}

/* -------------------------------------------------------------------------- */

float Clock::calcBpmFromRec(Frame recordedFrames) const
{
	return (60.0f * getBeats()) / (recordedFrames / static_cast<float>(conf::conf.samplerate));
}

/* -------------------------------------------------------------------------- */

void Clock::recomputeFrames()
{
	recomputeFrames(model::get().clock);
	model::swap(model::SwapType::NONE);
}

/* -------------------------------------------------------------------------- */

void Clock::setBpm(float b)
{
	b = std::clamp(b, G_MIN_BPM, G_MAX_BPM);

	/* If JACK is being used, let it handle the bpm change. */

#ifdef WITH_AUDIO_JACK
	if (m_kernelAudio.getAPI() == G_SYS_API_JACK)
	{
		m_kernelAudio.jackSetBpm(b);
		return;
	}
#endif

	setBpmInternal(b);
}

/* -------------------------------------------------------------------------- */

void Clock::setBeats(int newBeats, int newBars)
{
	newBeats = std::clamp(newBeats, 1, G_MAX_BEATS);
	newBars  = std::clamp(newBars, 1, newBeats); // Bars cannot be greater than beats

	model::get().clock.beats = newBeats;
	model::get().clock.bars  = newBars;
	recomputeFrames(model::get().clock);

	model::swap(model::SwapType::HARD);
}

/* -------------------------------------------------------------------------- */

void Clock::setQuantize(int q)
{
	model::get().clock.quantize = q;
	recomputeFrames(model::get().clock);

	model::swap(model::SwapType::HARD);
}

/* -------------------------------------------------------------------------- */

void Clock::setStatus(ClockStatus s)
{
	model::get().clock.status = s;
	model::swap(model::SwapType::SOFT);

	if (s == ClockStatus::RUNNING)
		sync::sendMIDIstart();
	else if (s == ClockStatus::STOPPED)
		sync::sendMIDIstop();
}

/* -------------------------------------------------------------------------- */

void Clock::reset()
{
	model::get().clock.bars     = G_DEFAULT_BARS;
	model::get().clock.beats    = G_DEFAULT_BEATS;
	model::get().clock.bpm      = G_DEFAULT_BPM;
	model::get().clock.quantize = G_DEFAULT_QUANTIZE;
	recomputeFrames(model::get().clock);

	model::swap(model::SwapType::NONE);
}

/* -------------------------------------------------------------------------- */

void Clock::advance(Frame amount)
{
	const model::Clock& c = model::get().clock;

	if (c.status == ClockStatus::WAITING)
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
	const model::Clock& c = model::get().clock;

	c.state->currentFrame.store(0);
	c.state->currentBeat.store(0);
	c.state->currentFrameWait.store(0);

	sync::sendMIDIrewind();
}

/* -------------------------------------------------------------------------- */

Frame Clock::quantize(Frame f)
{
	if (!canQuantize())
		return f;
	return u::math::quantize(f, m_quantizerStep) % getFramesInLoop(); // No overflow
}

/* -------------------------------------------------------------------------- */

void Clock::recomputeFrames(model::Clock& c)
{
	c.framesInLoop = static_cast<int>((conf::conf.samplerate * (60.0f / c.bpm)) * c.beats);
	c.framesInBar  = static_cast<int>(c.framesInLoop / (float)c.bars);
	c.framesInBeat = static_cast<int>(c.framesInLoop / (float)c.beats);
	c.framesInSeq  = c.framesInBeat * G_MAX_BEATS;

	if (c.quantize != 0)
		m_quantizerStep = c.framesInBeat / c.quantize;
}

/* -------------------------------------------------------------------------- */

void Clock::setBpmInternal(float current)
{
	float ratio = model::get().clock.bpm / current;

	model::get().clock.bpm = current;
	recomputeFrames(model::get().clock);

	m::recorderHandler::updateBpm(ratio, m_quantizerStep);

	model::swap(model::SwapType::HARD);

	u::log::print("[clock::setBpmInternal] Bpm changed to %f\n", current);
}
} // namespace giada::m

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

#include "core/mixer.h"
#include "core/const.h"
#include "core/model/model.h"
#include "core/sequencer.h"
#include "utils/log.h"
#include "utils/math.h"

extern giada::m::Sequencer       g_sequencer;
extern giada::m::EventDispatcher g_eventDispatcher;

namespace giada::m
{
namespace
{
/* CH_LEFT, CH_RIGHT
Channels identifiers. */

constexpr int CH_LEFT  = 0;
constexpr int CH_RIGHT = 1;
} // namespace

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

Mixer::Mixer(Frame maxFramesInLoop, Frame framesInBuffer)
: m_inputTracker(0)
, m_signalCb(nullptr)
, m_endOfRecCb(nullptr)
, m_signalCbFired(false)
{
	reset(maxFramesInLoop, framesInBuffer);
}

/* -------------------------------------------------------------------------- */

void Mixer::reset(Frame maxFramesInLoop, Frame framesInBuffer)
{
	/* Allocate working buffers. m_recBuffer has variable size: it depends on how
	many frames there are in the current loop. */

	m_recBuffer.alloc(maxFramesInLoop, G_MAX_IO_CHANS);
	m_inBuffer.alloc(framesInBuffer, G_MAX_IO_CHANS);

	u::log::print("[mixer::reset] buffers ready - maxFramesInLoop=%d, framesInBuffer=%d\n",
	    maxFramesInLoop, framesInBuffer);
}

/* -------------------------------------------------------------------------- */

void Mixer::enable()
{
	model::get().mixer.state->active.store(true);
	u::log::print("[mixer::enable] enabled\n");
}

void Mixer::disable()
{
	model::get().mixer.state->active.store(false);
	while (model::isLocked())
		;
	u::log::print("[mixer::disable] disabled\n");
}

/* -------------------------------------------------------------------------- */

void Mixer::allocRecBuffer(Frame frames)
{
	m_recBuffer.alloc(frames, G_MAX_IO_CHANS);
}

void Mixer::clearRecBuffer()
{
	m_recBuffer.clear();
}

const mcl::AudioBuffer& Mixer::getRecBuffer()
{
	return m_recBuffer;
}

/* -------------------------------------------------------------------------- */

int Mixer::render(mcl::AudioBuffer& out, const mcl::AudioBuffer& in, const RenderInfo& info)
{
	const model::Lock   rtLock = model::get_RT();
	const model::Mixer& mixer  = rtLock.get().mixer;

	m_inBuffer.clear();

	/* Reset peak computation. */

	mixer.state->peakOutL.store(0.0);
	mixer.state->peakOutR.store(0.0);
	mixer.state->peakInL.store(0.0);
	mixer.state->peakInR.store(0.0);

	/* Process line IN if input has been enabled in KernelAudio. */

	if (info.hasInput)
	{
		processLineIn(mixer, in, info.inVol, info.recTriggerLevel);
		renderMasterIn(rtLock.get(), m_inBuffer);
	}

	/* Record input audio and advance the sequencer only if clock is active:
	can't record stuff with the sequencer off. */

	if (info.isClockActive)
	{
		if (info.canLineInRec)
			lineInRec(in, info.maxFramesToRec, info.inVol);
		if (info.isClockRunning)
			processSequencer(rtLock.get(), out, m_inBuffer);
	}

	/* Channel processing. Don't do it if layout is locked: another thread is 
	changing data (e.g. Plugins or Waves). */

	if (!rtLock.get().locked)
		processChannels(rtLock.get(), out, m_inBuffer);

	/* Render remaining internal channels. */

	renderMasterOut(rtLock.get(), out);
	renderPreview(rtLock.get(), out);

	/* Post processing. */

	finalizeOutput(mixer, out, info);

	return 0;
}

/* -------------------------------------------------------------------------- */

void Mixer::startInputRec(Frame from)
{
	m_inputTracker  = from;
	m_signalCbFired = false;
}

Frame Mixer::stopInputRec()
{
	Frame ret       = m_inputTracker;
	m_inputTracker  = 0;
	m_signalCbFired = false;
	return ret;
}

/* -------------------------------------------------------------------------- */

void Mixer::setSignalCallback(std::function<void()> f) { m_signalCb = f; }
void Mixer::setEndOfRecCallback(std::function<void()> f) { m_endOfRecCb = f; }

/* -------------------------------------------------------------------------- */

bool Mixer::isChannelAudible(const channel::Data& c) const
{
	if (c.isInternal())
		return true;
	if (c.mute)
		return false;
	bool hasSolos = model::get().mixer.hasSolos;
	return !hasSolos || (hasSolos && c.solo);
}

/* -------------------------------------------------------------------------- */

Peak Mixer::getPeakOut() const
{
	return {
	    m::model::get().mixer.state->peakOutL.load(),
	    m::model::get().mixer.state->peakOutR.load()};
}

Peak Mixer::getPeakIn() const
{
	return {
	    m::model::get().mixer.state->peakInL.load(),
	    m::model::get().mixer.state->peakInR.load()};
}

/* -------------------------------------------------------------------------- */

Mixer::RecordInfo Mixer::getRecordInfo() const
{
	return {m_inputTracker, m_recBuffer.countFrames()};
}

/* -------------------------------------------------------------------------- */

void Mixer::execSignalCb()
{
	m_signalCb();
	m_signalCb = nullptr;
}

/* -------------------------------------------------------------------------- */

void Mixer::execEndOfRecCb()
{
	m_endOfRecCb();
	m_endOfRecCb = nullptr;
}

void Mixer::fireSignalCb()
{
	g_eventDispatcher.pumpUIevent({EventDispatcher::EventType::MIXER_SIGNAL_CALLBACK});
}

/* -------------------------------------------------------------------------- */

void Mixer::fireEndOfRecCb()
{
	g_eventDispatcher.pumpUIevent({EventDispatcher::EventType::MIXER_END_OF_REC_CALLBACK});
}

/* -------------------------------------------------------------------------- */

bool Mixer::thresholdReached(Peak p, float threshold) const
{
	return u::math::linearToDB(p.left) > threshold ||
	       u::math::linearToDB(p.right) > threshold;
}

/* -------------------------------------------------------------------------- */

void Mixer::lineInRec(const mcl::AudioBuffer& inBuf, Frame maxFrames, float inVol)
{
	assert(maxFrames <= m_recBuffer.countFrames());

	if (m_inputTracker >= maxFrames && m_endOfRecCb != nullptr)
	{
		fireEndOfRecCb();
		return;
	}

	const Frame framesToCopy = -1; // copy everything
	const Frame srcOffset    = 0;
	const Frame destOffset   = m_inputTracker % maxFrames; // loop over at maxFrames

	m_recBuffer.sum(inBuf, framesToCopy, srcOffset, destOffset, inVol);

	m_inputTracker += inBuf.countFrames();
}

/* -------------------------------------------------------------------------- */

void Mixer::processLineIn(const model::Mixer& mixer, const mcl::AudioBuffer& inBuf,
    float inVol, float recTriggerLevel)
{
	const Peak peak{inBuf.getPeak(CH_LEFT), inBuf.getPeak(CH_RIGHT)};

	if (m_signalCb != nullptr && thresholdReached(peak, recTriggerLevel) && !m_signalCbFired)
	{
		G_DEBUG("Signal > threshold!");
		fireSignalCb();
		m_signalCbFired = true;
	}

	mixer.state->peakInL.store(peak.left);
	mixer.state->peakInR.store(peak.right);

	/* Prepare the working buffer for input stream, which will be processed 
	later on by the Master Input Channel with plug-ins. */

	assert(inBuf.countChannels() <= m_inBuffer.countChannels());

	m_inBuffer.set(inBuf, inVol);
}

/* -------------------------------------------------------------------------- */

void Mixer::processChannels(const model::Layout& layout, mcl::AudioBuffer& out, mcl::AudioBuffer& in)
{
	for (const channel::Data& c : layout.channels)
		if (!c.isInternal())
			channel::render(c, &out, &in, isChannelAudible(c));
}

/* -------------------------------------------------------------------------- */

void Mixer::processSequencer(const model::Layout& layout, mcl::AudioBuffer& out, const mcl::AudioBuffer& in)
{
	/* Advance sequencer first, then render it (rendering is just about
	generating metronome audio). This way the metronome is aligned with 
	everything else. */

	const Sequencer::EventBuffer& events = g_sequencer.advance(in.countFrames());
	g_sequencer.render(out);

	/* No channel processing if layout is locked: another thread is changing
    data (e.g. Plugins or Waves). */

	if (layout.locked)
		return;

	for (const channel::Data& c : layout.channels)
		if (!c.isInternal())
			channel::advance(c, events);
}

/* -------------------------------------------------------------------------- */

void Mixer::renderMasterIn(const model::Layout& layout, mcl::AudioBuffer& in)
{
	channel::render(layout.getChannel(MASTER_IN_CHANNEL_ID), nullptr, &in, true);
}

void Mixer::renderMasterOut(const model::Layout& layout, mcl::AudioBuffer& out)
{
	channel::render(layout.getChannel(MASTER_OUT_CHANNEL_ID), &out, nullptr, true);
}

void Mixer::renderPreview(const model::Layout& layout, mcl::AudioBuffer& out)
{
	channel::render(layout.getChannel(PREVIEW_CHANNEL_ID), &out, nullptr, true);
}

/* -------------------------------------------------------------------------- */

void Mixer::limit(mcl::AudioBuffer& outBuf)
{
	for (int i = 0; i < outBuf.countFrames(); i++)
		for (int j = 0; j < outBuf.countChannels(); j++)
			outBuf[i][j] = std::max(-1.0f, std::min(outBuf[i][j], 1.0f));
}

/* -------------------------------------------------------------------------- */

void Mixer::finalizeOutput(const model::Mixer& mixer, mcl::AudioBuffer& outBuf,
    const RenderInfo& info)
{
	if (info.inToOut)
		outBuf.sum(m_inBuffer, info.outVol);
	else
		outBuf.applyGain(info.outVol);

	if (info.limitOutput)
		limit(outBuf);

	mixer.state->peakOutL.store(outBuf.getPeak(CH_LEFT));
	mixer.state->peakOutR.store(outBuf.getPeak(CH_RIGHT));
}
} // namespace giada::m

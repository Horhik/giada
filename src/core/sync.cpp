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

#include "sync.h"
#include "core/clock.h"
#include "core/conf.h"
#include "core/kernelAudio.h"
#include "core/kernelMidi.h"
#include "core/model/model.h"

extern giada::m::Sequencer  g_sequencer;
extern giada::m::Clock      g_clock;
extern giada::m::KernelMidi g_kernelMidi;

namespace giada::m
{
Synchronizer::Synchronizer(int sampleRate, float midiTCfps)
: m_onJackRewind(nullptr)
, m_onJackChangeBpm(nullptr)
, m_onJackStart(nullptr)
, m_onJackStop(nullptr)
{
	reset(sampleRate, midiTCfps);

#ifdef WITH_AUDIO_JACK
	m_onJackRewind    = []() { g_sequencer.rawRewind(); };
	m_onJackChangeBpm = [this](float bpm) { g_clock.setBpmRaw(bpm); };
	m_onJackStart     = []() { g_sequencer.rawStart(); };
	m_onJackStop      = []() { g_sequencer.rawStop(); };
#endif
}

/* -------------------------------------------------------------------------- */

void Synchronizer::reset(int sampleRate, float midiTCfps)
{
	m_midiTCrate = static_cast<int>((sampleRate / midiTCfps) * G_MAX_IO_CHANS); // stereo values
}

/* -------------------------------------------------------------------------- */

void Synchronizer::sendMIDIsync()
{
	const model::Clock& c = model::get().clock;

	/* Sending MIDI sync while waiting is meaningless. */

	if (c.status == ClockStatus::WAITING)
		return;

	int currentFrame = c.state->currentFrame.load();

	/* TODO - only Master (_M) is implemented so far. */

	if (conf::conf.midiSync == MIDI_SYNC_CLOCK_M)
	{
		if (currentFrame % (c.framesInBeat / 24) == 0)
			g_kernelMidi.send(MIDI_CLOCK, -1, -1);
		return;
	}

	if (conf::conf.midiSync == MIDI_SYNC_MTC_M)
	{

		/* check if a new timecode frame has passed. If so, send MIDI TC
		 * quarter frames. 8 quarter frames, divided in two branches:
		 * 1-4 and 5-8. We check timecode frame's parity: if even, send
		 * range 1-4, if odd send 5-8. */

		if (currentFrame % m_midiTCrate != 0) // no timecode frame passed
			return;

		/* frame low nibble
		 * frame high nibble
		 * seconds low nibble
		 * seconds high nibble */

		if (m_midiTCframes % 2 == 0)
		{
			g_kernelMidi.send(MIDI_MTC_QUARTER, (m_midiTCframes & 0x0F) | 0x00, -1);
			g_kernelMidi.send(MIDI_MTC_QUARTER, (m_midiTCframes >> 4) | 0x10, -1);
			g_kernelMidi.send(MIDI_MTC_QUARTER, (m_midiTCseconds & 0x0F) | 0x20, -1);
			g_kernelMidi.send(MIDI_MTC_QUARTER, (m_midiTCseconds >> 4) | 0x30, -1);
		}

		/* minutes low nibble
		 * minutes high nibble
		 * hours low nibble
		 * hours high nibble SMPTE frame rate */

		else
		{
			g_kernelMidi.send(MIDI_MTC_QUARTER, (m_midiTCminutes & 0x0F) | 0x40, -1);
			g_kernelMidi.send(MIDI_MTC_QUARTER, (m_midiTCminutes >> 4) | 0x50, -1);
			g_kernelMidi.send(MIDI_MTC_QUARTER, (m_midiTChours & 0x0F) | 0x60, -1);
			g_kernelMidi.send(MIDI_MTC_QUARTER, (m_midiTChours >> 4) | 0x70, -1);
		}

		m_midiTCframes++;

		/* check if total timecode frames are greater than timecode fps:
		 * if so, a second has passed */

		if (m_midiTCframes > conf::conf.midiTCfps)
		{
			m_midiTCframes = 0;
			m_midiTCseconds++;
			if (m_midiTCseconds >= 60)
			{
				m_midiTCminutes++;
				m_midiTCseconds = 0;
				if (m_midiTCminutes >= 60)
				{
					m_midiTChours++;
					m_midiTCminutes = 0;
				}
			}
			//u::log::print("%d:%d:%d:%d\n", m_midiTChours, m_midiTCminutes, m_midiTCseconds, m_midiTCframes);
		}
	}
}

/* -------------------------------------------------------------------------- */

void Synchronizer::sendMIDIrewind()
{
	m_midiTCframes  = 0;
	m_midiTCseconds = 0;
	m_midiTCminutes = 0;
	m_midiTChours   = 0;

	/* For cueing the slave to a particular start point, Quarter Frame messages 
    are not used. Instead, an MTC Full Frame message should be sent. The Full 
    Frame is a SysEx message that encodes the entire SMPTE time in one message. */

	if (conf::conf.midiSync == MIDI_SYNC_MTC_M)
	{
		g_kernelMidi.send(MIDI_SYSEX, 0x7F, 0x00); // send msg on channel 0
		g_kernelMidi.send(0x01, 0x01, 0x00);       // hours 0
		g_kernelMidi.send(0x00, 0x00, 0x00);       // mins, secs, frames 0
		g_kernelMidi.send(MIDI_EOX, -1, -1);       // end of sysex
	}
	else if (conf::conf.midiSync == MIDI_SYNC_CLOCK_M)
		g_kernelMidi.send(MIDI_POSITION_PTR, 0, 0);
}

/* -------------------------------------------------------------------------- */

void Synchronizer::sendMIDIstart()
{
	if (conf::conf.midiSync == MIDI_SYNC_CLOCK_M)
	{
		g_kernelMidi.send(MIDI_START, -1, -1);
		g_kernelMidi.send(MIDI_POSITION_PTR, 0, 0);
	}
}

/* -------------------------------------------------------------------------- */

void Synchronizer::sendMIDIstop()
{
	if (conf::conf.midiSync == MIDI_SYNC_CLOCK_M)
		g_kernelMidi.send(MIDI_STOP, -1, -1);
}

/* -------------------------------------------------------------------------- */

#ifdef WITH_AUDIO_JACK

void Synchronizer::recvJackSync(const JackTransport::State& state)
{
	assert(m_onJackRewind != nullptr);
	assert(m_onJackChangeBpm != nullptr);
	assert(m_onJackStart != nullptr);
	assert(m_onJackStop != nullptr);

	JackTransport::State jackStateCurr = state;

	if (jackStateCurr != m_jackStatePrev)
	{
		if (jackStateCurr.frame != m_jackStatePrev.frame && jackStateCurr.frame == 0)
		{
			G_DEBUG("JackState received - rewind to frame 0");
			m_onJackRewind();
		}

		// jackStateCurr.bpm == 0 if JACK doesn't send that info
		if (jackStateCurr.bpm != m_jackStatePrev.bpm && jackStateCurr.bpm > 1.0f)
		{
			G_DEBUG("JackState received - bpm=" << jackStateCurr.bpm);
			m_onJackChangeBpm(jackStateCurr.bpm);
		}

		if (jackStateCurr.running != m_jackStatePrev.running)
		{
			G_DEBUG("JackState received - running=" << jackStateCurr.running);
			jackStateCurr.running ? m_onJackStart() : m_onJackStop();
		}
	}

	m_jackStatePrev = jackStateCurr;
}

#endif
} // namespace giada::m

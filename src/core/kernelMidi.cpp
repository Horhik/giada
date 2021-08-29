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

#include "kernelMidi.h"
#include "const.h"
#include "midiDispatcher.h"
#include "midiMapConf.h"
#include "utils/log.h"

extern giada::m::MidiDispatcher g_midiDispatcher;

namespace giada::m
{
namespace
{
int getB1_(uint32_t iValue) { return (iValue >> 24) & 0xFF; }
int getB2_(uint32_t iValue) { return (iValue >> 16) & 0xFF; }
int getB3_(uint32_t iValue) { return (iValue >> 8) & 0xFF; }

/* -------------------------------------------------------------------------- */

static void callback_(double /*t*/, std::vector<unsigned char>* msg, void* /*data*/)
{
	if (msg->size() < 3)
	{
		//u::log::print("[KM] MIDI received - unknown signal - size=%d, value=0x", (int) msg->size());
		//for (unsigned i=0; i<msg->size(); i++)
		//	u::log::print("%X", (int) msg->at(i));
		//u::log::print("\n");
		return;
	}
	g_midiDispatcher.dispatch(msg->at(0), msg->at(1), msg->at(2));
}
} // namespace

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

KernelMidi::KernelMidi()
: m_status(false)
, m_api(0)
, m_numOutPorts(0)
, m_numInPorts(0)
{
}

/* -------------------------------------------------------------------------- */

void KernelMidi::setApi(int api)
{
	m_api = api;
	u::log::print("[KM] using system 0x%x\n", m_api);
}

/* -------------------------------------------------------------------------- */

int KernelMidi::openOutDevice(int port)
{
	try
	{
		m_midiOut = std::make_unique<RtMidiOut>((RtMidi::Api)m_api, "Giada MIDI Output");
		m_status  = true;
	}
	catch (RtMidiError& error)
	{
		u::log::print("[KM] MIDI out device error: %s\n", error.getMessage());
		m_status = false;
		return 0;
	}

	/* print output ports */

	m_numOutPorts = m_midiOut->getPortCount();
	u::log::print("[KM] %d output MIDI ports found\n", m_numOutPorts);
	for (unsigned i = 0; i < m_numOutPorts; i++)
		u::log::print("  %d) %s\n", i, getOutPortName(i));

	/* try to open a port, if enabled */

	if (port != -1 && m_numOutPorts > 0)
	{
		try
		{
			m_midiOut->openPort(port, getOutPortName(port));
			u::log::print("[KM] MIDI out port %d open\n", port);

			/* TODO - it shold send midiLightning message only if there is a map loaded
			and available in midimap:: */

			sendMidiLightningInitMsgs();
			return 1;
		}
		catch (RtMidiError& error)
		{
			u::log::print("[KM] unable to open MIDI out port %d: %s\n", port, error.getMessage());
			m_status = false;
			return 0;
		}
	}
	else
		return 2;
}

/* -------------------------------------------------------------------------- */

int KernelMidi::openInDevice(int port)
{
	try
	{
		m_midiIn = std::make_unique<RtMidiIn>((RtMidi::Api)m_api, "Giada MIDI input");
		m_status = true;
	}
	catch (RtMidiError& error)
	{
		u::log::print("[KM] MIDI in device error: %s\n", error.getMessage());
		m_status = false;
		return 0;
	}

	/* print input ports */

	m_numInPorts = m_midiIn->getPortCount();
	u::log::print("[KM] %d input MIDI ports found\n", m_numInPorts);
	for (unsigned i = 0; i < m_numInPorts; i++)
		u::log::print("  %d) %s\n", i, getInPortName(i));

	/* try to open a port, if enabled */

	if (port != -1 && m_numInPorts > 0)
	{
		try
		{
			m_midiIn->openPort(port, getInPortName(port));
			m_midiIn->ignoreTypes(true, false, true); // ignore all system/time msgs, for now
			u::log::print("[KM] MIDI in port %d open\n", port);
			m_midiIn->setCallback(&callback_);
			return 1;
		}
		catch (RtMidiError& error)
		{
			u::log::print("[KM] unable to open MIDI in port %d: %s\n", port, error.getMessage());
			m_status = false;
			return 0;
		}
	}
	else
		return 2;
}

/* -------------------------------------------------------------------------- */

bool KernelMidi::hasAPI(int API) const
{
	std::vector<RtMidi::Api> APIs;
	RtMidi::getCompiledApi(APIs);
	for (unsigned i = 0; i < APIs.size(); i++)
		if (APIs.at(i) == API)
			return true;
	return false;
}

/* -------------------------------------------------------------------------- */

std::string KernelMidi::getOutPortName(unsigned p) const
{
	try
	{
		return m_midiOut->getPortName(p);
	}
	catch (RtMidiError& /*error*/)
	{
		return "";
	}
}

std::string KernelMidi::getInPortName(unsigned p) const
{
	try
	{
		return m_midiIn->getPortName(p);
	}
	catch (RtMidiError& /*error*/)
	{
		return "";
	}
}

/* -------------------------------------------------------------------------- */

void KernelMidi::send(uint32_t data)
{
	if (!m_status)
		return;

	std::vector<unsigned char> msg(1, getB1_(data));
	msg.push_back(getB2_(data));
	msg.push_back(getB3_(data));

	m_midiOut->sendMessage(&msg);
	u::log::print("[KM::send] send msg=0x%X (%X %X %X)\n", data, msg[0], msg[1], msg[2]);
}

/* -------------------------------------------------------------------------- */

void KernelMidi::send(int b1, int b2, int b3)
{
	if (!m_status)
		return;

	std::vector<unsigned char> msg(1, b1);

	if (b2 != -1)
		msg.push_back(b2);
	if (b3 != -1)
		msg.push_back(b3);

	m_midiOut->sendMessage(&msg);
	u::log::print("[KM::send] send msg=(%X %X %X)\n", b1, b2, b3);
}

/* -------------------------------------------------------------------------- */

void KernelMidi::sendMidiLightning(uint32_t learnt, const midimap::Message& m)
{
	// Skip lightning message if not defined in midi map

	if (!midimap::isDefined(m))
	{
		u::log::print("[KM::sendMidiLightning] message skipped (not defined in midimap)");
		return;
	}

	u::log::print("[KM::sendMidiLightning] learnt=0x%X, chan=%d, msg=0x%X, offset=%d\n",
	    learnt, m.channel, m.value, m.offset);

	/* Isolate 'channel' from learnt message and offset it as requested by 'nn' in 
	the midimap configuration file. */

	uint32_t out = ((learnt & 0x00FF0000) >> 16) << m.offset;

	/* Merge the previously prepared channel into final message, and finally send 
	it. */

	out |= m.value | (m.channel << 24);
	send(out);
}

/* -------------------------------------------------------------------------- */

unsigned KernelMidi::countInPorts() const { return m_numInPorts; }
unsigned KernelMidi::countOutPorts() const { return m_numOutPorts; }
bool     KernelMidi::getStatus() const { return m_status; }

/* -------------------------------------------------------------------------- */

void KernelMidi::sendMidiLightningInitMsgs()
{
	for (const midimap::Message& m : midimap::midimap.initCommands)
	{
		if (m.value != 0x0 && m.channel != -1)
		{
			u::log::print("[KM] MIDI send (init) - Channel %x - Event 0x%X\n", m.channel, m.value);
			MidiEvent e(m.value);
			e.setChannel(m.channel);
			send(e.getRaw());
		}
	}
}
} // namespace giada::m

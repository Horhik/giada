/* -----------------------------------------------------------------------------
 *
 * Giada - Your Hardcore Loopmachine
 *
 * KernelAudio
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

#include "kernelAudio.h"
#include "conf.h"
#include "const.h"
#include "core/clock.h"
#include "core/mixerHandler.h"
#include "core/model/model.h"
#include "core/recManager.h"
#include "core/sync.h"
#include "deps/mcl-audio-buffer/src/audioBuffer.hpp"
#include "glue/main.h"
#include "mixer.h"
#include "utils/log.h"
#include "utils/vector.h"

extern giada::m::KernelAudio g_kernelAudio;

namespace giada::m
{
namespace
{
int callback_(void* outBuf, void* inBuf, unsigned bufferSize, double /*streamTime*/,
    RtAudioStreamStatus /*status*/, void* /*userData*/)
{
	mcl::AudioBuffer out(static_cast<float*>(outBuf), bufferSize, G_MAX_IO_CHANS);
	mcl::AudioBuffer in;
	if (g_kernelAudio.isInputEnabled())
		in = mcl::AudioBuffer(static_cast<float*>(inBuf), bufferSize, conf::conf.channelsInCount);

	/* Clean up output buffer before any rendering. Do this even if mixer is
	disabled to avoid audio leftovers during a temporary suspension (e.g. when
	loading a new patch). */

	out.clear();

	if (!g_kernelAudio.canRender())
		return 0;

#ifdef WITH_AUDIO_JACK
	if (g_kernelAudio.getAPI() == G_SYS_API_JACK)
		sync::recvJackSync(g_kernelAudio.jackTransportQuery());
#endif

	mixer::RenderInfo info;
	info.isAudioReady    = model::get().kernel.audioReady;
	info.hasInput        = g_kernelAudio.isInputEnabled();
	info.isClockActive   = clock::isActive();
	info.isClockRunning  = clock::isRunning();
	info.canLineInRec    = recManager::isRecordingInput() && g_kernelAudio.isInputEnabled();
	info.limitOutput     = conf::conf.limitOutput;
	info.inToOut         = mh::getInToOut();
	info.maxFramesToRec  = conf::conf.inputRecMode == InputRecMode::FREE ? clock::getMaxFramesInLoop() : clock::getFramesInLoop();
	info.outVol          = mh::getOutVol();
	info.inVol           = mh::getInVol();
	info.recTriggerLevel = conf::conf.recTriggerLevel;

	return mixer::render(out, in, info);
}
} // namespace

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

int KernelAudio::openDevice(const conf::Conf& conf)
{
	m_api = conf.soundSystem;
	u::log::print("[KA] using system 0x%x\n", m_api);

#if defined(__linux__) || defined(__FreeBSD__)

	if (m_api == G_SYS_API_JACK && hasAPI(RtAudio::UNIX_JACK))
		m_rtAudio = std::make_unique<RtAudio>(RtAudio::UNIX_JACK);
	else if (m_api == G_SYS_API_ALSA && hasAPI(RtAudio::LINUX_ALSA))
		m_rtAudio = std::make_unique<RtAudio>(RtAudio::LINUX_ALSA);
	else if (m_api == G_SYS_API_PULSE && hasAPI(RtAudio::LINUX_PULSE))
		m_rtAudio = std::make_unique<RtAudio>(RtAudio::LINUX_PULSE);

#elif defined(__FreeBSD__)

	if (m_api == G_SYS_API_JACK && hasAPI(RtAudio::UNIX_JACK))
		m_rtAudio = std::make_unique<RtAudio>(RtAudio::UNIX_JACK);
	else if (m_api == G_SYS_API_PULSE && hasAPI(RtAudio::LINUX_PULSE))
		m_rtAudio = std::make_unique<RtAudio>(RtAudio::LINUX_PULSE);

#elif defined(_WIN32)

	if (m_api == G_SYS_API_DS && hasAPI(RtAudio::WINDOWS_DS))
		m_rtAudio = std::make_unique<RtAudio>(RtAudio::WINDOWS_DS);
	else if (m_api == G_SYS_API_ASIO && hasAPI(RtAudio::WINDOWS_ASIO))
		m_rtAudio = std::make_unique<RtAudio>(RtAudio::WINDOWS_ASIO);
	else if (m_api == G_SYS_API_WASAPI && hasAPI(RtAudio::WINDOWS_WASAPI))
		m_rtAudio = std::make_unique<RtAudio>(RtAudio::WINDOWS_WASAPI);

#elif defined(__APPLE__)

	if (m_api == G_SYS_API_CORE && hasAPI(RtAudio::MACOSX_CORE))
		m_rtAudio = std::make_unique<RtAudio>(RtAudio::MACOSX_CORE);

#endif

	else
	{
		u::log::print("[KA] No API available, nothing to do!\n");
		return 0;
	}

	u::log::print("[KA] Opening device out=%d, in=%d, samplerate=%d\n",
	    conf.soundDeviceOut, conf.soundDeviceIn, conf.samplerate);

	m_devices = fetchDevices();
	printDevices(m_devices);

	/* Abort here if devices found are zero. */

	if (m_devices.size() == 0)
	{
		closeDevice();
		return 0;
	}

	RtAudio::StreamParameters outParams;
	RtAudio::StreamParameters inParams;

	outParams.deviceId     = conf.soundDeviceOut == G_DEFAULT_SOUNDDEV_OUT ? m_rtAudio->getDefaultOutputDevice() : conf.soundDeviceOut;
	outParams.nChannels    = conf.channelsOutCount;
	outParams.firstChannel = conf.channelsOutStart;

	/* Input device can be disabled. Unlike the output, here we are using all
	channels and let the user choose which one to record from in the configuration
	panel. */

	if (conf.soundDeviceIn != -1)
	{
		inParams.deviceId     = conf.soundDeviceIn;
		inParams.nChannels    = conf.channelsInCount;
		inParams.firstChannel = conf.channelsInStart;
		m_inputEnabled        = true;
	}
	else
		m_inputEnabled = false;

	RtAudio::StreamOptions options;
	options.streamName      = G_APP_NAME;
	options.numberOfBuffers = 4; // TODO - wtf?

	m_realBufferSize = conf.buffersize;
	m_realSampleRate = conf.samplerate;

#ifdef WITH_AUDIO_JACK

	/* If JACK, use its own sample rate, not the one coming from the conf
	object. */

	if (m_api == G_SYS_API_JACK)
	{
		assert(m_devices.size() > 0);
		assert(m_devices[0].sampleRates.size() > 0);

		m_realSampleRate = m_devices[0].sampleRates[0];
		u::log::print("[KA] JACK in use, samplerate=%d\n", m_realSampleRate);
	}

#endif

	try
	{
		m_rtAudio->openStream(
		    &outParams,                                     // output params
		    conf.soundDeviceIn != -1 ? &inParams : nullptr, // input params if inDevice is selected
		    RTAUDIO_FLOAT32,                                // audio format
		    m_realSampleRate,                               // sample rate
		    &m_realBufferSize,                              // buffer size in byte
		    &callback_,                                     // audio callback
		    nullptr,                                        // user data (unused)
		    &options);

#ifdef WITH_AUDIO_JACK
		m_jackTransport.emplace(*static_cast<jack_client_t*>(m_rtAudio->HACK__getJackClient()));
#endif

		model::get().kernel.audioReady = true;
		model::swap(model::SwapType::NONE);
		return 1;
	}
	catch (RtAudioError& e)
	{
		u::log::print("[KA] m_rtAudio init error: %s\n", e.getMessage());
		closeDevice();
		return 0;
	}
}

/* -------------------------------------------------------------------------- */

int KernelAudio::startStream()
{
	try
	{
		m_rtAudio->startStream();
		u::log::print("[KA] latency = %lu\n", m_rtAudio->getStreamLatency());
		return 1;
	}
	catch (RtAudioError& e)
	{
		u::log::print("[KA] Start stream error: %s\n", e.getMessage());
		return 0;
	}
}

/* -------------------------------------------------------------------------- */

int KernelAudio::stopStream()
{
	try
	{
		m_rtAudio->stopStream();
		return 1;
	}
	catch (RtAudioError& /*e*/)
	{
		u::log::print("[KA] Stop stream error\n");
		return 0;
	}
}

/* -------------------------------------------------------------------------- */

void KernelAudio::closeDevice()
{
	if (!m_rtAudio->isStreamOpen())
		return;
	m_rtAudio->stopStream();
	m_rtAudio->closeStream();
	m_rtAudio.reset(nullptr);
}

/* -------------------------------------------------------------------------- */

bool KernelAudio::isReady() const
{
	return model::get().kernel.audioReady;
}

/* -------------------------------------------------------------------------- */

unsigned KernelAudio::getRealBufSize() const { return m_realBufferSize; }
bool     KernelAudio::isInputEnabled() const { return m_inputEnabled; }

/* -------------------------------------------------------------------------- */

m::KernelAudio::Device KernelAudio::getDevice(const char* name) const
{
	for (Device device : m_devices)
		if (name == device.name)
			return device;
	return {0, false};
}

/* -------------------------------------------------------------------------- */

const std::vector<m::KernelAudio::Device>& KernelAudio::getDevices() const
{
	return m_devices;
}

/* -------------------------------------------------------------------------- */

bool KernelAudio::hasAPI(int API) const
{
	std::vector<RtAudio::Api> APIs;
	RtAudio::getCompiledApi(APIs);
	for (unsigned i = 0; i < APIs.size(); i++)
		if (APIs.at(i) == API)
			return true;
	return false;
}

int KernelAudio::getAPI() const { return m_api; }

/* -------------------------------------------------------------------------- */

void KernelAudio::logCompiledAPIs() const
{
	std::vector<RtAudio::Api> APIs;
	RtAudio::getCompiledApi(APIs);

	u::log::print("[KA] Compiled RtAudio APIs: %d\n", APIs.size());

	for (const RtAudio::Api& m_api : APIs)
	{
		switch (m_api)
		{
		case RtAudio::Api::LINUX_ALSA:
			u::log::print("  ALSA\n");
			break;
		case RtAudio::Api::LINUX_PULSE:
			u::log::print("  PulseAudio\n");
			break;
		case RtAudio::Api::UNIX_JACK:
			u::log::print("  JACK\n");
			break;
		case RtAudio::Api::MACOSX_CORE:
			u::log::print("  CoreAudio\n");
			break;
		case RtAudio::Api::WINDOWS_WASAPI:
			u::log::print("  WASAPI\n");
			break;
		case RtAudio::Api::WINDOWS_ASIO:
			u::log::print("  ASIO\n");
			break;
		case RtAudio::Api::WINDOWS_DS:
			u::log::print("  DirectSound\n");
			break;
		case RtAudio::Api::RTAUDIO_DUMMY:
			u::log::print("  Dummy\n");
			break;
		default:
			u::log::print("  (unknown)\n");
			break;
		}
	}
}

/* -------------------------------------------------------------------------- */

#ifdef WITH_AUDIO_JACK

JackTransport::State KernelAudio::jackTransportQuery()
{
	if (m_api == G_SYS_API_JACK)
		return m_jackTransport->getState();
	return {};
}

/* -------------------------------------------------------------------------- */

void KernelAudio::jackStart()
{
	if (m_api == G_SYS_API_JACK)
		m_jackTransport->start();
}

/* -------------------------------------------------------------------------- */

void KernelAudio::jackSetPosition(uint32_t frame)
{
	if (m_api == G_SYS_API_JACK)
		m_jackTransport->setPosition(frame);
}

/* -------------------------------------------------------------------------- */

void KernelAudio::jackSetBpm(double bpm)
{
	if (m_api == G_SYS_API_JACK)
		m_jackTransport->setBpm(bpm);
}

/* -------------------------------------------------------------------------- */

void KernelAudio::jackStop()
{
	if (m_api == G_SYS_API_JACK)
		m_jackTransport->stop();
}

#endif // WITH_AUDIO_JACK

/* -------------------------------------------------------------------------- */

m::KernelAudio::Device KernelAudio::fetchDevice(size_t deviceIndex) const
{
	try
	{
		RtAudio::DeviceInfo info = m_rtAudio->getDeviceInfo(deviceIndex);

		if (!info.probed)
		{
			u::log::print("[KA] Can't probe device %d\n", deviceIndex);
			return {deviceIndex};
		}

		return {
		    deviceIndex,
		    true,
		    info.name,
		    static_cast<int>(info.outputChannels),
		    static_cast<int>(info.inputChannels),
		    static_cast<int>(info.duplexChannels),
		    info.isDefaultOutput,
		    info.isDefaultInput,
		    u::vector::cast<int>(info.sampleRates)};
	}
	catch (RtAudioError& e)
	{
		u::log::print("[KA] Error fetching device %d: %s\n", deviceIndex, e.getMessage());
		return {0};
	}
}

/* -------------------------------------------------------------------------- */

std::vector<m::KernelAudio::Device> KernelAudio::fetchDevices() const
{
	std::vector<Device> out;
	for (unsigned i = 0; i < m_rtAudio->getDeviceCount(); i++)
		out.push_back(fetchDevice(i));
	return out;
}

/* -------------------------------------------------------------------------- */

void KernelAudio::printDevices(const std::vector<m::KernelAudio::Device>& devices) const
{
	u::log::print("[KA] %d device(s) found\n", devices.size());
	for (const m::KernelAudio::Device& d : devices)
	{
		u::log::print("  %d) %s\n", d.index, d.name);
		u::log::print("      ins=%d outs=%d duplex=%d\n", d.maxInputChannels, d.maxOutputChannels, d.maxDuplexChannels);
		u::log::print("      isDefaultOut=%d isDefaultIn=%d\n", d.isDefaultOut, d.isDefaultIn);
		u::log::print("      sampleRates:\n\t");
		for (int s : d.sampleRates)
			u::log::print("%d ", s);
		u::log::print("\n");
	}
}

/* -------------------------------------------------------------------------- */

bool KernelAudio::canRender() const
{
	return model::get().kernel.audioReady && model::get().mixer.state->active.load() == true;
}
} // namespace giada::m

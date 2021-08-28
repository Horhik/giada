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

#ifndef G_KERNELAUDIO_H
#define G_KERNELAUDIO_H

#include "deps/rtaudio/RtAudio.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>
#ifdef WITH_AUDIO_JACK
#include "core/jackTransport.h"
#endif

namespace giada::m::conf
{
struct Conf;
}

namespace giada::m
{
class KernelAudio
{
public:
	struct Device
	{
		size_t           index             = 0;
		bool             probed            = false;
		std::string      name              = "";
		int              maxOutputChannels = 0;
		int              maxInputChannels  = 0;
		int              maxDuplexChannels = 0;
		bool             isDefaultOut      = false;
		bool             isDefaultIn       = false;
		std::vector<int> sampleRates       = {};
	};

	int  openDevice(const m::conf::Conf& conf);
	void closeDevice();
	int  startStream();
	int  stopStream();
#ifdef WITH_AUDIO_JACK
	void                 jackStart();
	void                 jackStop();
	void                 jackSetPosition(uint32_t frame);
	void                 jackSetBpm(double bpm);
	JackTransport::State jackTransportQuery();
#endif

	bool                       isReady() const;
	bool                       isInputEnabled() const;
	bool                       canRender() const;
	unsigned                   getRealBufSize() const;
	bool                       hasAPI(int API) const;
	int                        getAPI() const;
	void                       logCompiledAPIs() const;
	Device                     getDevice(const char* name) const;
	const std::vector<Device>& getDevices() const;

private:
	Device              fetchDevice(size_t deviceIndex) const;
	std::vector<Device> fetchDevices() const;
	void                printDevices(const std::vector<Device>& devices) const;

#ifdef WITH_AUDIO_JACK
	std::optional<JackTransport> m_jackTransport;
#endif
	std::vector<Device>      m_devices;
	std::unique_ptr<RtAudio> m_rtAudio;
	bool                     m_inputEnabled   = false;
	unsigned                 m_realBufferSize = 0; // Real buffer size from the soundcard
	int                      m_realSampleRate = 0; // Sample rate might differ if JACK in use
	int                      m_api            = 0;
};
} // namespace giada::m

#endif

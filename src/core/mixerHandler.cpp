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

#include "core/mixerHandler.h"
#include "core/channels/channelManager.h"
#include "core/clock.h"
#include "core/conf.h"
#include "core/const.h"
#include "core/init.h"
#include "core/kernelAudio.h"
#include "core/kernelMidi.h"
#include "core/midiMap.h"
#include "core/mixer.h"
#include "core/model/model.h"
#include "core/patch.h"
#include "core/plugins/plugin.h"
#include "core/plugins/pluginHost.h"
#include "core/plugins/pluginManager.h"
#include "core/recorder.h"
#include "core/sync.h"
#include "core/wave.h"
#include "core/waveFx.h"
#include "glue/channel.h"
#include "glue/main.h"
#include "src/core/actions/actionRecorder.h"
#include "src/core/actions/actions.h"
#include "utils/fs.h"
#include "utils/log.h"
#include "utils/string.h"
#include "utils/vector.h"
#include <algorithm>
#include <cassert>
#include <vector>

extern giada::m::model::Model   g_model;
extern giada::m::KernelAudio    g_kernelAudio;
extern giada::m::Clock          g_clock;
extern giada::m::Mixer          g_mixer;
extern giada::m::MixerHandler   g_mixerHandler;
extern giada::m::PluginHost     g_pluginHost;
extern giada::m::ActionRecorder g_actionRecorder;
extern giada::m::Synchronizer   g_synchronizer;
extern giada::m::Recorder       g_recorder;
extern giada::m::conf::Data     g_conf;
extern giada::m::patch::Data    g_patch;
extern giada::m::ChannelManager g_channelManager;
extern giada::m::WaveManager    g_waveManager;

namespace giada::m
{
namespace
{
int audioCallback_(void* outBuf, void* inBuf, int bufferSize)
{
	mcl::AudioBuffer out(static_cast<float*>(outBuf), bufferSize, G_MAX_IO_CHANS);
	mcl::AudioBuffer in;
	if (g_kernelAudio.isInputEnabled())
		in = mcl::AudioBuffer(static_cast<float*>(inBuf), bufferSize, g_conf.channelsInCount);

	/* Clean up output buffer before any rendering. Do this even if mixer is
	disabled to avoid audio leftovers during a temporary suspension (e.g. when
	loading a new patch). */

	out.clear();

	if (!g_kernelAudio.canRender())
		return 0;

#ifdef WITH_AUDIO_JACK
	if (g_kernelAudio.getAPI() == G_SYS_API_JACK)
		g_synchronizer.recvJackSync(g_kernelAudio.jackTransportQuery());
#endif

	Mixer::RenderInfo info;
	info.isAudioReady    = g_kernelAudio.isReady();
	info.hasInput        = g_kernelAudio.isInputEnabled();
	info.isClockActive   = g_clock.isActive();
	info.isClockRunning  = g_clock.isRunning();
	info.canLineInRec    = g_recorder.isRecordingInput() && g_kernelAudio.isInputEnabled();
	info.limitOutput     = g_conf.limitOutput;
	info.inToOut         = g_mixerHandler.getInToOut();
	info.maxFramesToRec  = g_conf.inputRecMode == InputRecMode::FREE ? g_clock.getMaxFramesInLoop() : g_clock.getFramesInLoop();
	info.outVol          = g_mixerHandler.getOutVol();
	info.inVol           = g_mixerHandler.getInVol();
	info.recTriggerLevel = g_conf.recTriggerLevel;

	return g_mixer.render(out, in, info);
}
} // namespace

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

MixerHandler::MixerHandler(Frame framesInLoop, Frame framesInBuffer)
{
	reset(framesInLoop, framesInBuffer);
	g_kernelAudio.onAudioCallback = audioCallback_;
}

/* -------------------------------------------------------------------------- */

void MixerHandler::reset(Frame framesInLoop, Frame framesInBuffer)
{
	g_mixer.reset(framesInLoop, framesInBuffer);

	g_model.get().channels.clear();

	g_model.get().channels.push_back(g_channelManager.create(
	    Mixer::MASTER_OUT_CHANNEL_ID, ChannelType::MASTER, /*columnId=*/0));
	g_model.get().channels.push_back(g_channelManager.create(
	    Mixer::MASTER_IN_CHANNEL_ID, ChannelType::MASTER, /*columnId=*/0));
	g_model.get().channels.push_back(g_channelManager.create(
	    Mixer::PREVIEW_CHANNEL_ID, ChannelType::PREVIEW, /*columnId=*/0));

	g_model.swap(model::SwapType::NONE);
}

/* -------------------------------------------------------------------------- */

void MixerHandler::startRendering() { g_mixer.enable(); }
void MixerHandler::stopRendering() { g_mixer.disable(); }

/* -------------------------------------------------------------------------- */

void MixerHandler::addChannel(ChannelType type, ID columnId)
{
	addChannelInternal(type, columnId);
}

/* -------------------------------------------------------------------------- */

int MixerHandler::loadChannel(ID channelId, const std::string& fname)
{
	WaveManager::Result res = createWave(fname);

	if (res.status != G_RES_OK)
		return res.status;

	g_model.add(std::move(res.wave));

	Wave& wave = g_model.back<Wave>();
	Wave* old  = g_model.get().getChannel(channelId).samplePlayer->getWave();

	samplePlayer::loadWave(g_model.get().getChannel(channelId), &wave);
	g_model.swap(model::SwapType::HARD);

	/* Remove old wave, if any. It is safe to do it now: the audio thread is
	already processing the new layout. */

	if (old != nullptr)
		g_model.remove<Wave>(*old);

	g_recorder.refreshInputRecMode();

	return res.status;
}

/* -------------------------------------------------------------------------- */

int MixerHandler::addAndLoadChannel(ID columnId, const std::string& fname)
{
	WaveManager::Result res = createWave(fname);
	if (res.status == G_RES_OK)
		addAndLoadChannel(columnId, std::move(res.wave));
	return res.status;
}

void MixerHandler::addAndLoadChannel(ID columnId, std::unique_ptr<Wave>&& w)
{
	g_model.add(std::move(w));

	Wave&          wave    = g_model.back<Wave>();
	channel::Data& channel = addChannelInternal(ChannelType::SAMPLE, columnId);

	samplePlayer::loadWave(channel, &wave);
	g_model.swap(model::SwapType::HARD);

	g_recorder.refreshInputRecMode();
}

/* -------------------------------------------------------------------------- */

void MixerHandler::cloneChannel(ID channelId)
{
	channel::Data& oldChannel = g_model.get().getChannel(channelId);
	channel::Data  newChannel = g_channelManager.create(oldChannel);

	/* Clone plugins, actions and wave first in their own lists. */

#ifdef WITH_VST
	newChannel.plugins = g_pluginHost.clonePlugins(oldChannel.plugins);
#endif
	g_actionRecorder.cloneActions(channelId, newChannel.id);

	if (newChannel.samplePlayer && newChannel.samplePlayer->hasWave())
	{
		Wave* wave = newChannel.samplePlayer->getWave();
		g_model.add(g_waveManager.createFromWave(*wave, 0, wave->getBuffer().countFrames()));
	}

	/* Then push the new channel in the channels vector. */

	g_model.get().channels.push_back(newChannel);
	g_model.swap(model::SwapType::HARD);
}

/* -------------------------------------------------------------------------- */

void MixerHandler::freeChannel(ID channelId)
{
	channel::Data& ch = g_model.get().getChannel(channelId);

	assert(ch.samplePlayer);

	const Wave* wave = ch.samplePlayer->getWave();

	samplePlayer::loadWave(ch, nullptr);
	g_model.swap(model::SwapType::HARD);

	if (wave != nullptr)
		g_model.remove<Wave>(*wave);

	g_recorder.refreshInputRecMode();
}

/* -------------------------------------------------------------------------- */

void MixerHandler::freeAllChannels()
{
	for (channel::Data& ch : g_model.get().channels)
		if (ch.samplePlayer)
			samplePlayer::loadWave(ch, nullptr);

	g_model.swap(model::SwapType::HARD);
	g_model.clear<model::WavePtrs>();

	g_recorder.refreshInputRecMode();
}

/* -------------------------------------------------------------------------- */

void MixerHandler::deleteChannel(ID channelId)
{
	const channel::Data& ch   = g_model.get().getChannel(channelId);
	const Wave*          wave = ch.samplePlayer ? ch.samplePlayer->getWave() : nullptr;
#ifdef WITH_VST
	const std::vector<Plugin*> plugins = ch.plugins;
#endif

	u::vector::removeIf(g_model.get().channels, [channelId](const channel::Data& c) {
		return c.id == channelId;
	});
	g_model.swap(model::SwapType::HARD);

	if (wave != nullptr)
		g_model.remove<Wave>(*wave);

#ifdef WITH_VST
	g_pluginHost.freePlugins(plugins);
#endif

	g_recorder.refreshInputRecMode();
}

/* -------------------------------------------------------------------------- */

void MixerHandler::renameChannel(ID channelId, const std::string& name)
{
	g_model.get().getChannel(channelId).name = name;
	g_model.swap(model::SwapType::HARD);
}

/* -------------------------------------------------------------------------- */

void MixerHandler::updateSoloCount()
{
	bool hasSolos = forAnyChannel([](const channel::Data& ch) {
		return !ch.isInternal() && ch.solo;
	});

	g_model.get().mixer.hasSolos = hasSolos;
	g_model.swap(model::SwapType::NONE);
}

/* -------------------------------------------------------------------------- */

void MixerHandler::setInToOut(bool v)
{
	g_model.get().mixer.inToOut = v;
	g_model.swap(model::SwapType::NONE);
}

/* -------------------------------------------------------------------------- */

float MixerHandler::getInVol() const
{
	return g_model.get().getChannel(Mixer::MASTER_IN_CHANNEL_ID).volume;
}

float MixerHandler::getOutVol() const
{
	return g_model.get().getChannel(Mixer::MASTER_OUT_CHANNEL_ID).volume;
}

bool MixerHandler::getInToOut() const
{
	return g_model.get().mixer.inToOut;
}

/* -------------------------------------------------------------------------- */

void MixerHandler::finalizeInputRec(Frame recordedFrames)
{
	for (channel::Data* ch : getRecordableChannels())
		recordChannel(*ch, recordedFrames);
	for (channel::Data* ch : getOverdubbableChannels())
		overdubChannel(*ch);

	g_mixer.clearRecBuffer();
}

/* -------------------------------------------------------------------------- */

bool MixerHandler::hasInputRecordableChannels() const
{
	return forAnyChannel([](const channel::Data& ch) { return ch.canInputRec(); });
}

bool MixerHandler::hasActionRecordableChannels() const
{
	return forAnyChannel([](const channel::Data& ch) { return ch.canActionRec(); });
}

bool MixerHandler::hasLogicalSamples() const
{
	return forAnyChannel([](const channel::Data& ch) { return ch.samplePlayer && ch.samplePlayer->hasLogicalWave(); });
}

bool MixerHandler::hasEditedSamples() const
{
	return forAnyChannel([](const channel::Data& ch) {
		return ch.samplePlayer && ch.samplePlayer->hasEditedWave();
	});
}

bool MixerHandler::hasActions() const
{
	return forAnyChannel([](const channel::Data& ch) { return ch.hasActions; });
}

bool MixerHandler::hasAudioData() const
{
	return forAnyChannel([](const channel::Data& ch) {
		return ch.samplePlayer && ch.samplePlayer->hasWave();
	});
}

/* -------------------------------------------------------------------------- */

channel::Data& MixerHandler::addChannelInternal(ChannelType type, ID columnId)
{
	g_model.get().channels.push_back(g_channelManager.create(/*id=*/0, type, columnId));
	g_model.swap(model::SwapType::HARD);

	return g_model.get().channels.back();
}

/* -------------------------------------------------------------------------- */

WaveManager::Result MixerHandler::createWave(const std::string& fname)
{
	return g_waveManager.createFromFile(fname, /*id=*/0, g_conf.samplerate,
	    g_conf.rsmpQuality);
}

/* -------------------------------------------------------------------------- */

bool MixerHandler::forAnyChannel(std::function<bool(const channel::Data&)> f) const
{
	return std::any_of(g_model.get().channels.begin(), g_model.get().channels.end(), f);
}

/* -------------------------------------------------------------------------- */

std::vector<channel::Data*> MixerHandler::getChannelsIf(std::function<bool(const channel::Data&)> f)
{
	std::vector<channel::Data*> out;
	for (channel::Data& ch : g_model.get().channels)
		if (f(ch))
			out.push_back(&ch);
	return out;
}

std::vector<channel::Data*> MixerHandler::getRecordableChannels()
{
	return getChannelsIf([](const channel::Data& c) { return c.canInputRec() && !c.hasWave(); });
}

std::vector<channel::Data*> MixerHandler::getOverdubbableChannels()
{
	return getChannelsIf([](const channel::Data& c) { return c.canInputRec() && c.hasWave(); });
}

/* -------------------------------------------------------------------------- */

void MixerHandler::setupChannelPostRecording(channel::Data& ch)
{
	/* Start sample channels in loop mode right away. */
	if (ch.samplePlayer->isAnyLoopMode())
		samplePlayer::kickIn(ch, g_clock.getCurrentFrame());
	/* Disable 'arm' button if overdub protection is on. */
	if (ch.audioReceiver->overdubProtection == true)
		ch.armed = false;
}

/* -------------------------------------------------------------------------- */

void MixerHandler::recordChannel(channel::Data& ch, Frame recordedFrames)
{
	/* Create a new Wave with audio coming from Mixer's input buffer. */

	std::string           filename = "TAKE-" + std::to_string(g_patch.lastTakeId++) + ".wav";
	std::unique_ptr<Wave> wave     = g_waveManager.createEmpty(recordedFrames, G_MAX_IO_CHANS,
        g_conf.samplerate, filename);

	G_DEBUG("Created new Wave, size=" << wave->getBuffer().countFrames());

	/* Copy up to wave.getSize() from the mixer's input buffer into wave's. */

	wave->getBuffer().set(g_mixer.getRecBuffer(), wave->getBuffer().countFrames());

	/* Update channel with the new Wave. */

	g_model.add(std::move(wave));
	samplePlayer::loadWave(ch, &g_model.back<Wave>());
	setupChannelPostRecording(ch);

	g_model.swap(model::SwapType::HARD);
}

/* -------------------------------------------------------------------------- */

void MixerHandler::overdubChannel(channel::Data& ch)
{
	Wave* wave = ch.samplePlayer->getWave();

	/* Need model::DataLock here, as data might be being read by the audio
	thread at the same time. */

	model::DataLock lock = g_model.lockData();

	wave->getBuffer().sum(g_mixer.getRecBuffer(), /*gain=*/1.0f);
	wave->setLogical(true);

	setupChannelPostRecording(ch);
}
} // namespace giada::m

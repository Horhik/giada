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
#include "core/actionRecorder.h"
#include "core/channels/channelManager.h"
#include "core/clock.h"
#include "core/conf.h"
#include "core/const.h"
#include "core/init.h"
#include "core/kernelAudio.h"
#include "core/kernelMidi.h"
#include "core/midiMapConf.h"
#include "core/mixer.h"
#include "core/model/model.h"
#include "core/patch.h"
#include "core/plugins/plugin.h"
#include "core/plugins/pluginHost.h"
#include "core/plugins/pluginManager.h"
#include "core/recManager.h"
#include "core/recorderHandler.h"
#include "core/wave.h"
#include "core/waveFx.h"
#include "glue/channel.h"
#include "glue/main.h"
#include "utils/fs.h"
#include "utils/log.h"
#include "utils/string.h"
#include "utils/vector.h"
#include <algorithm>
#include <cassert>
#include <vector>

extern giada::m::KernelAudio           g_kernelAudio;
extern giada::m::Clock                 g_clock;
extern giada::m::Mixer                 g_mixer;
extern giada::m::PluginHost            g_pluginHost;
extern giada::m::ActionRecorderHandler g_actionRecorderHandler;

namespace giada::m
{
MixerHandler::MixerHandler(Frame framesInLoop, Frame framesInBuffer)
{
	reset(framesInLoop, framesInBuffer);
}

/* -------------------------------------------------------------------------- */

void MixerHandler::reset(Frame framesInLoop, Frame framesInBuffer)
{
	g_mixer.reset(framesInLoop, framesInBuffer);

	model::get().channels.clear();

	model::get().channels.push_back(channelManager::create(
	    Mixer::MASTER_OUT_CHANNEL_ID, ChannelType::MASTER, /*columnId=*/0));
	model::get().channels.push_back(channelManager::create(
	    Mixer::MASTER_IN_CHANNEL_ID, ChannelType::MASTER, /*columnId=*/0));
	model::get().channels.push_back(channelManager::create(
	    Mixer::PREVIEW_CHANNEL_ID, ChannelType::PREVIEW, /*columnId=*/0));

	model::swap(model::SwapType::NONE);
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
	waveManager::Result res = createWave(fname);

	if (res.status != G_RES_OK)
		return res.status;

	model::add(std::move(res.wave));

	Wave& wave = model::back<Wave>();
	Wave* old  = model::get().getChannel(channelId).samplePlayer->getWave();

	samplePlayer::loadWave(model::get().getChannel(channelId), &wave);
	model::swap(model::SwapType::HARD);

	/* Remove old wave, if any. It is safe to do it now: the audio thread is
	already processing the new layout. */

	if (old != nullptr)
		model::remove<Wave>(*old);

	recManager::refreshInputRecMode();

	return res.status;
}

/* -------------------------------------------------------------------------- */

int MixerHandler::addAndLoadChannel(ID columnId, const std::string& fname)
{
	waveManager::Result res = createWave(fname);
	if (res.status == G_RES_OK)
		addAndLoadChannel(columnId, std::move(res.wave));
	return res.status;
}

void MixerHandler::addAndLoadChannel(ID columnId, std::unique_ptr<Wave>&& w)
{
	model::add(std::move(w));

	Wave&          wave    = model::back<Wave>();
	channel::Data& channel = addChannelInternal(ChannelType::SAMPLE, columnId);

	samplePlayer::loadWave(channel, &wave);
	model::swap(model::SwapType::HARD);

	recManager::refreshInputRecMode();
}

/* -------------------------------------------------------------------------- */

void MixerHandler::cloneChannel(ID channelId)
{
	channel::Data& oldChannel = model::get().getChannel(channelId);
	channel::Data  newChannel = channelManager::create(oldChannel);

	/* Clone plugins, actions and wave first in their own lists. */

#ifdef WITH_VST
	newChannel.plugins = g_pluginHost.clonePlugins(oldChannel.plugins);
#endif
	g_actionRecorderHandler.cloneActions(channelId, newChannel.id);

	if (newChannel.samplePlayer && newChannel.samplePlayer->hasWave())
	{
		Wave* wave = newChannel.samplePlayer->getWave();
		model::add(waveManager::createFromWave(*wave, 0, wave->getBuffer().countFrames()));
	}

	/* Then push the new channel in the channels vector. */

	model::get().channels.push_back(newChannel);
	model::swap(model::SwapType::HARD);
}

/* -------------------------------------------------------------------------- */

void MixerHandler::freeChannel(ID channelId)
{
	channel::Data& ch = model::get().getChannel(channelId);

	assert(ch.samplePlayer);

	const Wave* wave = ch.samplePlayer->getWave();

	samplePlayer::loadWave(ch, nullptr);
	model::swap(model::SwapType::HARD);

	if (wave != nullptr)
		model::remove<Wave>(*wave);

	recManager::refreshInputRecMode();
}

/* -------------------------------------------------------------------------- */

void MixerHandler::freeAllChannels()
{
	for (channel::Data& ch : model::get().channels)
		if (ch.samplePlayer)
			samplePlayer::loadWave(ch, nullptr);

	model::swap(model::SwapType::HARD);
	model::clear<model::WavePtrs>();

	recManager::refreshInputRecMode();
}

/* -------------------------------------------------------------------------- */

void MixerHandler::deleteChannel(ID channelId)
{
	const channel::Data& ch   = model::get().getChannel(channelId);
	const Wave*          wave = ch.samplePlayer ? ch.samplePlayer->getWave() : nullptr;
#ifdef WITH_VST
	const std::vector<Plugin*> plugins = ch.plugins;
#endif

	u::vector::removeIf(model::get().channels, [channelId](const channel::Data& c) {
		return c.id == channelId;
	});
	model::swap(model::SwapType::HARD);

	if (wave != nullptr)
		model::remove<Wave>(*wave);

#ifdef WITH_VST
	g_pluginHost.freePlugins(plugins);
#endif

	recManager::refreshInputRecMode();
}

/* -------------------------------------------------------------------------- */

void MixerHandler::renameChannel(ID channelId, const std::string& name)
{
	model::get().getChannel(channelId).name = name;
	model::swap(model::SwapType::HARD);
}

/* -------------------------------------------------------------------------- */

void MixerHandler::updateSoloCount()
{
	bool hasSolos = forAnyChannel([](const channel::Data& ch) {
		return !ch.isInternal() && ch.solo;
	});

	model::get().mixer.hasSolos = hasSolos;
	model::swap(model::SwapType::NONE);
}

/* -------------------------------------------------------------------------- */

void MixerHandler::setInToOut(bool v)
{
	model::get().mixer.inToOut = v;
	model::swap(model::SwapType::NONE);
}

/* -------------------------------------------------------------------------- */

float MixerHandler::getInVol() const
{
	return model::get().getChannel(Mixer::MASTER_IN_CHANNEL_ID).volume;
}

float MixerHandler::getOutVol() const
{
	return model::get().getChannel(Mixer::MASTER_OUT_CHANNEL_ID).volume;
}

bool MixerHandler::getInToOut() const
{
	return model::get().mixer.inToOut;
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
	model::get().channels.push_back(channelManager::create(/*id=*/0, type, columnId));
	model::swap(model::SwapType::HARD);

	return model::get().channels.back();
}

/* -------------------------------------------------------------------------- */

waveManager::Result MixerHandler::createWave(const std::string& fname)
{
	return waveManager::createFromFile(fname, /*id=*/0, conf::conf.samplerate,
	    conf::conf.rsmpQuality);
}

/* -------------------------------------------------------------------------- */

bool MixerHandler::forAnyChannel(std::function<bool(const channel::Data&)> f) const
{
	return std::any_of(model::get().channels.begin(), model::get().channels.end(), f);
}

/* -------------------------------------------------------------------------- */

std::vector<channel::Data*> MixerHandler::getChannelsIf(std::function<bool(const channel::Data&)> f)
{
	std::vector<channel::Data*> out;
	for (channel::Data& ch : model::get().channels)
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

	std::string           filename = "TAKE-" + std::to_string(patch::patch.lastTakeId++) + ".wav";
	std::unique_ptr<Wave> wave     = waveManager::createEmpty(recordedFrames, G_MAX_IO_CHANS,
        conf::conf.samplerate, filename);

	G_DEBUG("Created new Wave, size=" << wave->getBuffer().countFrames());

	/* Copy up to wave.getSize() from the mixer's input buffer into wave's. */

	wave->getBuffer().set(g_mixer.getRecBuffer(), wave->getBuffer().countFrames());

	/* Update channel with the new Wave. */

	model::add(std::move(wave));
	samplePlayer::loadWave(ch, &model::back<Wave>());
	setupChannelPostRecording(ch);

	model::swap(model::SwapType::HARD);
}

/* -------------------------------------------------------------------------- */

void MixerHandler::overdubChannel(channel::Data& ch)
{
	Wave* wave = ch.samplePlayer->getWave();

	/* Need model::DataLock here, as data might be being read by the audio
	thread at the same time. */

	model::DataLock lock;
	wave->getBuffer().sum(g_mixer.getRecBuffer(), /*gain=*/1.0f);
	wave->setLogical(true);

	setupChannelPostRecording(ch);
}
} // namespace giada::m

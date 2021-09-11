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

#ifndef G_MIXER_HANDLER_H
#define G_MIXER_HANDLER_H

#include "core/waveManager.h"
#include "types.h"
#include <functional>
#include <memory>
#include <string>

namespace giada::m::channel
{
class Data;
}

namespace giada::m
{
class Wave;
}

namespace giada::m
{
class Plugin;
class MixerHandler final
{
public:
	MixerHandler(Frame framesInLoop, Frame framesInBuffer);

	/* hasLogicalSamples
    True if 1 or more samples are logical (memory only, such as takes). */

	bool hasLogicalSamples() const;

	/* hasEditedSamples
    True if 1 or more samples was edited via gEditor */

	bool hasEditedSamples() const;

	/* has(Input|Action)RecordableChannels
    Tells whether Mixer has one or more input or action recordable channels. */

	bool hasInputRecordableChannels() const;
	bool hasActionRecordableChannels() const;

	/* hasActions
    True if at least one Channel has actions recorded in it. */

	bool hasActions() const;

	/* hasAudioData
    True if at least one Sample Channel has some audio recorded in it. */

	bool hasAudioData() const;

	float getInVol() const;
	float getOutVol() const;
	bool  getInToOut() const;

	/* reset
	Brings everything back to the initial state. */

	void reset(Frame framesInLoop, Frame framesInBuffer);

	/* startRendering
    Fires up mixer. */

	void startRendering();

	/* stopRendering
    Stops mixer from running. */

	void stopRendering();

	/* addChannel
    Adds a new channel of type 'type' into the channels stack. Returns the new
    channel ID. */

	void addChannel(ChannelType type, ID columnId, int bufferSize);

	/* loadChannel
    Loads a new Wave inside a Sample Channel. */

	void loadChannel(ID channelId, std::unique_ptr<Wave> w);

	/* addAndLoadChannel
    Creates a new channels, fills it with a Wave and then add it to the stack. */

	void addAndLoadChannel(ID columnId, std::unique_ptr<Wave> w, int bufferSize);

	/* freeChannel
    Unloads existing Wave from a Sample Channel. */

	void freeChannel(ID channelId);

	/* deleteChannel
    Completely removes a channel from the stack. */

	void deleteChannel(ID channelId);

	void cloneChannel(ID channelId, int bufferSize);
	void renameChannel(ID channelId, const std::string& name);
	void freeAllChannels();

	void setInToOut(bool v);

	/* updateSoloCount
    Updates the number of solo-ed channels in mixer. */

	void updateSoloCount();

	/* finalizeInputRec
    Fills armed Sample Channels with audio data coming from an input recording
    session. */

	void finalizeInputRec(Frame recordedFrames, Frame currentFrame);

	/* onChannelsAltered
	Fired when something is done on channels (added, removed, loaded, ...). */

	std::function<void()> onChannelsAltered;

	/* onChannelRecorded
	Fired during the input recording finalization, when a new empty Wave must
	be added to each armed channel in order to store recorded audio coming from
	Mixer. */

	std::function<std::unique_ptr<Wave>(Frame)> onChannelRecorded;

	/* onCloneChannelWave 
	Fired when cloning a Wave that belongs to a channel. Wants a copy of the 
	original Wave in return. */

	std::function<std::unique_ptr<Wave>(const Wave&)> onCloneChannelWave;

	/* onCloneChannelPlugins 
	Fired when cloning a list of plug-ins that belong to a channel. Wants a
	vector of cloned plug-ins pointers in return. */

	std::function<std::vector<Plugin*>(const std::vector<Plugin*>&)> onCloneChannelPlugins;

private:
	bool                        forAnyChannel(std::function<bool(const channel::Data&)> f) const;
	std::vector<channel::Data*> getChannelsIf(std::function<bool(const channel::Data&)> f);

	channel::Data& addChannelInternal(ChannelType type, ID columnId, int bufferSize);

	std::vector<channel::Data*> getRecordableChannels();
	std::vector<channel::Data*> getOverdubbableChannels();

	void setupChannelPostRecording(channel::Data& ch, Frame currentFrame);

	/* recordChannel
	Records the current Mixer audio input data into an empty channel. */

	void recordChannel(channel::Data& ch, Frame recordedFrames, Frame currentFrame);

	/* overdubChannel
	Records the current Mixer audio input data into a channel with an existing
	Wave, overdub mode. */

	void overdubChannel(channel::Data& ch, Frame currentFrame);
};
} // namespace giada::m

#endif

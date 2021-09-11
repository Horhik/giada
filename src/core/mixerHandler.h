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

	int loadChannel(ID channelId, const std::string& fname);

	/* addAndLoadChannel (1)
    Creates a new channels, fills it with a Wave and then add it to the stack. */

	int addAndLoadChannel(ID columnId, const std::string& fname, int bufferSize);

	/* addAndLoadChannel (2)
    Same as (1), but Wave is already provided. */

	void addAndLoadChannel(ID columnId, std::unique_ptr<Wave>&& w, int bufferSize);

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

	void finalizeInputRec(Frame recordedFrames);

	/* onChannelsAltered
	Fired when something is done on channels (added, removed, loaded, ...). */

	std::function<void()> onChannelsAltered;

private:
	bool                        forAnyChannel(std::function<bool(const channel::Data&)> f) const;
	std::vector<channel::Data*> getChannelsIf(std::function<bool(const channel::Data&)> f);

	channel::Data&      addChannelInternal(ChannelType type, ID columnId, int bufferSize);
	WaveManager::Result createWave(const std::string& fname);

	std::vector<channel::Data*> getRecordableChannels();
	std::vector<channel::Data*> getOverdubbableChannels();

	void setupChannelPostRecording(channel::Data& ch);

	/* recordChannel
	Records the current Mixer audio input data into an empty channel. */

	void recordChannel(channel::Data& ch, Frame recordedFrames);

	/* overdubChannel
	Records the current Mixer audio input data into a channel with an existing
	Wave, overdub mode. */

	void overdubChannel(channel::Data& ch);
};
} // namespace giada::m

#endif

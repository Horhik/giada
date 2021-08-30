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

#include "core/recorder.h"
#include "core/clock.h"
#include "core/conf.h"
#include "core/kernelAudio.h"
#include "core/midiDispatcher.h"
#include "core/mixer.h"
#include "core/mixerHandler.h"
#include "core/model/model.h"
#include "core/sequencer.h"
#include "core/types.h"
#include "gui/dispatcher.h"
#include "src/core/actions/actionRecorder.h"
#include "src/core/actions/actions.h"

extern giada::m::KernelAudio    g_kernelAudio;
extern giada::m::Clock          g_clock;
extern giada::m::Sequencer      g_sequencer;
extern giada::m::Mixer          g_mixer;
extern giada::m::MixerHandler   g_mixerHandler;
extern giada::m::MidiDispatcher g_midiDispatcher;
extern giada::m::ActionRecorder g_actionRecorder;
extern giada::m::conf::Conf     g_conf;

namespace giada::m
{
bool Recorder::isRecording() const
{
	return isRecordingAction() || isRecordingInput();
}

bool Recorder::isRecordingAction() const
{
	return model::get().recorder.isRecordingAction;
}

bool Recorder::isRecordingInput() const
{
	return model::get().recorder.isRecordingInput;
}

/* -------------------------------------------------------------------------- */

void Recorder::startActionRec(RecTriggerMode mode)
{
	if (!isKernelReady())
		return;

	if (mode == RecTriggerMode::NORMAL)
	{
		startActionRec();
		setRecordingAction(true);
	}
	else
	{ // RecTriggerMode::SIGNAL
		g_clock.setStatus(ClockStatus::WAITING);
		g_clock.rewind();
		g_midiDispatcher.setSignalCallback([this]() { startActionRec(); });
		v::dispatcher::setSignalCallback([this]() { startActionRec(); });
		setRecordingAction(true);
	}
}

/* -------------------------------------------------------------------------- */

void Recorder::stopActionRec()
{
	setRecordingAction(false);

	/* If you stop the Action Recorder in SIGNAL mode before any actual 
	recording: just clean up everything and return. */

	if (g_clock.getStatus() == ClockStatus::WAITING)
	{
		g_clock.setStatus(ClockStatus::STOPPED);
		g_midiDispatcher.setSignalCallback(nullptr);
		v::dispatcher::setSignalCallback(nullptr);
		return;
	}

	std::unordered_set<ID> channels = g_actionRecorder.consolidate();

	/* Enable reading actions for Channels that have just been filled with 
	actions. Start reading right away, without checking whether 
	conf::treatRecsAsLoops is enabled or not. Same thing for MIDI channels.  */

	for (ID id : channels)
	{
		channel::Data& ch = model::get().getChannel(id);
		ch.state->readActions.store(true);
		if (ch.type == ChannelType::MIDI)
			ch.state->playStatus.store(ChannelStatus::PLAY);
	}
	model::swap(model::SwapType::HARD);
}

/* -------------------------------------------------------------------------- */

void Recorder::toggleActionRec(RecTriggerMode m)
{
	isRecordingAction() ? stopActionRec() : startActionRec(m);
}

/* -------------------------------------------------------------------------- */

bool Recorder::startInputRec(RecTriggerMode triggerMode, InputRecMode inputMode)
{
	if (!canRec() || !g_mixerHandler.hasInputRecordableChannels())
		return false;

	if (triggerMode == RecTriggerMode::SIGNAL || inputMode == InputRecMode::FREE)
		g_clock.rewind();

	if (inputMode == InputRecMode::FREE)
		g_mixer.setEndOfRecCallback([this, inputMode] { stopInputRec(inputMode); });

	if (triggerMode == RecTriggerMode::NORMAL)
	{
		startInputRec();
		setRecordingInput(true);
		G_DEBUG("Start input rec, NORMAL mode");
	}
	else
	{
		g_clock.setStatus(ClockStatus::WAITING);
		g_mixer.setSignalCallback([this] {
			startInputRec();
			setRecordingInput(true);
		});
		G_DEBUG("Start input rec, SIGNAL mode");
	}

	return true;
}

/* -------------------------------------------------------------------------- */

void Recorder::stopInputRec(InputRecMode recMode)
{
	setRecordingInput(false);

	Frame recordedFrames = g_mixer.stopInputRec();

	/* When recording in RIGID mode, the amount of recorded frames is always 
	equal to the current loop length. */

	if (recMode == InputRecMode::RIGID)
		recordedFrames = g_clock.getFramesInLoop();

	G_DEBUG("Stop input rec, recordedFrames=" << recordedFrames);

	/* If you stop the Input Recorder in SIGNAL mode before any actual 
	recording: just clean up everything and return. */

	if (g_clock.getStatus() == ClockStatus::WAITING)
	{
		g_clock.setStatus(ClockStatus::STOPPED);
		g_mixer.setSignalCallback(nullptr);
		return;
	}

	/* Finalize recordings. InputRecMode::FREE requires some adjustments. */

	g_mixerHandler.finalizeInputRec(recordedFrames);

	if (recMode == InputRecMode::FREE)
	{
		g_clock.rewind();
		g_clock.setBpm(g_clock.calcBpmFromRec(recordedFrames));
		g_mixer.setEndOfRecCallback(nullptr);
		refreshInputRecMode(); // Back to RIGID mode if necessary
	}
}

/* -------------------------------------------------------------------------- */

bool Recorder::toggleInputRec(RecTriggerMode m, InputRecMode i)
{
	if (isRecordingInput())
	{
		stopInputRec(i);
		return true;
	}
	return startInputRec(m, i);
}

/* -------------------------------------------------------------------------- */

bool Recorder::canEnableRecOnSignal() const { return !g_clock.isRunning(); }
bool Recorder::canEnableFreeInputRec() const { return !g_mixerHandler.hasAudioData(); }

/* -------------------------------------------------------------------------- */

void Recorder::refreshInputRecMode()
{
	if (!canEnableFreeInputRec())
		g_conf.inputRecMode = InputRecMode::RIGID;
}

/* -------------------------------------------------------------------------- */

bool Recorder::isKernelReady() const
{
	return g_kernelAudio.isReady();
}

/* -------------------------------------------------------------------------- */

bool Recorder::canRec() const
{
	return isKernelReady() && g_kernelAudio.isInputEnabled();
}

/* -------------------------------------------------------------------------- */

void Recorder::setRecordingAction(bool v)
{
	model::get().recorder.isRecordingAction = v;
	model::swap(model::SwapType::NONE);
}

void Recorder::setRecordingInput(bool v)
{
	model::get().recorder.isRecordingInput = v;
	model::swap(model::SwapType::NONE);
}

/* -------------------------------------------------------------------------- */

bool Recorder::startActionRec()
{
	g_clock.setStatus(ClockStatus::RUNNING);
	g_sequencer.start();
	g_conf.recTriggerMode = RecTriggerMode::NORMAL;
	return true;
}

/* -------------------------------------------------------------------------- */

void Recorder::startInputRec()
{
	/* Start recording from the current frame, not the beginning. */
	g_mixer.startInputRec(g_clock.getCurrentFrame());
	g_sequencer.start();
	g_conf.recTriggerMode = RecTriggerMode::NORMAL;
}
} // namespace giada::m
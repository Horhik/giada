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
#include "core/conf.h"
#include "core/kernelAudio.h"
#include "core/mixer.h"
#include "core/mixerHandler.h"
#include "core/model/model.h"
#include "core/sequencer.h"
#include "core/synchronizer.h"
#include "core/types.h"
#include "gui/dispatcher.h"
#include "src/core/actions/actionRecorder.h"
#include "src/core/actions/actions.h"

extern giada::m::KernelAudio  g_kernelAudio;
extern giada::m::Sequencer    g_sequencer;
extern giada::m::Mixer        g_mixer;
extern giada::m::Synchronizer g_synchronizer;
extern giada::m::conf::Data   g_conf;

namespace giada::m
{
Recorder::Recorder(model::Model& m)
: m_model(m)
{
}

/* -------------------------------------------------------------------------- */

bool Recorder::isRecording() const
{
	return isRecordingAction() || isRecordingInput();
}

bool Recorder::isRecordingAction() const
{
	return m_model.get().recorder.state->isRecordingAction.load();
}

bool Recorder::isRecordingInput() const
{
	return m_model.get().recorder.state->isRecordingInput.load();
}

/* -------------------------------------------------------------------------- */

void Recorder::prepareActionRec(RecTriggerMode mode, EventDispatcher& eventDispatcher)
{
	if (mode == RecTriggerMode::NORMAL)
	{
		startActionRec(eventDispatcher);
		G_DEBUG("Start action rec, NORMAL mode");
	}
	else
	{ // RecTriggerMode::SIGNAL
		g_sequencer.setStatus(SeqStatus::WAITING);
		g_sequencer.rewind();
		g_synchronizer.sendMIDIrewind();
		G_DEBUG("Start action rec, SIGNAL mode (waiting for signal from Midi Dispatcher...)");
		/////v::dispatcher::setSignalCallback([this]() { startActionRec(); }); // TODO !!!
	}
}

/* -------------------------------------------------------------------------- */

void Recorder::stopActionRec(ActionRecorder& actionRecorder)
{
	setRecordingAction(false);

	/* If you stop the Action Recorder in SIGNAL mode before any actual 
	recording: just clean up everything and return. */

	if (g_sequencer.getStatus() == SeqStatus::WAITING)
	{
		g_sequencer.setStatus(SeqStatus::STOPPED);
		g_synchronizer.sendMIDIstop();
		v::dispatcher::setSignalCallback(nullptr); // TODO
		return;
	}

	std::unordered_set<ID> channels = actionRecorder.consolidate();

	/* Enable reading actions for Channels that have just been filled with 
	actions. Start reading right away, without checking whether 
	conf::treatRecsAsLoops is enabled or not. Same thing for MIDI channels.  */

	for (ID id : channels)
	{
		channel::Data& ch = m_model.get().getChannel(id);
		ch.state->readActions.store(true);
		if (ch.type == ChannelType::MIDI)
			ch.state->playStatus.store(ChannelStatus::PLAY);
	}
	m_model.swap(model::SwapType::HARD);
}

/* -------------------------------------------------------------------------- */

bool Recorder::prepareInputRec(RecTriggerMode triggerMode, InputRecMode inputMode,
    EventDispatcher& eventDispatcher)
{
	if (triggerMode == RecTriggerMode::SIGNAL || inputMode == InputRecMode::FREE)
	{
		g_sequencer.rewind();
		g_synchronizer.sendMIDIrewind();
	}

	if (triggerMode == RecTriggerMode::NORMAL)
	{
		startInputRec(eventDispatcher);
		G_DEBUG("Start input rec, NORMAL mode");
	}
	else
	{
		g_sequencer.setStatus(SeqStatus::WAITING);
		G_DEBUG("Start input rec, SIGNAL mode (waiting for signal from Mixer...)");
	}

	return true;
}

/* -------------------------------------------------------------------------- */

void Recorder::stopInputRec(InputRecMode recMode, int sampleRate, MixerHandler& mixerHandler)
{
	setRecordingInput(false);

	Frame recordedFrames = g_mixer.stopInputRec();

	/* When recording in RIGID mode, the amount of recorded frames is always 
	equal to the current loop length. */

	if (recMode == InputRecMode::RIGID)
		recordedFrames = g_sequencer.getFramesInLoop();

	G_DEBUG("Stop input rec, recordedFrames=" << recordedFrames);

	/* If you stop the Input Recorder in SIGNAL mode before any actual 
	recording: just clean up everything and return. */

	if (g_sequencer.getStatus() == SeqStatus::WAITING)
	{
		g_sequencer.setStatus(SeqStatus::STOPPED);
		return;
	}

	/* Finalize recordings. InputRecMode::FREE requires some adjustments. */

	mixerHandler.finalizeInputRec(recordedFrames, g_sequencer.getCurrentFrame());

	if (recMode == InputRecMode::FREE)
	{
		g_sequencer.rewind();
		g_synchronizer.sendMIDIrewind();
		g_sequencer.setBpm(g_sequencer.calcBpmFromRec(recordedFrames, sampleRate), g_kernelAudio, sampleRate);
		refreshInputRecMode(mixerHandler); // Back to RIGID mode if necessary
	}
}

/* -------------------------------------------------------------------------- */

bool Recorder::canEnableRecOnSignal() const { return !g_sequencer.isRunning(); }
bool Recorder::canEnableFreeInputRec(const MixerHandler& mixerHandler) const
{
	return !mixerHandler.hasAudioData();
}

/* -------------------------------------------------------------------------- */

void Recorder::refreshInputRecMode(const MixerHandler& mixerHandler)
{
	if (!canEnableFreeInputRec(mixerHandler))
		g_conf.inputRecMode = InputRecMode::RIGID;
}

/* -------------------------------------------------------------------------- */

void Recorder::setRecordingAction(bool v)
{
	m_model.get().recorder.state->isRecordingAction.store(v);
}

void Recorder::setRecordingInput(bool v)
{
	m_model.get().recorder.state->isRecordingInput.store(v);
}

/* -------------------------------------------------------------------------- */

void Recorder::startActionRec(EventDispatcher& eventDispatcher)
{
	g_sequencer.setStatus(SeqStatus::RUNNING);
	g_synchronizer.sendMIDIstart();
	eventDispatcher.pumpUIevent({EventDispatcher::EventType::SEQUENCER_START});
	setRecordingAction(true);
}

/* -------------------------------------------------------------------------- */

void Recorder::startInputRec(EventDispatcher& eventDispatcher)
{
	/* Start recording from the current frame, not the beginning. */
	g_mixer.startInputRec(g_sequencer.getCurrentFrame());
	eventDispatcher.pumpUIevent({EventDispatcher::EventType::SEQUENCER_START});
	setRecordingInput(true);
}
} // namespace giada::m
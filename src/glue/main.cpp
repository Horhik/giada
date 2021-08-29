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

#include "main.h"
#include "core/actionRecorder.h"
#include "core/clock.h"
#include "core/conf.h"
#include "core/const.h"
#include "core/init.h"
#include "core/kernelAudio.h"
#include "core/kernelMidi.h"
#include "core/mixer.h"
#include "core/mixerHandler.h"
#include "core/model/model.h"
#include "core/plugins/pluginHost.h"
#include "core/plugins/pluginManager.h"
#include "core/recManager.h"
#include "core/recorderHandler.h"
#include "gui/dialogs/mainWindow.h"
#include "gui/dialogs/warnings.h"
#include "gui/elems/mainWindow/keyboard/keyboard.h"
#include "gui/elems/mainWindow/keyboard/sampleChannel.h"
#include "gui/elems/mainWindow/mainIO.h"
#include "gui/elems/mainWindow/mainTimer.h"
#include "utils/gui.h"
#include "utils/log.h"
#include "utils/string.h"
#include <FL/Fl.H>
#include <cassert>
#include <cmath>

extern giada::v::gdMainWindow* G_MainWin;
extern giada::m::KernelAudio   g_kernelAudio;
extern giada::m::Clock         g_clock;
extern giada::m::Mixer         g_mixer;
extern giada::m::MixerHandler  g_mixerHandler;

namespace giada::c::main
{
Timer::Timer(const m::model::Clock& c)
: bpm(c.bpm)
, beats(c.beats)
, bars(c.bars)
, quantize(c.quantize)
, isUsingJack(g_kernelAudio.getAPI() == G_SYS_API_JACK)
, isRecordingInput(m::recManager::isRecordingInput())
{
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

IO::IO(const m::channel::Data& out, const m::channel::Data& in, const m::model::Mixer& m)
: masterOutVol(out.volume)
, masterInVol(in.volume)
#ifdef WITH_VST
, masterOutHasPlugins(out.plugins.size() > 0)
, masterInHasPlugins(in.plugins.size() > 0)
#endif
, inToOut(m.inToOut)
{
}

/* -------------------------------------------------------------------------- */

Peak IO::getMasterOutPeak()
{
	return g_mixer.getPeakOut();
}

Peak IO::getMasterInPeak()
{
	return g_mixer.getPeakIn();
}

/* -------------------------------------------------------------------------- */

bool IO::isKernelReady()
{
	return g_kernelAudio.isReady();
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

Timer getTimer()
{
	return Timer(m::model::get().clock);
}

/* -------------------------------------------------------------------------- */

IO getIO()
{
	return IO(m::model::get().getChannel(m::Mixer::MASTER_OUT_CHANNEL_ID),
	    m::model::get().getChannel(m::Mixer::MASTER_IN_CHANNEL_ID),
	    m::model::get().mixer);
}

/* -------------------------------------------------------------------------- */

Sequencer getSequencer()
{
	Sequencer out;

	m::Mixer::RecordInfo recInfo = g_mixer.getRecordInfo();

	out.isFreeModeInputRec = m::recManager::isRecordingInput() && m::conf::conf.inputRecMode == InputRecMode::FREE;
	out.shouldBlink        = u::gui::shouldBlink() && (g_clock.getStatus() == ClockStatus::WAITING || out.isFreeModeInputRec);
	out.beats              = g_clock.getBeats();
	out.bars               = g_clock.getBars();
	out.currentBeat        = g_clock.getCurrentBeat();
	out.recPosition        = recInfo.position;
	out.recMaxLength       = recInfo.maxLength;

	return out;
}

/* -------------------------------------------------------------------------- */

void setBpm(const char* i, const char* f)
{
	/* Never change this stuff while recording audio. */

	if (m::recManager::isRecordingInput())
		return;

	g_clock.setBpm(std::atof(i) + (std::atof(f) / 10.0f));
}

/* -------------------------------------------------------------------------- */

void setBpm(float f)
{
	/* Never change this stuff while recording audio. */

	if (m::recManager::isRecordingInput())
		return;

	g_clock.setBpm(f);
}

/* -------------------------------------------------------------------------- */

void setBeats(int beats, int bars)
{
	/* Never change this stuff while recording audio. */

	if (m::recManager::isRecordingInput())
		return;

	g_clock.setBeats(beats, bars);
	g_mixer.allocRecBuffer(g_clock.getMaxFramesInLoop());
}

/* -------------------------------------------------------------------------- */

void quantize(int val)
{
	g_clock.setQuantize(val);
}

/* -------------------------------------------------------------------------- */

void clearAllSamples()
{
	if (!v::gdConfirmWin("Warning", "Free all Sample channels: are you sure?"))
		return;
	G_MainWin->delSubWindow(WID_SAMPLE_EDITOR);
	g_clock.setStatus(ClockStatus::STOPPED);
	g_mixerHandler.freeAllChannels();
	m::recorderHandler::clearAllActions();
}

/* -------------------------------------------------------------------------- */

void clearAllActions()
{
	if (!v::gdConfirmWin("Warning", "Clear all actions: are you sure?"))
		return;
	G_MainWin->delSubWindow(WID_ACTION_EDITOR);
	m::recorderHandler::clearAllActions();
}

/* -------------------------------------------------------------------------- */

void setInToOut(bool v)
{
	g_mixerHandler.setInToOut(v);
}

/* -------------------------------------------------------------------------- */

void toggleRecOnSignal()
{
	if (!m::recManager::canEnableRecOnSignal())
	{
		m::conf::conf.recTriggerMode = RecTriggerMode::NORMAL;
		return;
	}
	m::conf::conf.recTriggerMode = m::conf::conf.recTriggerMode == RecTriggerMode::NORMAL ? RecTriggerMode::SIGNAL : RecTriggerMode::NORMAL;
}

/* -------------------------------------------------------------------------- */

void toggleFreeInputRec()
{
	if (!m::recManager::canEnableFreeInputRec())
	{
		m::conf::conf.inputRecMode = InputRecMode::RIGID;
		return;
	}
	m::conf::conf.inputRecMode = m::conf::conf.inputRecMode == InputRecMode::FREE ? InputRecMode::RIGID : InputRecMode::FREE;
}

/* -------------------------------------------------------------------------- */

void closeProject()
{
	if (!v::gdConfirmWin("Warning", "Close project: are you sure?"))
		return;
	m::init::reset();
	g_mixer.enable();
}
} // namespace giada::c::main

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

#include "core/channels/channelManager.h"
#include "core/clock.h"
#include "core/conf.h"
#include "core/eventDispatcher.h"
#include "core/init.h"
#include "core/kernelAudio.h"
#include "core/kernelMidi.h"
#include "core/midiDispatcher.h"
#include "core/midiMap.h"
#include "core/mixer.h"
#include "core/mixerHandler.h"
#include "core/model/model.h"
#include "core/patch.h"
#include "core/plugins/pluginHost.h"
#include "core/plugins/pluginManager.h"
#include "core/recorder.h"
#include "core/sequencer.h"
#include "core/sync.h"
#include "core/waveManager.h"
#include "gui/dialogs/mainWindow.h"
#include "src/core/actions/actionRecorder.h"
#include "src/core/actions/actions.h"
#include <FL/Fl.H>
#ifdef WITH_TESTS
#define CATCH_CONFIG_RUNNER
#include "tests/recorder.cpp"
#include "tests/utils.cpp"
#include "tests/wave.cpp"
#include "tests/waveFx.cpp"
#include "tests/waveManager.cpp"
#include <catch2/catch.hpp>
#include <string>
#include <vector>
#endif

giada::m::model::Model          g_model;
giada::m::conf::Data            g_conf;
giada::m::patch::Data           g_patch;
giada::m::midiMap::Data         g_midiMap;
giada::m::KernelAudio           g_kernelAudio;
giada::m::KernelMidi            g_kernelMidi;
giada::m::EventDispatcher       g_eventDispatcher;
giada::m::MidiDispatcher        g_midiDispatcher(g_eventDispatcher, g_model);
giada::m::Actions               g_actions(g_model);
/*! */ giada::m::ActionRecorder g_actionRecorder;
/*! */ giada::m::Recorder       g_recorder;
giada::m::Synchronizer          g_synchronizer(g_conf, g_kernelMidi);
/*! */ giada::m::Clock          g_clock(g_kernelAudio, g_synchronizer);
/*! */ giada::m::Sequencer      g_sequencer(g_kernelAudio, g_clock);
/*! */ giada::m::Mixer          g_mixer(g_clock.getMaxFramesInLoop(), g_kernelAudio.getRealBufSize());
/*! */ giada::m::MixerHandler   g_mixerHandler(g_clock.getMaxFramesInLoop(), g_kernelAudio.getRealBufSize());
/*! */ giada::m::PluginHost     g_pluginHost(g_kernelAudio.getRealBufSize());
/*! */ giada::m::PluginManager  g_pluginManager;
giada::m::ChannelManager        g_channelManager(g_conf, g_model);
giada::m::WaveManager           g_waveManager;
giada::v::gdMainWindow*         G_MainWin = nullptr;

int main(int argc, char** argv)
{
#ifdef WITH_TESTS
	std::vector<char*> args(argv, argv + argc);
	if (args.size() > 1 && strcmp(args[1], "--run-tests") == 0)
		return Catch::Session().run(args.size() - 1, &args[1]);
#endif

	// TODO - move the setup to Engine class
	// TODO - move the setup to Engine class
	// TODO - move the setup to Engine class
	g_kernelMidi.onMidiReceived = [](uint32_t msg) { g_midiDispatcher.dispatch(msg); };

#ifdef WITH_AUDIO_JACK
	g_synchronizer.onJackRewind    = []() { g_sequencer.rawRewind(); };
	g_synchronizer.onJackChangeBpm = [](float bpm) { g_clock.setBpmRaw(bpm); };
	g_synchronizer.onJackStart     = []() { g_sequencer.rawStart(); };
	g_synchronizer.onJackStop      = []() { g_sequencer.rawStop(); };
#endif

	g_eventDispatcher.onMidiLearn       = [](const MidiEvent& e) { g_midiDispatcher.learn(e); };
	g_eventDispatcher.onMidiProcess     = [](const MidiEvent& e) { g_midiDispatcher.process(e); };
	g_eventDispatcher.onProcessChannels = [](const EventDispatcher::EventBuffer& eb) {
		for (channel::Data& ch : g_model.get().channels)
			channel::react(ch, eb, g_mixer.isChannelAudible(ch));
		g_model.swap(model::SwapType::SOFT); // TODO - is this necessary???
	};
	g_eventDispatcher.onProcessSequencer      = [](const EventDispatcher::EventBuffer& eb) { g_sequencer.react(eb); };
	g_eventDispatcher.onMixerSignalCallback   = []() { g_mixer.execSignalCb(); };
	g_eventDispatcher.onMixerEndOfRecCallback = []() { g_mixer.execEndOfRecCb(); };
	// TODO - move the setup to Engine class
	// TODO - move the setup to Engine class
	// TODO - move the setup to Engine class

	giada::m::init::startup(argc, argv);

	Fl::lock(); // Enable multithreading in FLTK
	int ret = Fl::run();

	giada::m::init::shutdown();

	return ret;
}
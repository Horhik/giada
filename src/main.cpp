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
#include "core/synchronizer.h"
#include "core/waveManager.h"
#include "deps/mcl-audio-buffer/src/audioBuffer.hpp"
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

giada::m::model::Model    g_model;
giada::m::conf::Data      g_conf;
giada::m::patch::Data     g_patch;
giada::m::midiMap::Data   g_midiMap;
giada::m::KernelAudio     g_kernelAudio;
giada::m::KernelMidi      g_kernelMidi;
giada::m::ChannelManager  g_channelManager(g_conf, g_model);
giada::m::PluginManager   g_pluginManager(static_cast<PluginManager::SortMethod>(g_conf.pluginSortMethod));
giada::m::WaveManager     g_waveManager;
giada::m::EventDispatcher g_eventDispatcher;
giada::m::MidiDispatcher  g_midiDispatcher(g_model);
giada::m::ActionRecorder  g_actionRecorder(g_model);
/*! */ giada::m::Recorder g_recorder(g_model);
giada::m::Synchronizer    g_synchronizer(g_conf, g_kernelMidi);
giada::m::Sequencer       g_sequencer(g_model);
giada::m::Mixer           g_mixer(g_model);
giada::m::MixerHandler    g_mixerHandler(g_model, g_mixer);
giada::m::PluginHost      g_pluginHost(g_pluginManager, g_model);
giada::v::gdMainWindow*   G_MainWin = nullptr;

// TODO - move to Engine class
// TODO - move to Engine class
// TODO - move to Engine class
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

	if (!g_kernelAudio.isReady() || !g_mixer.isActive())
		return 0;

#ifdef WITH_AUDIO_JACK
	if (g_kernelAudio.getAPI() == G_SYS_API_JACK)
		g_synchronizer.recvJackSync(g_kernelAudio.jackTransportQuery());
#endif

	Mixer::RenderInfo info;
	info.isAudioReady    = g_kernelAudio.isReady();
	info.hasInput        = g_kernelAudio.isInputEnabled();
	info.isClockActive   = g_sequencer.isActive();
	info.isClockRunning  = g_sequencer.isRunning();
	info.shouldLineInRec = g_recorder.isRecordingInput() && g_kernelAudio.isInputEnabled();
	info.limitOutput     = g_conf.limitOutput;
	info.inToOut         = g_mixerHandler.getInToOut();
	info.maxFramesToRec  = g_conf.inputRecMode == InputRecMode::FREE ? g_sequencer.getMaxFramesInLoop(g_conf.samplerate) : g_sequencer.getFramesInLoop();
	info.outVol          = g_mixerHandler.getOutVol();
	info.inVol           = g_mixerHandler.getInVol();
	info.recTriggerLevel = g_conf.recTriggerLevel;

	return g_mixer.render(out, in, info);
}

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
	g_kernelAudio.onAudioCallback = audioCallback_;

	g_kernelMidi.onMidiReceived = [](uint32_t msg) { g_midiDispatcher.dispatch(msg); };

#ifdef WITH_AUDIO_JACK
	g_synchronizer.onJackRewind    = []() { g_sequencer.rawRewind(); };
	g_synchronizer.onJackChangeBpm = [](float bpm) { g_sequencer.setBpmRaw(bpm, g_conf.samplerate); };
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
	g_eventDispatcher.onProcessSequencer = [](const EventDispatcher::EventBuffer& eb) {
		g_sequencer.react(eb, g_kernelAudio);
	};
	g_eventDispatcher.onMixerSignalCallback = []() {
		if (g_sequencer.getStatus() == SeqStatus::WAITING)
			g_recorder.startInputRec(g_eventDispatcher);
	};
	g_eventDispatcher.onMixerEndOfRecCallback = []() {
		if (g_recorder.isRecordingInput())
			g_recorder.stopInputRec(g_conf.inputRecMode, g_conf.samplerate, g_mixerHandler);
	};

	/* Notify Event Dispatcher when a MIDI signal is received. */
	g_midiDispatcher.onDispatch = [](EventDispatcher::EventType event, Action action) {
		g_eventDispatcher.pumpMidiEvent({event, 0, 0, action});
	};

	g_midiDispatcher.onEventReceived = []() {
		if (g_sequencer.getStatus() == SeqStatus::WAITING)
			g_recorder.startActionRec(g_eventDispatcher);
	};

	/* Invokes the signal callback. This is done by pumping a MIXER_SIGNAL_CALLBACK
	event to the Event Dispatcher, rather than invoking the callback directly.
	This is done on purpose: the callback might (and surely will) contain 
	blocking stuff from model:: that the realtime thread cannot perform 
	directly. */
	g_mixer.onSignalTresholdReached = []() {
		g_eventDispatcher.pumpUIevent({EventDispatcher::EventType::MIXER_SIGNAL_CALLBACK});
	};

	/* Same rationale as above, for the end-of-recording callback. */
	g_mixer.onEndOfRecording = []() {
		g_eventDispatcher.pumpUIevent({EventDispatcher::EventType::MIXER_END_OF_REC_CALLBACK});
	};

	g_mixer.onProcessSequencer = [](Frame frames, mcl::AudioBuffer& out) {
		const Sequencer::EventBuffer& events = g_sequencer.advance(frames, g_actionRecorder);
		g_sequencer.render(out);
		return events;
	};

	g_mixerHandler.onChannelsAltered = []() {
		g_recorder.refreshInputRecMode(g_mixerHandler);
	};
	g_mixerHandler.onChannelRecorded = [](Frame recordedFrames) {
		std::string filename = "TAKE-" + std::to_string(g_patch.lastTakeId++) + ".wav";
		return g_waveManager.createEmpty(recordedFrames, G_MAX_IO_CHANS, g_conf.samplerate, filename);
	};

	g_sequencer.onAboutStart = [](SeqStatus status) {
		/* TODO move this logic to Recorder */
		if (status == SeqStatus::WAITING)
			g_recorder.stopActionRec(g_actionRecorder);
		g_conf.recTriggerMode = RecTriggerMode::NORMAL;
	};

	g_sequencer.onAboutStop = []() {
		/* If recordings (both input and action) are active deactivate them, but 
	store the takes. RecManager takes care of it. */
		/* TODO move this logic to Recorder */
		if (g_recorder.isRecordingAction())
			g_recorder.stopActionRec(g_actionRecorder);
		else if (g_recorder.isRecordingInput())
			g_recorder.stopInputRec(g_conf.inputRecMode, g_conf.samplerate, g_mixerHandler);
	};

	g_sequencer.onBpmChange = [](float oldVal, float newVal, int quantizerStep) {
		g_actionRecorder.updateBpm(oldVal / newVal, quantizerStep);
	};

	// TODO - move the setup to Engine class
	// TODO - move the setup to Engine class
	// TODO - move the setup to Engine class

	giada::m::init::startup(argc, argv);

	Fl::lock(); // Enable multithreading in FLTK
	int ret = Fl::run();

	giada::m::init::shutdown();

	return ret;
}
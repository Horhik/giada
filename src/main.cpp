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

#include "core/clock.h"
#include "core/conf.h"
#include "core/init.h"
#include "core/kernelAudio.h"
#include "core/kernelMidi.h"
#include "core/mixer.h"
#include "core/mixerHandler.h"
#include "core/plugins/pluginHost.h"
#include "core/sequencer.h"
#include "core/sync.h"
#include "gui/dialogs/mainWindow.h"
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

// TODO - conf
// TODO - patch
giada::m::KernelAudio  g_kernelAudio;
giada::m::KernelMidi   g_kernelMidi;
giada::m::Synchronizer g_synchronizer(giada::m::conf::conf.samplerate, giada::m::conf::conf.midiTCfps);
giada::m::Clock        g_clock(g_kernelAudio, g_synchronizer);
giada::m::Sequencer    g_sequencer(g_kernelAudio, g_clock);
giada::m::Mixer        g_mixer(g_clock.getMaxFramesInLoop(), g_kernelAudio.getRealBufSize());
giada::m::MixerHandler g_mixerHandler(g_clock.getMaxFramesInLoop(), g_kernelAudio.getRealBufSize());
giada::m::PluginHost   g_pluginHost(g_kernelAudio.getRealBufSize());

class giada::v::gdMainWindow* G_MainWin = nullptr;

int main(int argc, char** argv)
{
#ifdef WITH_TESTS
	std::vector<char*> args(argv, argv + argc);
	if (args.size() > 1 && strcmp(args[1], "--run-tests") == 0)
		return Catch::Session().run(args.size() - 1, &args[1]);
#endif

	giada::m::init::startup(argc, argv);

	Fl::lock(); // Enable multithreading in FLTK
	int ret = Fl::run();

	giada::m::init::shutdown();

	return ret;
}
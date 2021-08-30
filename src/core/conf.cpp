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

#include "conf.h"
#include "core/const.h"
#include "core/types.h"
#include "deps/json/single_include/nlohmann/json.hpp"
#include "utils/fs.h"
#include "utils/log.h"
#include <FL/Fl.H>
#include <cassert>
#include <fstream>
#include <string>

extern giada::m::conf::Data g_conf;

namespace nl = nlohmann;

namespace giada::m::conf
{
namespace
{
std::string confFilePath_ = "";
std::string confDirPath_  = "";

/* -------------------------------------------------------------------------- */

void sanitize_()
{
	g_conf.soundDeviceOut   = std::max(0, g_conf.soundDeviceOut);
	g_conf.channelsOutCount = G_MAX_IO_CHANS;
	g_conf.channelsOutStart = std::max(0, g_conf.channelsOutStart);
	g_conf.channelsInCount  = std::max(1, g_conf.channelsInCount);
	g_conf.channelsInStart  = std::max(0, g_conf.channelsInStart);
}

/* -------------------------------------------------------------------------- */

/* createConfigFolder
Creates local folder where to put the configuration file. Path differs from OS
to OS. */

int createConfigFolder_()
{
#if defined(__linux__) || defined(__FreeBSD__) || defined(__APPLE__)

	if (u::fs::dirExists(confDirPath_))
		return 1;

	u::log::print("[conf::createConfigFolder] .giada folder not present. Updating...\n");

	if (u::fs::mkdir(confDirPath_))
	{
		u::log::print("[conf::createConfigFolder] status: ok\n");
		return 1;
	}
	else
	{
		u::log::print("[conf::createConfigFolder] status: error!\n");
		return 0;
	}

#else // Windows: nothing to do

	return 1;

#endif
}
} // namespace

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

void init()
{
	g_conf = Data();

	/* Initialize confFilePath_, i.e. the configuration file. In windows it is in
	 * the same dir of the .exe, while in Linux and OS X in ~/.giada */

#if defined(__linux__) || defined(__FreeBSD__) || defined(__APPLE__)

	confFilePath_ = u::fs::getHomePath() + G_SLASH + CONF_FILENAME;
	confDirPath_  = u::fs::getHomePath() + G_SLASH;

#elif defined(_WIN32)

	confFilePath_ = CONF_FILENAME;
	confDirPath_  = "";

#endif
}

/* -------------------------------------------------------------------------- */

bool read()
{
	init();

	std::ifstream ifs(confFilePath_);
	if (!ifs.good())
		return false;

	nl::json j = nl::json::parse(ifs);

	g_conf.logMode                    = j.value(CONF_KEY_LOG_MODE, g_conf.logMode);
	g_conf.showTooltips               = j.value(CONF_KEY_SHOW_TOOLTIPS, g_conf.showTooltips);
	g_conf.soundSystem                = j.value(CONF_KEY_SOUND_SYSTEM, g_conf.soundSystem);
	g_conf.soundDeviceOut             = j.value(CONF_KEY_SOUND_DEVICE_OUT, g_conf.soundDeviceOut);
	g_conf.soundDeviceIn              = j.value(CONF_KEY_SOUND_DEVICE_IN, g_conf.soundDeviceIn);
	g_conf.channelsOutCount           = j.value(CONF_KEY_CHANNELS_OUT_COUNT, g_conf.channelsOutCount);
	g_conf.channelsOutStart           = j.value(CONF_KEY_CHANNELS_OUT_START, g_conf.channelsOutStart);
	g_conf.channelsInCount            = j.value(CONF_KEY_CHANNELS_IN_COUNT, g_conf.channelsInCount);
	g_conf.channelsInStart            = j.value(CONF_KEY_CHANNELS_IN_START, g_conf.channelsInStart);
	g_conf.samplerate                 = j.value(CONF_KEY_SAMPLERATE, g_conf.samplerate);
	g_conf.buffersize                 = j.value(CONF_KEY_BUFFER_SIZE, g_conf.buffersize);
	g_conf.limitOutput                = j.value(CONF_KEY_LIMIT_OUTPUT, g_conf.limitOutput);
	g_conf.rsmpQuality                = j.value(CONF_KEY_RESAMPLE_QUALITY, g_conf.rsmpQuality);
	g_conf.midiSystem                 = j.value(CONF_KEY_MIDI_SYSTEM, g_conf.midiSystem);
	g_conf.midiPortOut                = j.value(CONF_KEY_MIDI_PORT_OUT, g_conf.midiPortOut);
	g_conf.midiPortIn                 = j.value(CONF_KEY_MIDI_PORT_IN, g_conf.midiPortIn);
	g_conf.midiMapPath                = j.value(CONF_KEY_MIDIMAP_PATH, g_conf.midiMapPath);
	g_conf.lastFileMap                = j.value(CONF_KEY_LAST_MIDIMAP, g_conf.lastFileMap);
	g_conf.midiSync                   = j.value(CONF_KEY_MIDI_SYNC, g_conf.midiSync);
	g_conf.midiTCfps                  = j.value(CONF_KEY_MIDI_TC_FPS, g_conf.midiTCfps);
	g_conf.chansStopOnSeqHalt         = j.value(CONF_KEY_CHANS_STOP_ON_SEQ_HALT, g_conf.chansStopOnSeqHalt);
	g_conf.treatRecsAsLoops           = j.value(CONF_KEY_TREAT_RECS_AS_LOOPS, g_conf.treatRecsAsLoops);
	g_conf.inputMonitorDefaultOn      = j.value(CONF_KEY_INPUT_MONITOR_DEFAULT_ON, g_conf.inputMonitorDefaultOn);
	g_conf.overdubProtectionDefaultOn = j.value(CONF_KEY_OVERDUB_PROTECTION_DEFAULT_ON, g_conf.overdubProtectionDefaultOn);
	g_conf.pluginPath                 = j.value(CONF_KEY_PLUGINS_PATH, g_conf.pluginPath);
	g_conf.patchPath                  = j.value(CONF_KEY_PATCHES_PATH, g_conf.patchPath);
	g_conf.samplePath                 = j.value(CONF_KEY_SAMPLES_PATH, g_conf.samplePath);
	g_conf.mainWindowX                = j.value(CONF_KEY_MAIN_WINDOW_X, g_conf.mainWindowX);
	g_conf.mainWindowY                = j.value(CONF_KEY_MAIN_WINDOW_Y, g_conf.mainWindowY);
	g_conf.mainWindowW                = j.value(CONF_KEY_MAIN_WINDOW_W, g_conf.mainWindowW);
	g_conf.mainWindowH                = j.value(CONF_KEY_MAIN_WINDOW_H, g_conf.mainWindowH);
	g_conf.browserX                   = j.value(CONF_KEY_BROWSER_X, g_conf.browserX);
	g_conf.browserY                   = j.value(CONF_KEY_BROWSER_Y, g_conf.browserY);
	g_conf.browserW                   = j.value(CONF_KEY_BROWSER_W, g_conf.browserW);
	g_conf.browserH                   = j.value(CONF_KEY_BROWSER_H, g_conf.browserH);
	g_conf.browserPosition            = j.value(CONF_KEY_BROWSER_POSITION, g_conf.browserPosition);
	g_conf.browserLastPath            = j.value(CONF_KEY_BROWSER_LAST_PATH, g_conf.browserLastPath);
	g_conf.browserLastValue           = j.value(CONF_KEY_BROWSER_LAST_VALUE, g_conf.browserLastValue);
	g_conf.actionEditorX              = j.value(CONF_KEY_ACTION_EDITOR_X, g_conf.actionEditorX);
	g_conf.actionEditorY              = j.value(CONF_KEY_ACTION_EDITOR_Y, g_conf.actionEditorY);
	g_conf.actionEditorW              = j.value(CONF_KEY_ACTION_EDITOR_W, g_conf.actionEditorW);
	g_conf.actionEditorH              = j.value(CONF_KEY_ACTION_EDITOR_H, g_conf.actionEditorH);
	g_conf.actionEditorZoom           = j.value(CONF_KEY_ACTION_EDITOR_ZOOM, g_conf.actionEditorZoom);
	g_conf.actionEditorSplitH         = j.value(CONF_KEY_ACTION_EDITOR_SPLIT_H, g_conf.actionEditorSplitH);
	g_conf.actionEditorGridVal        = j.value(CONF_KEY_ACTION_EDITOR_GRID_VAL, g_conf.actionEditorGridVal);
	g_conf.actionEditorGridOn         = j.value(CONF_KEY_ACTION_EDITOR_GRID_ON, g_conf.actionEditorGridOn);
	g_conf.actionEditorPianoRollY     = j.value(CONF_KEY_ACTION_EDITOR_PIANO_ROLL_Y, g_conf.actionEditorPianoRollY);
	g_conf.sampleEditorX              = j.value(CONF_KEY_SAMPLE_EDITOR_X, g_conf.sampleEditorX);
	g_conf.sampleEditorY              = j.value(CONF_KEY_SAMPLE_EDITOR_Y, g_conf.sampleEditorY);
	g_conf.sampleEditorW              = j.value(CONF_KEY_SAMPLE_EDITOR_W, g_conf.sampleEditorW);
	g_conf.sampleEditorH              = j.value(CONF_KEY_SAMPLE_EDITOR_H, g_conf.sampleEditorH);
	g_conf.sampleEditorGridVal        = j.value(CONF_KEY_SAMPLE_EDITOR_GRID_VAL, g_conf.sampleEditorGridVal);
	g_conf.sampleEditorGridOn         = j.value(CONF_KEY_SAMPLE_EDITOR_GRID_ON, g_conf.sampleEditorGridOn);
	g_conf.pluginListX                = j.value(CONF_KEY_PLUGIN_LIST_X, g_conf.pluginListX);
	g_conf.pluginListY                = j.value(CONF_KEY_PLUGIN_LIST_Y, g_conf.pluginListY);
	g_conf.midiInputX                 = j.value(CONF_KEY_MIDI_INPUT_X, g_conf.midiInputX);
	g_conf.midiInputY                 = j.value(CONF_KEY_MIDI_INPUT_Y, g_conf.midiInputY);
	g_conf.midiInputW                 = j.value(CONF_KEY_MIDI_INPUT_W, g_conf.midiInputW);
	g_conf.midiInputH                 = j.value(CONF_KEY_MIDI_INPUT_H, g_conf.midiInputH);
	g_conf.recTriggerMode             = j.value(CONF_KEY_REC_TRIGGER_MODE, g_conf.recTriggerMode);
	g_conf.recTriggerLevel            = j.value(CONF_KEY_REC_TRIGGER_LEVEL, g_conf.recTriggerLevel);
	g_conf.inputRecMode               = j.value(CONF_KEY_INPUT_REC_MODE, g_conf.inputRecMode);
	g_conf.midiInEnabled              = j.value(CONF_KEY_MIDI_IN, g_conf.midiInEnabled);
	g_conf.midiInFilter               = j.value(CONF_KEY_MIDI_IN_FILTER, g_conf.midiInFilter);
	g_conf.midiInRewind               = j.value(CONF_KEY_MIDI_IN_REWIND, g_conf.midiInRewind);
	g_conf.midiInStartStop            = j.value(CONF_KEY_MIDI_IN_START_STOP, g_conf.midiInStartStop);
	g_conf.midiInActionRec            = j.value(CONF_KEY_MIDI_IN_ACTION_REC, g_conf.midiInActionRec);
	g_conf.midiInInputRec             = j.value(CONF_KEY_MIDI_IN_INPUT_REC, g_conf.midiInInputRec);
	g_conf.midiInMetronome            = j.value(CONF_KEY_MIDI_IN_METRONOME, g_conf.midiInMetronome);
	g_conf.midiInVolumeIn             = j.value(CONF_KEY_MIDI_IN_VOLUME_IN, g_conf.midiInVolumeIn);
	g_conf.midiInVolumeOut            = j.value(CONF_KEY_MIDI_IN_VOLUME_OUT, g_conf.midiInVolumeOut);
	g_conf.midiInBeatDouble           = j.value(CONF_KEY_MIDI_IN_BEAT_DOUBLE, g_conf.midiInBeatDouble);
	g_conf.midiInBeatHalf             = j.value(CONF_KEY_MIDI_IN_BEAT_HALF, g_conf.midiInBeatHalf);
#ifdef WITH_VST
	g_conf.pluginChooserX   = j.value(CONF_KEY_PLUGIN_CHOOSER_X, g_conf.pluginChooserX);
	g_conf.pluginChooserY   = j.value(CONF_KEY_PLUGIN_CHOOSER_Y, g_conf.pluginChooserY);
	g_conf.pluginChooserW   = j.value(CONF_KEY_PLUGIN_CHOOSER_W, g_conf.pluginChooserW);
	g_conf.pluginChooserH   = j.value(CONF_KEY_PLUGIN_CHOOSER_H, g_conf.pluginChooserH);
	g_conf.pluginSortMethod = j.value(CONF_KEY_PLUGIN_SORT_METHOD, g_conf.pluginSortMethod);
#endif

	sanitize_();

	return true;
}

/* -------------------------------------------------------------------------- */

bool write()
{
	if (!createConfigFolder_())
		return false;

	nl::json j;

	j[CONF_KEY_HEADER]                        = "GIADACFG";
	j[CONF_KEY_LOG_MODE]                      = g_conf.logMode;
	j[CONF_KEY_SHOW_TOOLTIPS]                 = g_conf.showTooltips;
	j[CONF_KEY_SOUND_SYSTEM]                  = g_conf.soundSystem;
	j[CONF_KEY_SOUND_DEVICE_OUT]              = g_conf.soundDeviceOut;
	j[CONF_KEY_SOUND_DEVICE_IN]               = g_conf.soundDeviceIn;
	j[CONF_KEY_CHANNELS_OUT_COUNT]            = g_conf.channelsOutCount;
	j[CONF_KEY_CHANNELS_OUT_START]            = g_conf.channelsOutStart;
	j[CONF_KEY_CHANNELS_IN_COUNT]             = g_conf.channelsInCount;
	j[CONF_KEY_CHANNELS_IN_START]             = g_conf.channelsInStart;
	j[CONF_KEY_SAMPLERATE]                    = g_conf.samplerate;
	j[CONF_KEY_BUFFER_SIZE]                   = g_conf.buffersize;
	j[CONF_KEY_LIMIT_OUTPUT]                  = g_conf.limitOutput;
	j[CONF_KEY_RESAMPLE_QUALITY]              = g_conf.rsmpQuality;
	j[CONF_KEY_MIDI_SYSTEM]                   = g_conf.midiSystem;
	j[CONF_KEY_MIDI_PORT_OUT]                 = g_conf.midiPortOut;
	j[CONF_KEY_MIDI_PORT_IN]                  = g_conf.midiPortIn;
	j[CONF_KEY_MIDIMAP_PATH]                  = g_conf.midiMapPath;
	j[CONF_KEY_LAST_MIDIMAP]                  = g_conf.lastFileMap;
	j[CONF_KEY_MIDI_SYNC]                     = g_conf.midiSync;
	j[CONF_KEY_MIDI_TC_FPS]                   = g_conf.midiTCfps;
	j[CONF_KEY_MIDI_IN]                       = g_conf.midiInEnabled;
	j[CONF_KEY_MIDI_IN_FILTER]                = g_conf.midiInFilter;
	j[CONF_KEY_MIDI_IN_REWIND]                = g_conf.midiInRewind;
	j[CONF_KEY_MIDI_IN_START_STOP]            = g_conf.midiInStartStop;
	j[CONF_KEY_MIDI_IN_ACTION_REC]            = g_conf.midiInActionRec;
	j[CONF_KEY_MIDI_IN_INPUT_REC]             = g_conf.midiInInputRec;
	j[CONF_KEY_MIDI_IN_METRONOME]             = g_conf.midiInMetronome;
	j[CONF_KEY_MIDI_IN_VOLUME_IN]             = g_conf.midiInVolumeIn;
	j[CONF_KEY_MIDI_IN_VOLUME_OUT]            = g_conf.midiInVolumeOut;
	j[CONF_KEY_MIDI_IN_BEAT_DOUBLE]           = g_conf.midiInBeatDouble;
	j[CONF_KEY_MIDI_IN_BEAT_HALF]             = g_conf.midiInBeatHalf;
	j[CONF_KEY_CHANS_STOP_ON_SEQ_HALT]        = g_conf.chansStopOnSeqHalt;
	j[CONF_KEY_TREAT_RECS_AS_LOOPS]           = g_conf.treatRecsAsLoops;
	j[CONF_KEY_INPUT_MONITOR_DEFAULT_ON]      = g_conf.inputMonitorDefaultOn;
	j[CONF_KEY_OVERDUB_PROTECTION_DEFAULT_ON] = g_conf.overdubProtectionDefaultOn;
	j[CONF_KEY_PLUGINS_PATH]                  = g_conf.pluginPath;
	j[CONF_KEY_PATCHES_PATH]                  = g_conf.patchPath;
	j[CONF_KEY_SAMPLES_PATH]                  = g_conf.samplePath;
	j[CONF_KEY_MAIN_WINDOW_X]                 = g_conf.mainWindowX;
	j[CONF_KEY_MAIN_WINDOW_Y]                 = g_conf.mainWindowY;
	j[CONF_KEY_MAIN_WINDOW_W]                 = g_conf.mainWindowW;
	j[CONF_KEY_MAIN_WINDOW_H]                 = g_conf.mainWindowH;
	j[CONF_KEY_BROWSER_X]                     = g_conf.browserX;
	j[CONF_KEY_BROWSER_Y]                     = g_conf.browserY;
	j[CONF_KEY_BROWSER_W]                     = g_conf.browserW;
	j[CONF_KEY_BROWSER_H]                     = g_conf.browserH;
	j[CONF_KEY_BROWSER_POSITION]              = g_conf.browserPosition;
	j[CONF_KEY_BROWSER_LAST_PATH]             = g_conf.browserLastPath;
	j[CONF_KEY_BROWSER_LAST_VALUE]            = g_conf.browserLastValue;
	j[CONF_KEY_ACTION_EDITOR_X]               = g_conf.actionEditorX;
	j[CONF_KEY_ACTION_EDITOR_Y]               = g_conf.actionEditorY;
	j[CONF_KEY_ACTION_EDITOR_W]               = g_conf.actionEditorW;
	j[CONF_KEY_ACTION_EDITOR_H]               = g_conf.actionEditorH;
	j[CONF_KEY_ACTION_EDITOR_ZOOM]            = g_conf.actionEditorZoom;
	j[CONF_KEY_ACTION_EDITOR_SPLIT_H]         = g_conf.actionEditorSplitH;
	j[CONF_KEY_ACTION_EDITOR_GRID_VAL]        = g_conf.actionEditorGridVal;
	j[CONF_KEY_ACTION_EDITOR_GRID_ON]         = g_conf.actionEditorGridOn;
	j[CONF_KEY_ACTION_EDITOR_PIANO_ROLL_Y]    = g_conf.actionEditorPianoRollY;
	j[CONF_KEY_SAMPLE_EDITOR_X]               = g_conf.sampleEditorX;
	j[CONF_KEY_SAMPLE_EDITOR_Y]               = g_conf.sampleEditorY;
	j[CONF_KEY_SAMPLE_EDITOR_W]               = g_conf.sampleEditorW;
	j[CONF_KEY_SAMPLE_EDITOR_H]               = g_conf.sampleEditorH;
	j[CONF_KEY_SAMPLE_EDITOR_GRID_VAL]        = g_conf.sampleEditorGridVal;
	j[CONF_KEY_SAMPLE_EDITOR_GRID_ON]         = g_conf.sampleEditorGridOn;
	j[CONF_KEY_PLUGIN_LIST_X]                 = g_conf.pluginListX;
	j[CONF_KEY_PLUGIN_LIST_Y]                 = g_conf.pluginListY;
	j[CONF_KEY_MIDI_INPUT_X]                  = g_conf.midiInputX;
	j[CONF_KEY_MIDI_INPUT_Y]                  = g_conf.midiInputY;
	j[CONF_KEY_MIDI_INPUT_W]                  = g_conf.midiInputW;
	j[CONF_KEY_MIDI_INPUT_H]                  = g_conf.midiInputH;
	j[CONF_KEY_REC_TRIGGER_MODE]              = static_cast<int>(g_conf.recTriggerMode);
	j[CONF_KEY_REC_TRIGGER_LEVEL]             = g_conf.recTriggerLevel;
	j[CONF_KEY_INPUT_REC_MODE]                = static_cast<int>(g_conf.inputRecMode);
#ifdef WITH_VST
	j[CONF_KEY_PLUGIN_CHOOSER_X]   = g_conf.pluginChooserX;
	j[CONF_KEY_PLUGIN_CHOOSER_Y]   = g_conf.pluginChooserY;
	j[CONF_KEY_PLUGIN_CHOOSER_W]   = g_conf.pluginChooserW;
	j[CONF_KEY_PLUGIN_CHOOSER_H]   = g_conf.pluginChooserH;
	j[CONF_KEY_PLUGIN_SORT_METHOD] = g_conf.pluginSortMethod;
#endif

	std::ofstream ofs(confFilePath_);
	if (!ofs.good())
	{
		u::log::print("[conf::write] unable to write configuration file!\n");
		return false;
	}

	ofs << j;
	return true;
}
} // namespace giada::m::conf
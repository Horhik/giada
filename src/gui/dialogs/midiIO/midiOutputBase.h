/* -----------------------------------------------------------------------------
 *
 * Giada - Your Hardcore Loopmachine
 *
 * -----------------------------------------------------------------------------
 *
 * Copyright (C) 2010-2020 Giovanni A. Zuliani | Monocasual
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


#ifndef GD_MIDI_OUTPUT_BASE_H
#define GD_MIDI_OUTPUT_BASE_H


#include <FL/Fl.H>
#include "gui/dialogs/window.h"
#include "gui/elems/midiLearner.h"


class geButton;
class geCheck;


/* There's no such thing as a gdMidiOutputMaster vs gdMidiOutputChannel. MIDI
output master is managed by the configuration window, hence gdMidiOutput deals
only with channels.

Both MidiOutputMidiCh and MidiOutputSampleCh have the MIDI lighting widget set.
In addition MidiOutputMidiCh has the MIDI message output box. */

/* TODO - gdMidiOutput is almost the same thing of gdMidiInput. Create another
parent class gdMidiIO to inherit from */

namespace giada {
namespace v 
{
class geMidiLearner;
class gdMidiOutputBase : public gdWindow
{
public:

	gdMidiOutputBase(int w, int h);
	~gdMidiOutputBase();

	void refresh() override;

protected:

	/* cb_close
	close current window. */

	static void cb_close(Fl_Widget* w, void* p);
	void cb_close();

	/* cb_enableLightning
	enable MIDI lightning output. */

	static void cb_enableLightning(Fl_Widget* w, void* p);
	void cb_enableLightning();

	/* setTitle
	 * set window title. */

	void setTitle(int chanNum);

	std::vector<geMidiLearner*> m_learners;
	
	geButton* m_close;
	geCheck*  m_enableLightning;
};
}} // giada::v::


#endif

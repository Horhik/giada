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

#ifndef G_REC_MANAGER_H
#define G_REC_MANAGER_H

#include "core/types.h"

namespace giada::m::model
{
class Model;
}

namespace giada::m
{
class ActionRecorder;
class Recorder final
{
public:
	Recorder(model::Model&);

	bool isRecording() const;
	bool isRecordingAction() const;
	bool isRecordingInput() const;

	/* canEnableRecOnSignal
    True if rec-on-signal can be enabled: can't set it while sequencer is 
    running, in order to prevent mistakes while live recording. */

	bool canEnableRecOnSignal() const;

	/* canEnableFreeInputRec
    True if free loop-length can be enabled: Can't set it if there's already a 
    filled Sample Channel in the current project. */

	bool canEnableFreeInputRec() const;

	void startActionRec(RecTriggerMode);
	void stopActionRec(ActionRecorder&);
	bool startInputRec(RecTriggerMode, InputRecMode, int sampleRate);
	void stopInputRec(InputRecMode, int sampleRate);

	/* refreshInputRecMode
    Makes sure the input rec mode stays the right one when a new Sample Channel 
    is filled with data. See canEnableFreeInputRec() rationale. */

	void refreshInputRecMode();

private:
	void setRecordingAction(bool v);
	void setRecordingInput(bool v);

	void startActionRec();
	void startInputRec();

	model::Model& m_model;
};
} // namespace giada::m

#endif

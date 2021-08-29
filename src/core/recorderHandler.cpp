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

#include "core/recorderHandler.h"
#include "core/action.h"
#include "core/actionRecorder.h"
#include "core/clock.h"
#include "core/const.h"
#include "core/model/model.h"
#include "core/patch.h"
#include "utils/log.h"
#include "utils/ver.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <unordered_map>

extern giada::m::ActionRecorder g_actionRecorder;

namespace giada::m
{
namespace
{
constexpr int MAX_LIVE_RECS_CHUNK = 128;
} // namespace

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

ActionRecorderHandler::ActionRecorderHandler()
{
	m_actions.reserve(MAX_LIVE_RECS_CHUNK);
}

/* -------------------------------------------------------------------------- */

bool ActionRecorderHandler::isBoundaryEnvelopeAction(const Action& a) const
{
	assert(a.prev != nullptr);
	assert(a.next != nullptr);
	return a.prev->frame > a.frame || a.next->frame < a.frame;
}

/* -------------------------------------------------------------------------- */

void ActionRecorderHandler::updateBpm(float ratio, int quantizerStep)
{
	if (ratio == 1.0f)
		return;

	g_actionRecorder.updateKeyFrames([=](Frame old) {
		/* The division here cannot be precise. A new frame can be 44099 and the 
		quantizer set to 44100. That would mean two recs completely useless. So we 
		compute a reject value ('delta'): if it's lower than 6 frames the new frame 
		is collapsed with a quantized frame. FIXME - maybe 6 frames are too low. */
		Frame frame = static_cast<Frame>(old * ratio);
		if (frame != 0)
		{
			Frame delta = quantizerStep % frame;
			if (delta > 0 && delta <= 6)
				frame = frame + delta;
		}
		return frame;
	});
}

/* -------------------------------------------------------------------------- */

void ActionRecorderHandler::updateSamplerate(int systemRate, int patchRate)
{
	if (systemRate == patchRate)
		return;

	float ratio = systemRate / (float)patchRate;

	g_actionRecorder.updateKeyFrames([=](Frame old) { return floorf(old * ratio); });
}

/* -------------------------------------------------------------------------- */

bool ActionRecorderHandler::cloneActions(ID channelId, ID newChannelId)
{
	bool                       cloned = false;
	std::vector<Action>        actions;
	std::unordered_map<ID, ID> map; // Action ID mapper, old -> new

	g_actionRecorder.forEachAction([&](const Action& a) {
		if (a.channelId != channelId)
			return;

		ID newActionId = g_actionRecorder.getNewActionId();

		map.insert({a.id, newActionId});

		Action clone(a);
		clone.id        = newActionId;
		clone.channelId = newChannelId;

		actions.push_back(clone);
		cloned = true;
	});

	/* Update nextId and prevId relationships given the new action ID. */

	for (Action& a : actions)
	{
		if (a.prevId != 0)
			a.prevId = map.at(a.prevId);
		if (a.nextId != 0)
			a.nextId = map.at(a.nextId);
	}

	g_actionRecorder.rec(actions);

	return cloned;
}

/* -------------------------------------------------------------------------- */

void ActionRecorderHandler::liveRec(ID channelId, MidiEvent e, Frame globalFrame)
{
	assert(e.isNoteOnOff()); // Can't record any other kind of events for now

	/* TODO - this might allocate on the MIDI thread */
	if (m_actions.size() >= m_actions.capacity())
		m_actions.reserve(m_actions.size() + MAX_LIVE_RECS_CHUNK);

	m_actions.push_back(g_actionRecorder.makeAction(g_actionRecorder.getNewActionId(), channelId, globalFrame, e));
}

/* -------------------------------------------------------------------------- */

std::unordered_set<ID> ActionRecorderHandler::consolidate()
{
	for (auto it = m_actions.begin(); it != m_actions.end(); ++it)
		consolidate(*it, it - m_actions.begin()); // Pass current index

	g_actionRecorder.rec(m_actions);

	std::unordered_set<ID> out;
	for (const Action& action : m_actions)
		out.insert(action.channelId);

	m_actions.clear();
	return out;
}

/* -------------------------------------------------------------------------- */

void ActionRecorderHandler::clearAllActions()
{
	for (channel::Data& ch : model::get().channels)
		ch.hasActions = false;

	model::swap(model::SwapType::HARD);

	g_actionRecorder.clearAll();
}

/* -------------------------------------------------------------------------- */

ActionRecorder::ActionMap ActionRecorderHandler::deserializeActions(const std::vector<patch::Action>& pactions)
{
	ActionRecorder::ActionMap out;

	/* First pass: add actions with no relationship, that is with no prev/next
	pointers filled in. */

	for (const patch::Action& paction : pactions)
		out[paction.frame].push_back(g_actionRecorder.makeAction(paction));

	/* Second pass: fill in previous and next actions, if any. Is this the
	fastest/smartest way to do it? Maybe not. Optimizations are welcome. */

	for (const patch::Action& paction : pactions)
	{
		if (paction.nextId == 0 && paction.prevId == 0)
			continue;
		Action* curr = const_cast<Action*>(getActionPtrById(paction.id, out));
		assert(curr != nullptr);
		if (paction.nextId != 0)
		{
			curr->next = getActionPtrById(paction.nextId, out);
			assert(curr->next != nullptr);
		}
		if (paction.prevId != 0)
		{
			curr->prev = getActionPtrById(paction.prevId, out);
			assert(curr->prev != nullptr);
		}
	}

	return out;
}

/* -------------------------------------------------------------------------- */

std::vector<patch::Action> ActionRecorderHandler::serializeActions(const ActionRecorder::ActionMap& actions)
{
	std::vector<patch::Action> out;
	for (const auto& kv : actions)
	{
		for (const Action& a : kv.second)
		{
			out.push_back({
			    a.id,
			    a.channelId,
			    a.frame,
			    a.event.getRaw(),
			    a.prevId,
			    a.nextId,
			});
		}
	}
	return out;
}

/* -------------------------------------------------------------------------- */

const Action* ActionRecorderHandler::getActionPtrById(int id, const ActionRecorder::ActionMap& source)
{
	for (const auto& [_, actions] : source)
		for (const Action& action : actions)
			if (action.id == id)
				return &action;
	return nullptr;
}

/* -------------------------------------------------------------------------- */

bool ActionRecorderHandler::areComposite(const Action& a1, const Action& a2) const
{
	return a1.event.getStatus() == MidiEvent::NOTE_ON &&
	       a2.event.getStatus() == MidiEvent::NOTE_OFF &&
	       a1.event.getNote() == a2.event.getNote() &&
	       a1.channelId == a2.channelId;
}

/* -------------------------------------------------------------------------- */

void ActionRecorderHandler::consolidate(const Action& a1, std::size_t i)
{
	/* This algorithm must start searching from the element next to 'a1': since 
	live actions are recorded in linear sequence, the potential partner of 'a1' 
	always lies beyond a1 itself. Without this trick (i.e. if it loops from 
	vector.begin() each time) the algorithm would end up matching wrong partners. */

	for (auto it = m_actions.begin() + i; it != m_actions.end(); ++it)
	{

		const Action& a2 = *it;

		if (!areComposite(a1, a2))
			continue;

		const_cast<Action&>(a1).nextId = a2.id;
		const_cast<Action&>(a2).prevId = a1.id;

		break;
	}
}
} // namespace giada::m
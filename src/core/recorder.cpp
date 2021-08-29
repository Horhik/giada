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
#include "core/action.h"
#include "core/idManager.h"
#include "core/model/model.h"
#include "utils/log.h"
#include <algorithm>
#include <cassert>
#include <memory>

namespace giada::m
{
ActionRecorder::ActionRecorder()
{
	reset();
}

/* -------------------------------------------------------------------------- */

void ActionRecorder::reset()
{
	m_actionId = IdManager();
	clearAll();
}

/* -------------------------------------------------------------------------- */

void ActionRecorder::clearAll()
{
	model::DataLock lock;
	model::getAll<model::Actions>().clear();
}

/* -------------------------------------------------------------------------- */

void ActionRecorder::clearChannel(ID channelId)
{
	removeIf([=](const Action& a) { return a.channelId == channelId; });
}

/* -------------------------------------------------------------------------- */

void ActionRecorder::clearActions(ID channelId, int type)
{
	removeIf([=](const Action& a) {
		return a.channelId == channelId && a.event.getStatus() == type;
	});
}

/* -------------------------------------------------------------------------- */

void ActionRecorder::deleteAction(ID id)
{
	removeIf([=](const Action& a) { return a.id == id; });
}

void ActionRecorder::deleteAction(ID currId, ID nextId)
{
	removeIf([=](const Action& a) { return a.id == currId || a.id == nextId; });
}

/* -------------------------------------------------------------------------- */

void ActionRecorder::updateKeyFrames(std::function<Frame(Frame old)> f)
{
	ActionMap temp;

	/* Copy all existing actions in local map by cloning them, with just a
	difference: they have a new frame value. */

	for (const auto& [oldFrame, actions] : model::getAll<model::Actions>())
	{
		Frame newFrame = f(oldFrame);
		for (const Action& a : actions)
		{
			Action copy = a;
			copy.frame  = newFrame;
			temp[newFrame].push_back(copy);
		}
		G_DEBUG(oldFrame << " -> " << newFrame);
	}

	updateMapPointers(temp);

	model::DataLock lock;
	model::getAll<model::Actions>() = std::move(temp);
}

/* -------------------------------------------------------------------------- */

void ActionRecorder::updateEvent(ID id, MidiEvent e)
{
	model::DataLock lock;
	findAction(model::getAll<model::Actions>(), id)->event = e;
}

/* -------------------------------------------------------------------------- */

void ActionRecorder::updateSiblings(ID id, ID prevId, ID nextId)
{
	model::DataLock lock;

	Action* pcurr = findAction(model::getAll<model::Actions>(), id);
	Action* pprev = findAction(model::getAll<model::Actions>(), prevId);
	Action* pnext = findAction(model::getAll<model::Actions>(), nextId);

	pcurr->prev   = pprev;
	pcurr->prevId = pprev->id;
	pcurr->next   = pnext;
	pcurr->nextId = pnext->id;

	if (pprev != nullptr)
	{
		pprev->next   = pcurr;
		pprev->nextId = pcurr->id;
	}
	if (pnext != nullptr)
	{
		pnext->prev   = pcurr;
		pnext->prevId = pcurr->id;
	}
}

/* -------------------------------------------------------------------------- */

bool ActionRecorder::hasActions(ID channelId, int type) const
{
	for (const auto& [frame, actions] : model::getAll<model::Actions>())
		for (const Action& a : actions)
			if (a.channelId == channelId && (type == 0 || type == a.event.getStatus()))
				return true;
	return false;
}

/* -------------------------------------------------------------------------- */

Action ActionRecorder::makeAction(ID id, ID channelId, Frame frame, MidiEvent e)
{
	Action out{m_actionId.generate(id), channelId, frame, e, -1, -1};
	m_actionId.set(id);
	return out;
}

Action ActionRecorder::makeAction(const patch::Action& a)
{
	m_actionId.set(a.id);
	return Action{a.id, a.channelId, a.frame, a.event, -1, -1, a.prevId,
	    a.nextId};
}

/* -------------------------------------------------------------------------- */

Action ActionRecorder::rec(ID channelId, Frame frame, MidiEvent event)
{
	/* Skip duplicates. */

	if (exists(channelId, frame, event))
		return {};

	Action a = makeAction(0, channelId, frame, event);

	/* If key frame doesn't exist yet, the [] operator in std::map is smart 
	enough to insert a new item first. No plug-in data for now. */

	model::DataLock lock;

	model::getAll<model::Actions>()[frame].push_back(a);
	updateMapPointers(model::getAll<model::Actions>());

	return a;
}

/* -------------------------------------------------------------------------- */

void ActionRecorder::rec(std::vector<Action>& actions)
{
	if (actions.size() == 0)
		return;

	model::DataLock lock;

	ActionMap& map = model::getAll<model::Actions>();

	for (const Action& a : actions)
		if (!exists(a.channelId, a.frame, a.event, map))
			map[a.frame].push_back(a);
	updateMapPointers(map);
}

/* -------------------------------------------------------------------------- */

void ActionRecorder::rec(ID channelId, Frame f1, Frame f2, MidiEvent e1, MidiEvent e2)
{
	model::DataLock lock;

	ActionMap& map = model::getAll<model::Actions>();

	map[f1].push_back(makeAction(0, channelId, f1, e1));
	map[f2].push_back(makeAction(0, channelId, f2, e2));

	Action* a1 = findAction(map, map[f1].back().id);
	Action* a2 = findAction(map, map[f2].back().id);
	a1->nextId = a2->id;
	a2->prevId = a1->id;

	updateMapPointers(map);
}

/* -------------------------------------------------------------------------- */

const std::vector<Action>* ActionRecorder::getActionsOnFrame(Frame frame) const
{
	if (model::getAll<model::Actions>().count(frame) == 0)
		return nullptr;
	return &model::getAll<model::Actions>().at(frame);
}

/* -------------------------------------------------------------------------- */

Action ActionRecorder::getClosestAction(ID channelId, Frame f, int type) const
{
	Action out = {};
	forEachAction([&](const Action& a) {
		if (a.event.getStatus() != type || a.channelId != channelId)
			return;
		if (!out.isValid() || (a.frame <= f && a.frame > out.frame))
			out = a;
	});
	return out;
}

/* -------------------------------------------------------------------------- */

std::vector<Action> ActionRecorder::getActionsOnChannel(ID channelId) const
{
	std::vector<Action> out;
	forEachAction([&](const Action& a) {
		if (a.channelId == channelId)
			out.push_back(a);
	});
	return out;
}

/* -------------------------------------------------------------------------- */

void ActionRecorder::forEachAction(std::function<void(const Action&)> f) const
{
	for (auto& [_, actions] : model::getAll<model::Actions>())
		for (const Action& action : actions)
			f(action);
}

/* -------------------------------------------------------------------------- */

ID ActionRecorder::getNewActionId()
{
	return m_actionId.generate();
}

/* -------------------------------------------------------------------------- */

Action* ActionRecorder::findAction(ActionMap& src, ID id)
{
	for (auto& [frame, actions] : src)
		for (Action& a : actions)
			if (a.id == id)
				return &a;
	assert(false);
	return nullptr;
}

/* -------------------------------------------------------------------------- */

void ActionRecorder::updateMapPointers(ActionMap& src)
{
	for (auto& kv : src)
	{
		for (Action& action : kv.second)
		{
			if (action.nextId != 0)
				action.next = findAction(src, action.nextId);
			if (action.prevId != 0)
				action.prev = findAction(src, action.prevId);
		}
	}
}

/* -------------------------------------------------------------------------- */

void ActionRecorder::optimize(ActionMap& map)
{
	for (auto it = map.cbegin(); it != map.cend();)
		it->second.size() == 0 ? it = map.erase(it) : ++it;
}

/* -------------------------------------------------------------------------- */

void ActionRecorder::removeIf(std::function<bool(const Action&)> f)
{
	model::DataLock lock;

	ActionMap& map = model::getAll<model::Actions>();
	for (auto& [frame, actions] : map)
		actions.erase(std::remove_if(actions.begin(), actions.end(), f), actions.end());
	optimize(map);
	updateMapPointers(map);
}

/* -------------------------------------------------------------------------- */

bool ActionRecorder::exists(ID channelId, Frame frame, const MidiEvent& event, const ActionMap& target) const
{
	for (const auto& [_, actions] : target)
		for (const Action& a : actions)
			if (a.channelId == channelId && a.frame == frame && a.event.getRaw() == event.getRaw())
				return true;
	return false;
}

/* -------------------------------------------------------------------------- */

bool ActionRecorder::exists(ID channelId, Frame frame, const MidiEvent& event) const
{
	return exists(channelId, frame, event, model::getAll<model::Actions>());
}
} // namespace giada::m

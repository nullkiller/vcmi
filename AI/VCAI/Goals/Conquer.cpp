/*
 * Goals.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "Conquer.h"
#include "Build.h"
#include "Defence.h"
#include "Explore.h"
#include "CaptureObjects.h"
#include "GatherArmy.h"
#include "VisitNearestTown.h"
#include "../VCAI.h"
#include "../SectorMap.h"
#include "lib/mapping/CMap.h" //for victory conditions
#include "lib/CPathfinder.h"
#include "../Tasks/VisitTile.h"
#include "../Tasks/BuildStructure.h"
#include "../Tasks/RecruitHero.h"

extern boost::thread_specific_ptr<CCallback> cb;
extern boost::thread_specific_ptr<VCAI> ai;

using namespace Goals;

Tasks::TaskList Conquer::getTasks() {
	auto tasks = Tasks::TaskList();
	auto heroes = cb->getHeroesInfo();

	// lets process heroes according their army strength in descending order
	std::sort(heroes.begin(), heroes.end(), isLevelHigher);

	addTasks(tasks, sptr(Build()), 0.8);
	addTasks(tasks, sptr(RecruitHero()));

	if (tasks.size()) {
		sortByPriority(tasks);

		return tasks;
	}

	addTasks(tasks, sptr(GatherArmy()), 0.7); // no hero - just pickup existing army, no buy
	addTasks(tasks, sptr(CaptureObjects()), 0, 1);

	for (auto nextHero : heroes) {
		if (!nextHero->movement) {
			continue;
		}

		auto heroPtr = HeroPtr(nextHero);
		auto heroTasks = Tasks::TaskList();

		logAi->trace("Considering tasks for hero %s", nextHero->name);


		const CGHeroInstance* strongestHero = heroes.at(0);

		if (cb->getDate(Date::DAY) > 21 && nextHero == strongestHero) {
			addTasks(heroTasks, sptr(Explore().sethero(HeroPtr(heroPtr))), 0.65);
		}
		else {
			addTasks(heroTasks, sptr(Explore().sethero(HeroPtr(heroPtr))), 0.5);
		}

		if (heroTasks.empty() && nextHero->movement > 0) {
			// sometimes there is nothing better than go to the nearest town
			addTasks(heroTasks, sptr(VisitNearestTown().sethero(heroPtr)), 0);
		}

		if (heroTasks.size()) {
			sortByPriority(heroTasks);

			for (auto task : tasks) {
				logAi->trace("found task %s, priority %.3f", task->toString(), task->getPriority());
			}

			tasks.push_back(heroTasks.front());

			break;
		}
	}

	if(tasks.empty())
		return tasks;

	sortByPriority(tasks);

	Tasks::TaskList result;

	result.push_back(tasks[0]);

	return result;
}

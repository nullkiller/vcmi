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
#include "Goals/Conquer.h"
#include "Goals/Build.h"
#include "Goals/Explore.h"
#include "Goals/CaptureObjects.h"
#include "Goals/GatherArmy.h"
#include "VCAI.h"
#include "Fuzzy.h"
#include "SectorMap.h"
#include "../../lib/mapping/CMap.h" //for victory conditions
#include "../../lib/CPathfinder.h"
#include "Tasks/VisitTile.h"
#include "Tasks/BuildStructure.h"
#include "Tasks/RecruitHero.h"

extern boost::thread_specific_ptr<CCallback> cb;
extern boost::thread_specific_ptr<VCAI> ai;

using namespace Goals;

Tasks::TaskList Conquer::getTasks() {
	auto tasks = Tasks::TaskList();
	auto heroes = cb->getHeroesInfo();

	// lets process heroes according their army strength in descending order
	std::sort(heroes.rbegin(), heroes.rend(), isLevelHigher);
	auto nextHero = vstd::tryFindIf(heroes, [](const CGHeroInstance* h) -> bool { return h->movement > 0; });

	addTasks(tasks, sptr(Build()), 0.9);
	addTasks(tasks, sptr(RecruitHero()));
	addTasks(tasks, sptr(GatherArmy()), 0.6); // no hero - just pickup existing army, no buy

	if (nextHero) {
		auto heroPtr = HeroPtr(nextHero.get());
		auto heroTasks = Tasks::TaskList();

		addTasks(heroTasks, sptr(CaptureObjects().ofType(Obj::TOWN).sethero(heroPtr)), 1);
		addTasks(heroTasks, sptr(CaptureObjects().ofType(Obj::HERO).sethero(heroPtr)), 0.95);
		addTasks(heroTasks, sptr(CaptureObjects().ofType(Obj::MINE).sethero(heroPtr)), 0.7);
		addTasks(heroTasks, sptr(CaptureObjects().sethero(HeroPtr(heroPtr))), 0.5);

		auto strongestHero = vstd::maxElementByFun(heroes, [](const CGHeroInstance* h) -> bool { return h->getArmyStrength(); });

		if (cb->getDate(Date::MONTH) > 1 && nextHero.get() == strongestHero[0]) {
			addTasks(heroTasks, sptr(Explore().sethero(HeroPtr(heroPtr))), 0.7);
		}
		else {
			addTasks(heroTasks, sptr(Explore().sethero(HeroPtr(heroPtr))), 0.5);
		}

		if (heroTasks.size()) {
			sortByPriority(heroTasks);
			tasks.push_back(heroTasks.front());
		}
	}

	sortByPriority(tasks);

	return tasks;
}

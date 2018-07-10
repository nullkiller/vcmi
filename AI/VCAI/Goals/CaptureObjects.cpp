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
#include "CaptureObjects.h"
#include "GatherArmy.h"
#include "../VCAI.h"
#include "../AIUtility.h"
#include "../SectorMap.h"
#include "lib/mapping/CMap.h" //for victory conditions
#include "lib/CPathfinder.h"
#include "../Tasks/VisitTile.h"
#include "../Tasks/BuildStructure.h"
#include "../Tasks/RecruitHero.h"

extern boost::thread_specific_ptr<CCallback> cb;
extern boost::thread_specific_ptr<VCAI> ai;

using namespace Goals;

std::string CaptureObjects::toString() const {
	return "Capture objects";
}

Tasks::TaskList CaptureObjects::getTasks() {
	Tasks::TaskList tasks;

	auto neededArmy = 0;
	auto heroes = cb->getHeroesInfo();
	
	auto captureObjects = [&](std::vector<const CGObjectInstance*> objs) -> bool {
		if (objs.empty()) {
			return false;
		}

		for (auto objToVisit : objs) {
			const int3 pos = objToVisit->visitablePos();

			auto pathInfo = ai->turnData->getChainInfo(pos);

			logAi->trace("considering object %s %s", objToVisit->getObjectName(), pos.toString());

			for(const CHeroChainPath & chainPath : pathInfo)
			{
				const CHeroChainPathNode & node = chainPath.nodes.back();
				HeroPtr hero = node.hero;

				auto sm = ai->getCachedSectorMap(hero);

				logAi->trace("Hero %s can rich %s", hero->getObjectName(), objToVisit->getObjectName());

				if (!this->shouldVisitObject(objToVisit, hero, *sm)) {
					continue;
				}

				//auto targetPos = sm->firstTileToGet(hero, pos);
				auto missingArmy = analyzeDanger(hero, pos);

				if(!missingArmy)
				{
					double priority = 1 - node.movementPointsUsed / (double)hero->maxMovePoints(true);

					if(priority < 0)
						priority = 0;
					
					logAi->trace("Hero %s can take %s", hero->getObjectName(), objToVisit->getObjectName());

					if(node.turns == 0)
					{
						addTask(tasks, Tasks::VisitTile(pos, hero, objToVisit), priority);
					}

					break;
				}
				else if(neededArmy == 0 || neededArmy > missingArmy)
				{
					neededArmy = missingArmy;
				}
			}
		}

		return false;
	};

	if (specificObjects) {
		captureObjects(objectsToCapture);
	}
	else {
		captureObjects(std::vector<const CGObjectInstance*>(ai->visitableObjs.begin(), ai->visitableObjs.end()));
	}

	/*if (!tasks.size() && neededArmy) {
		addTasks(tasks, sptr(GatherArmy(neededArmy, forceGatherArmy).sethero(this->hero)));
	}*/
	return tasks;
}

bool CaptureObjects::shouldVisitObject(ObjectIdRef obj, HeroPtr hero, SectorMap& sm) {
	const CGObjectInstance* objInstance = obj;

	if (!objInstance || objectTypes.size() && !vstd::contains(objectTypes, objInstance->ID.num)) {
		return false;
	}

	if (!objInstance || objectSubTypes.size() && !vstd::contains(objectSubTypes, objInstance->subID)) {
		return false;
	}

	const int3 pos = objInstance->visitablePos();
	const int3 targetPos = sm.firstTileToGet(hero, pos);

	if (!targetPos.valid()
		|| vstd::contains(ai->alreadyVisited, objInstance)) {
		return false;
	}

	if (objInstance->wasVisited(hero.get())) {
		return false;
	}

	auto playerRelations = cb->getPlayerRelations(ai->playerID, objInstance->tempOwner);
	if (playerRelations != PlayerRelations::ENEMIES && !isWeeklyRevisitable(objInstance)) {
		return false;
	}

	if (!shouldVisit(hero, objInstance)) {
		return false;
	}

	if (!ai->isAccessibleForHero(targetPos, hero))
	{
		return false;
	}

	//it may be hero visiting this obj
	//we don't try visiting object on which allied or owned hero stands
	// -> it will just trigger exchange windows and AI will be confused that obj behind doesn't get visited
	const CGObjectInstance * topObj = cb->getTopObj(obj->visitablePos());

	if (topObj->ID == Obj::HERO && cb->getPlayerRelations(hero->tempOwner, topObj->tempOwner) != PlayerRelations::ENEMIES)
		return false;
	else
		return true; //all of the following is met
}

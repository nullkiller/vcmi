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
#include "../VCAI.h"
#include "../AIUtility.h"
#include "../SectorMap.h"
#include "lib/mapping/CMap.h" //for victory conditions
#include "lib/CPathfinder.h"
#include "../Tasks/ExecuteChain.h"
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
	
	auto captureObjects = [&](std::vector<const CGObjectInstance*> objs) -> void {
		if (objs.empty()) {
			return;
		}

		for (auto objToVisit : objs) {
			const int3 pos = objToVisit->visitablePos();
			auto pathInfo = ai->turnData->getChainInfo(pos);
			Tasks::TaskPtr bestTask;

			logAi->trace("considering object %s %s", objToVisit->getObjectName(), pos.toString());

			for(const CHeroChainPath & chainPath : pathInfo)
			{
				const CHeroChainPathNode & node = chainPath.nodes.front();
				HeroPtr hero = node.hero;

				auto armyLoss = node.armyLoss;
				auto totalArmy = node.armyLoss + node.armyValue;
				
				if(node.action != CGBaseNode::ENodeAction::BLOCKING_VISIT)
					armyLoss += evaluateLoss(hero, objToVisit->visitablePos(), node.armyValue, true);

				if(armyLoss > node.armyValue) // we lost 50% or more
				{
					logAi->trace(
						"Hero %s can rich %s but he is too week. Estimated loss is %d of %d", 
						hero->getObjectName(), 
						objToVisit->getObjectName(),
						armyLoss,
						totalArmy);

					continue;
				}

				logAi->trace("Hero %s can rich %s", hero->getObjectName(), objToVisit->getObjectName());

				if (!this->shouldVisitObject(objToVisit, hero)) {
					continue;
				}

				logAi->trace("Hero %s should take %s", hero->getObjectName(), objToVisit->getObjectName());

				auto newTask = Tasks::sptr(Tasks::ExecuteChain(chainPath, totalArmy, armyLoss, objToVisit));

				if(!bestTask || bestTask->getPriority() < newTask->getPriority())
				{
					bestTask = newTask;
				}
			}

			if(bestTask && bestTask->canExecute())
			{
				tasks.push_back(bestTask);
			}
		}
	};

	if (specificObjects) {
		captureObjects(objectsToCapture);
	}
	else {
		captureObjects(std::vector<const CGObjectInstance*>(ai->visitableObjs.begin(), ai->visitableObjs.end()));
	}

	return tasks;
}

bool CaptureObjects::shouldVisitObject(ObjectIdRef obj, HeroPtr hero) {
	const CGObjectInstance* objInstance = obj;

	if (!objInstance || objectTypes.size() && !vstd::contains(objectTypes, objInstance->ID.num)) {
		return false;
	}

	if (!objInstance || objectSubTypes.size() && !vstd::contains(objectSubTypes, objInstance->subID)) {
		return false;
	}

	const int3 pos = objInstance->visitablePos();

	if (vstd::contains(ai->alreadyVisited, objInstance)) {
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

	//it may be hero visiting this obj
	//we don't try visiting object on which allied or owned hero stands
	// -> it will just trigger exchange windows and AI will be confused that obj behind doesn't get visited
	const CGObjectInstance * topObj = cb->getTopObj(obj->visitablePos());

	if (topObj->ID == Obj::HERO && cb->getPlayerRelations(hero->tempOwner, topObj->tempOwner) != PlayerRelations::ENEMIES)
		return false;
	else
		return true; //all of the following is met
}

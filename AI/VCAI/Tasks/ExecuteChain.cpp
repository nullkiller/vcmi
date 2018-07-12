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
#include "../VCAI.h"
#include "../Fuzzy.h"
#include "lib/CPathfinder.h"
#include "ExecuteChain.h"

extern boost::thread_specific_ptr<CCallback> cb;
extern boost::thread_specific_ptr<VCAI> ai;
extern FuzzyHelper * fh;

using namespace Tasks;

double getTurns(const CHeroChainPathNode & node)
{
	auto maxMp = node.hero->maxMovePoints(node.layer == EPathfindingLayer::LAND);
	
	return (double)node.movementPointsUsed / maxMp;
}

ExecuteChain::ExecuteChain(
	const CHeroChainPath & chainPath, 
	uint64_t totalArmy,
	uint64_t totalArmyLoss,
	const CGObjectInstance * obj)
{
	if(obj)
	{
		objInfo = obj->getObjectName();
	}

	auto finalNode = chainPath.nodes.front();

	movementPointsUsed = 0;
	turns = 0;
	complexity = chainPath.nodes.size();
	armyLoss = totalArmyLoss;
	armyTotal = totalArmy;
	armyLeft = totalArmy - totalArmyLoss;

	const CGHeroInstance * previousHero = nullptr;
	const CGObjectInstance * currentObj = obj;

	for(const CHeroChainPathNode & node : chainPath.nodes)
	{
		movementPointsUsed += node.movementPointsUsed;
		vstd::amax(turns, getTurns(node));

		if(previousHero != nullptr && previousHero != node.hero)
		{
			subTasks.push_back(sptr(VisitTile(node.targetPosition, previousHero, node.hero)));
		}

		subTasks.push_back(sptr(VisitTile(node.targetPosition, node.hero, currentObj)));
		previousHero = node.hero;
	}

	std::reverse(subTasks.begin(), subTasks.end());

	priority = fh->evaluate(this, finalNode.hero, obj);
}

void ExecuteChain::execute()
{
	for(auto task : subTasks)
	{
		if(task->canExecute())
			task->execute();
	}
}

bool ExecuteChain::canExecute()
{
	return subTasks[0]->canExecute();
}

std::string Tasks::ExecuteChain::toString()
{
	auto baseInfo = "ExecuteChain of " + std::to_string(subTasks.size()) + " tasks";

	return objInfo.size() ? baseInfo + " in order to get [" + objInfo + "]" : baseInfo;
}
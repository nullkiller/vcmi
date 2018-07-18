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
#include "CHeroChainAnalyser.h"

extern boost::thread_specific_ptr<CCallback> cb;
extern boost::thread_specific_ptr<VCAI> ai;

TurnData::TurnData(PlayerColor playerID)
	: chainConfig(std::make_shared<CVCAIHeroChainConfig>(playerID))
{
}

void TurnData::update()
{
	CHeroChainAnalyser().fill(this);
}

std::vector<CHeroChainPath> TurnData::getChainInfo(int3 pos)
{
	std::vector<CHeroChainPath> paths;
	auto chains = chainInfo->nodes[pos.x][pos.y][pos.z][EPathfindingLayer::LAND];

	for(const CHeroNode & node : chains)
	{
		if(node.action == CGBaseNode::ENodeAction::UNKNOWN)
		{
			continue;
		}

		auto path = CHeroChainPath();
		auto current = &node;

		while(current != nullptr)
		{
			auto pathNode = CHeroChainPathNode();

			pathNode.hero = chainConfig->getNodeHero(*chainInfo, &node);
			pathNode.movementPointsLeft = current->moveRemains;
			pathNode.movementPointsUsed = (int)(current->turns * pathNode.hero->maxMovePoints(true) + pathNode.hero->movement) - (int)current->moveRemains;
			pathNode.turns = current->turns;
			pathNode.armyLoss = current->armyLoss;
			pathNode.armyValue = current->armyValue;
			pathNode.action = current->action;
			pathNode.layer = current->layer;
			pathNode.targetPosition = current->coord;

			path.nodes.push_back(pathNode);
			current = current->previousActor;
		}
		paths.push_back(path);
	}

	return paths;
}
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
		auto path = CHeroChainPath();
		auto pathNode = CHeroChainPathNode();

		pathNode.hero = chainConfig->getNodeHero(*chainInfo, &node);
		pathNode.movementPointsLeft = node.moveRemains;
		pathNode.movementPointsUsed = (int)(node.turns * pathNode.hero->maxMovePoints(true) + pathNode.hero->movement) - (int)node.moveRemains;
		pathNode.turns = node.turns;
		
		path.nodes.push_back(pathNode);
		paths.push_back(path);
	}

	return paths;
}